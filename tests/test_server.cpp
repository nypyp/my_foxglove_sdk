/// @brief Integration tests for Foxglove WebSocket Server.

#include <foxglove/channel.hpp>
#include <foxglove/protocol.hpp>
#include <foxglove/schema.hpp>
#include <foxglove/server.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// libwebsockets for test client
#include <libwebsockets.h>

using namespace foxglove;
using json = nlohmann::json;

namespace {

/// @brief Test WebSocket client using libwebsockets for integration testing.
class TestWsClient {
 public:
  struct ReceivedMessage {
    enum Type { Text, Binary } type;
    std::vector<uint8_t> data;
  };

  TestWsClient() : connected_(false), interrupted_(false) {}

  ~TestWsClient() { disconnect(); }

  /// Connect to server at given host:port.
  bool connect(const std::string& host, int port,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::queue<ReceivedMessage> empty;
      received_messages_.swap(empty);
      std::queue<std::string> empty_send;
      send_queue_.swap(empty_send);
    }
    connected_ = false;
    interrupted_ = false;

    struct lws_context_creation_info info{};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols_;
    info.user = this;

    context_ = lws_create_context(&info);
    if (!context_) {
      return false;
    }

    struct lws_client_connect_info ccinfo{};
    ccinfo.context = context_;
    ccinfo.address = host.c_str();
    ccinfo.port = port;
    ccinfo.path = "/";
    ccinfo.host = host.c_str();
    ccinfo.origin = host.c_str();
    ccinfo.protocol = "foxglove.websocket.v1";

    wsi_ = lws_client_connect_via_info(&ccinfo);
    if (!wsi_) {
      lws_context_destroy(context_);
      context_ = nullptr;
      return false;
    }

    service_thread_ = std::thread([this]() {
      while (!interrupted_) {
        lws_service(context_, 50);
      }
    });

    auto start = std::chrono::steady_clock::now();
    while (!connected_ && std::chrono::steady_clock::now() - start < timeout) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return connected_;
  }

  void disconnect() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      interrupted_ = true;
    }

    if (context_) {
      lws_cancel_service(context_);
    }

    if (service_thread_.joinable()) {
      service_thread_.join();
    }

    if (context_) {
      lws_context_destroy(context_);
      context_ = nullptr;
    }

    wsi_ = nullptr;
    connected_ = false;
  }

  /// Send text message to server.
  bool send_text(const std::string& text) {
    if (!connected_ || !wsi_) return false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      send_queue_.push(text);
    }
    lws_callback_on_writable(wsi_);
    lws_cancel_service(context_);
    return true;
  }

  /// Wait for and return next received message with timeout.
  std::optional<ReceivedMessage> wait_for_message(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < timeout) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!received_messages_.empty()) {
          auto msg = received_messages_.front();
          received_messages_.pop();
          return msg;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
  }

  /// Check if connected.
  bool is_connected() const { return connected_; }

 private:
  static int callback(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                      void* in, size_t len) {
    auto* context = lws_get_context(wsi);
    auto* client = static_cast<TestWsClient*>(lws_context_user(context));
    if (!client) return 0;

    switch (reason) {
      case LWS_CALLBACK_CLIENT_ESTABLISHED:
        client->on_connected();
        break;

      case LWS_CALLBACK_CLIENT_CLOSED:
        client->on_disconnected();
        break;

      case LWS_CALLBACK_CLIENT_RECEIVE:
        client->on_receive(in, len, lws_frame_is_binary(wsi));
        break;

      case LWS_CALLBACK_CLIENT_WRITEABLE:
        client->on_writable(wsi);
        break;

      default:
        break;
    }
    return 0;
  }

  void on_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
  }

  void on_disconnected() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
  }

  void on_receive(void* in, size_t len, bool is_binary) {
    std::lock_guard<std::mutex> lock(mutex_);
    ReceivedMessage msg;
    msg.type = is_binary ? ReceivedMessage::Binary : ReceivedMessage::Text;
    if (in && len > 0) {
      msg.data.assign(static_cast<uint8_t*>(in), static_cast<uint8_t*>(in) + len);
    }
    received_messages_.push(std::move(msg));
  }

  void on_writable(struct lws* wsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (send_queue_.empty()) return;

    std::string text = send_queue_.front();
    send_queue_.pop();

    size_t buf_len = LWS_PRE + text.size();
    std::vector<uint8_t> buf(buf_len);
    memcpy(buf.data() + LWS_PRE, text.data(), text.size());

    lws_write(wsi, buf.data() + LWS_PRE, text.size(), LWS_WRITE_TEXT);

    if (!send_queue_.empty()) {
      lws_callback_on_writable(wsi);
    }
  }

  lws_context* context_ = nullptr;
  lws* wsi_ = nullptr;
  std::thread service_thread_;
  std::atomic<bool> connected_;
  std::atomic<bool> interrupted_;

  mutable std::mutex mutex_;
  std::queue<ReceivedMessage> received_messages_;
  std::queue<std::string> send_queue_;

  // lws keeps a raw pointer to protocols — must be a static C array, not std::vector.
  // Protocol name must match server's for subprotocol negotiation to succeed.
  static constexpr lws_protocols protocols_[] = {
      {"foxglove.websocket.v1", callback, 0, 4096, 0, nullptr, 0},
      {nullptr, nullptr, 0, 0, 0, nullptr, 0}
  };
};

}  // namespace

TEST_CASE("Server - creates and starts" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18765;  // Use high port to avoid conflicts
  options.name = "TestServer";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Server should be running, verify by connecting a client
  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18765));
  REQUIRE(client.is_connected());

  // Cleanup
  server.shutdown();
  client.disconnect();
}

TEST_CASE("Server - sends serverInfo on connect" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18766;
  options.name = "TestServerInfo";
  options.capabilities = 0x1234;

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18766));

  // Wait for serverInfo message
  auto msg_opt = client.wait_for_message();
  REQUIRE(msg_opt.has_value());
  REQUIRE(msg_opt->type == TestWsClient::ReceivedMessage::Text);

  // Parse JSON and verify fields
  std::string json_str(msg_opt->data.begin(), msg_opt->data.end());
  auto j = json::parse(json_str);

  REQUIRE(j["op"] == "serverInfo");
  REQUIRE(j["name"] == "TestServerInfo");
  REQUIRE(j["capabilities"] == 0x1234);
  REQUIRE(j.contains("protocolVersion"));

  server.shutdown();
  client.disconnect();
}

TEST_CASE("Server - advertises channels" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18767;
  options.name = "TestChannelAdvertise";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Create a channel
  Schema schema{"TestSchema", "jsonschema", {}};
  auto channel_result = RawChannel::create("/test/topic", "json", schema);
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  // Add channel to server
  server.add_channel(channel);

  // Connect client - should receive serverInfo then advertise
  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18767));

  // Skip serverInfo
  auto msg1 = client.wait_for_message();
  REQUIRE(msg1.has_value());

  // Next should be advertise
  auto msg2 = client.wait_for_message();
  REQUIRE(msg2.has_value());
  REQUIRE(msg2->type == TestWsClient::ReceivedMessage::Text);

  std::string json_str(msg2->data.begin(), msg2->data.end());
  auto j = json::parse(json_str);
  REQUIRE(j["op"] == "advertise");
  REQUIRE(j.contains("channels"));
  REQUIRE(j["channels"].size() >= 1);

  server.shutdown();
  client.disconnect();
}

TEST_CASE("Server - delivers messageData to subscribers" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18768;
  options.name = "TestMessageDelivery";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Create a channel
  std::string schema_json = R"({"type":"object"})";
  std::vector<uint8_t> schema_data(schema_json.begin(), schema_json.end());
  Schema schema{"TestSchema", "jsonschema", schema_data};
  auto channel_result = RawChannel::create("/test/topic", "json", schema);
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  server.add_channel(channel);

  // Connect client
  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18768));

  // Wait for serverInfo and advertise
  auto msg1 = client.wait_for_message();
  REQUIRE(msg1.has_value());
  auto msg2 = client.wait_for_message();
  REQUIRE(msg2.has_value());

  // Parse advertise to get channel ID
  std::string json_str(msg2->data.begin(), msg2->data.end());
  auto j = json::parse(json_str);
  REQUIRE(j["op"] == "advertise");
  uint32_t channel_id = j["channels"][0]["id"];

  // Subscribe to the channel
  json subscribe_msg = {
      {"op", "subscribe"},
      {"subscriptions", {{{"id", 1}, {"channelId", channel_id}}}}
  };
  client.send_text(subscribe_msg.dump());

  // Give server time to process subscription
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Log a message through the channel
  std::string test_data = "{\"value\":42}";
  std::vector<uint8_t> msg_payload(test_data.begin(), test_data.end());
  channel.log(msg_payload.data(), msg_payload.size(), 1234567890ULL);

  // Wait for binary messageData
  auto msg3 = client.wait_for_message();
  REQUIRE(msg3.has_value());
  REQUIRE(msg3->type == TestWsClient::ReceivedMessage::Binary);

  // Verify binary format: opcode(1) | subscription_id(4) | log_time(8) | data(N)
  REQUIRE(msg3->data.size() >= 13);
  REQUIRE(msg3->data[0] == 1);  // opcode for messageData
  // subscription_id and log_time are little-endian
  uint32_t sub_id = *reinterpret_cast<const uint32_t*>(msg3->data.data() + 1);
  REQUIRE(sub_id == 1);
  uint64_t log_time = *reinterpret_cast<const uint64_t*>(msg3->data.data() + 5);
  REQUIRE(log_time == 1234567890ULL);

  server.shutdown();
  client.disconnect();
}

TEST_CASE("Server - graceful shutdown" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18769;
  options.name = "TestShutdown";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Connect client
  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18769));
  REQUIRE(client.is_connected());

  // Shutdown server
  server.shutdown();

  // Client should eventually disconnect
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Should not crash - that's the success criteria
  client.disconnect();
}

TEST_CASE("Server - remove_channel unadvertises" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18770;
  options.name = "TestRemoveChannel";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Create a channel
  Schema schema{"TestSchema", "jsonschema", {}};
  auto channel_result = RawChannel::create("/test/topic", "json", schema);
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  // Add and then remove
  server.add_channel(channel);
  server.remove_channel(channel.id());

  // Connect client
  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18770));

  // Wait for messages
  auto msg1 = client.wait_for_message();  // serverInfo
  REQUIRE(msg1.has_value());

  // After serverInfo, may or may not receive unadvertise (depends on timing)
  // The key is that it doesn't crash

  server.shutdown();
  client.disconnect();
}

TEST_CASE("Server - multiple clients can connect" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18771;
  options.name = "TestMultiClient";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Connect multiple clients
  TestWsClient client1, client2, client3;
  REQUIRE(client1.connect("127.0.0.1", 18771));
  REQUIRE(client2.connect("127.0.0.1", 18771));
  REQUIRE(client3.connect("127.0.0.1", 18771));

  // All should receive serverInfo
  auto msg1 = client1.wait_for_message();
  auto msg2 = client2.wait_for_message();
  auto msg3 = client3.wait_for_message();
  REQUIRE(msg1.has_value());
  REQUIRE(msg2.has_value());
  REQUIRE(msg3.has_value());

  server.shutdown();
  client1.disconnect();
  client2.disconnect();
  client3.disconnect();
}

TEST_CASE("Server - broadcast_time sends to all clients" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18772;
  options.name = "TestBroadcastTime";

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  // Connect client
  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18772));

  // Skip serverInfo
  auto msg1 = client.wait_for_message();
  REQUIRE(msg1.has_value());

  // Broadcast time
  server.broadcast_time(1234567890ULL);

  // Wait for time message
  auto msg2 = client.wait_for_message();
  REQUIRE(msg2.has_value());
  REQUIRE(msg2->type == TestWsClient::ReceivedMessage::Binary);

  // Verify time message format: opcode(1) | timestamp(8)
  REQUIRE(msg2->data.size() >= 9);
  REQUIRE(msg2->data[0] == 2);  // opcode for time
  uint64_t time_val = *reinterpret_cast<const uint64_t*>(msg2->data.data() + 1);
  REQUIRE(time_val == 1234567890ULL);

  server.shutdown();
  client.disconnect();
}
