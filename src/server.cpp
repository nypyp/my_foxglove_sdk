#include <foxglove/server.hpp>

#include <foxglove/channel.hpp>
#include <foxglove/protocol.hpp>

#include <nlohmann/json.hpp>
#include <libwebsockets.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace foxglove {

// Protocol version string
static constexpr const char* kProtocolVersion = "0.0.1";
static constexpr const char* kSubProtocol = "foxglove.websocket.v1";

// Binary opcode definitions
static constexpr uint8_t kOpMessageData = 0x01;
static constexpr uint8_t kOpTime = 0x02;

/// @brief Per-session client state.
struct ClientSession {
  /// Subscription ID -> Channel ID
  std::unordered_map<uint32_t, uint32_t> subscriptions;
  /// Set of channel IDs this client has subscribed to
  std::unordered_set<uint32_t> subscribed_channels;
  /// Pending messages to send (queued from other threads)
  std::vector<std::vector<uint8_t>> pending_writes;
  std::mutex pending_mutex;
};

class WebSocketServer::Impl {
 public:
  Impl(WebSocketServerOptions options)
      : options_(std::move(options)), interrupted_(false) {}

  ~Impl() { shutdown(); }

  FoxgloveResult<void> start() {
    // Create lws context
    struct lws_context_creation_info info{};
    info.port = options_.port;
    info.protocols = protocols_;
    info.user = this;
    info.gid = -1;
    info.uid = -1;

    context_ = lws_create_context(&info);
    if (!context_) {
      return tl::make_unexpected(FoxgloveError::ServerStartFailed);
    }

    // Start service thread
    service_thread_ = std::thread([this]() {
      while (!interrupted_) {
        lws_service(context_, 50);  // 50ms timeout
      }
    });

    return {};
  }

  void shutdown() {
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
  }

  void add_channel(RawChannel& channel) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t channel_id = channel.id();
    channels_[channel_id] = &channel;

    // Wire the channel callback to dispatch to subscribers
    channel.set_callback([this, channel_id](uint32_t, const uint8_t* data, size_t len,
                                            uint64_t log_time) {
      dispatch_message(channel_id, data, len, log_time);
    });

    // Advertise to all connected clients
    advertise_channel_to_all(channel_id);
  }

  void remove_channel(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      // Unwire the callback
      it->second->set_callback(nullptr);
      channels_.erase(it);
    }

    // Unadvertise to all connected clients
    unadvertise_channel_to_all(channel_id);
  }

  void broadcast_time(uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint8_t> msg(9);
    msg[0] = kOpTime;
    std::memcpy(msg.data() + 1, &timestamp, sizeof(timestamp));

    for (auto& [wsi, session] : sessions_) {
      std::lock_guard<std::mutex> session_lock(session->pending_mutex);
      session->pending_writes.push_back(msg);
      lws_callback_on_writable(wsi);
    }

    lws_cancel_service(context_);
  }

  // Called from lws callback when client connects
  void on_client_connected(struct lws* wsi) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto session = std::make_unique<ClientSession>();
    sessions_[wsi] = std::move(session);

    // Send serverInfo
    ServerInfo info;
    info.name = options_.name;
    info.capabilities = options_.capabilities;
    info.supported_encodings = {"json", "protobuf"};
    info.protocol_version = kProtocolVersion;

    auto result = encode_server_info(info);
    if (result.has_value()) {
      queue_text_message(wsi, result.value());
    }

    // Advertise all existing channels
    for (const auto& [channel_id, channel] : channels_) {
      advertise_channel(wsi, channel_id);
    }
  }

  // Called from lws callback when client disconnects
  void on_client_disconnected(struct lws* wsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(wsi);
  }

  // Called from lws callback when text message received
  void on_client_message(struct lws* wsi, const char* data, size_t len) {
    std::string json_str(data, len);
    auto result = decode_client_message(json_str);
    if (!result.has_value()) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(wsi);
    if (it == sessions_.end()) return;

    auto& session = *it->second;

    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, Subscribe>) {
            for (const auto& sub : arg.subscriptions) {
              session.subscriptions[sub.subscription_id] = sub.channel_id;
              session.subscribed_channels.insert(sub.channel_id);
              if (options_.callbacks.on_subscribe) {
                options_.callbacks.on_subscribe(sub.channel_id, sub.subscription_id);
              }
            }
          } else if constexpr (std::is_same_v<T, Unsubscribe>) {
            for (uint32_t sub_id : arg.subscription_ids) {
              auto sub_it = session.subscriptions.find(sub_id);
              if (sub_it != session.subscriptions.end()) {
                session.subscribed_channels.erase(sub_it->second);
                session.subscriptions.erase(sub_it);
                if (options_.callbacks.on_unsubscribe) {
                  options_.callbacks.on_unsubscribe(sub_id);
                }
              }
            }
          }
        },
        result.value());
  }

  // Called from lws callback when writable
  void on_writable(struct lws* wsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(wsi);
    if (it == sessions_.end()) return;

    auto& session = *it->second;

    std::lock_guard<std::mutex> pending_lock(session.pending_mutex);
    if (session.pending_writes.empty()) return;

    const auto& msg = session.pending_writes.front();

    size_t buf_len = LWS_PRE + msg.size();
    std::vector<uint8_t> buf(buf_len);
    std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());

    bool is_binary = (msg.size() > 0 && msg[0] <= 0x02);
    lws_write_protocol protocol = is_binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;

    int written = lws_write(wsi, buf.data() + LWS_PRE, msg.size(), protocol);
    if (written >= 0) {
      session.pending_writes.erase(session.pending_writes.begin());
    }

    if (!session.pending_writes.empty()) {
      lws_callback_on_writable(wsi);
    }
  }

 private:
  void dispatch_message(uint32_t channel_id, const uint8_t* data, size_t len,
                        uint64_t log_time) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [wsi, session] : sessions_) {
      for (const auto& [sub_id, ch_id] : session->subscriptions) {
        if (ch_id == channel_id) {
          MessageData msg_data;
          msg_data.subscription_id = sub_id;
          msg_data.log_time = log_time;
          msg_data.data.assign(data, data + len);

          auto result = encode_message_data(msg_data);
          if (result.has_value()) {
            std::lock_guard<std::mutex> pending_lock(session->pending_mutex);
            session->pending_writes.push_back(std::move(result.value()));
            lws_callback_on_writable(wsi);
          }
          break;
        }
      }
    }

    if (context_) {
      lws_cancel_service(context_);
    }
  }

  void advertise_channel_to_all(uint32_t channel_id) {
    for (auto& [wsi, session] : sessions_) {
      advertise_channel(wsi, channel_id);
    }
  }

  void advertise_channel(struct lws* wsi, uint32_t channel_id) {
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return;

    const auto& descriptor = it->second->descriptor();

    ChannelAdvertisement adv;
    adv.id = descriptor.id;
    adv.topic = descriptor.topic;
    adv.encoding = descriptor.encoding;
    adv.schema_name = descriptor.schema.name;
    adv.schema_encoding = descriptor.schema.encoding;
    adv.schema_data = std::string(descriptor.schema.data.begin(), descriptor.schema.data.end());

    auto result = encode_advertise({adv});
    if (result.has_value()) {
      queue_text_message(wsi, result.value());
    }
  }

  void unadvertise_channel_to_all(uint32_t channel_id) {
    nlohmann::json unadvertise_msg = {{"op", "unadvertise"}, {"channelIds", {channel_id}}};
    std::string msg_str = unadvertise_msg.dump();

    for (auto& [wsi, session] : sessions_) {
      queue_text_message(wsi, msg_str);
    }
  }

  void queue_text_message(struct lws* wsi, const std::string& text) {
    auto it = sessions_.find(wsi);
    if (it == sessions_.end()) return;

    auto& session = *it->second;
    std::lock_guard<std::mutex> pending_lock(session.pending_mutex);
    session.pending_writes.emplace_back(text.begin(), text.end());
    lws_callback_on_writable(wsi);
  }

  static int lws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user,
                          void* in, size_t len) {
    auto* context = lws_get_context(wsi);
    auto* impl = static_cast<Impl*>(lws_context_user(context));
    if (!impl) return 0;

    switch (reason) {
      case LWS_CALLBACK_ESTABLISHED:
        impl->on_client_connected(wsi);
        break;

      case LWS_CALLBACK_CLOSED:
        impl->on_client_disconnected(wsi);
        break;

      case LWS_CALLBACK_RECEIVE:
        if (!lws_frame_is_binary(wsi)) {
          impl->on_client_message(wsi, static_cast<const char*>(in), len);
        }
        break;

      case LWS_CALLBACK_SERVER_WRITEABLE:
        impl->on_writable(wsi);
        break;

      case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
        // Validate subprotocol
        if (lws_hdr_total_length(wsi, WSI_TOKEN_PROTOCOL) > 0) {
          char protocol[256];
          int n = lws_hdr_copy(wsi, protocol, sizeof(protocol), WSI_TOKEN_PROTOCOL);
          if (n > 0) {
            if (std::strcmp(protocol, kSubProtocol) != 0) {
              return 1;  // Reject connection
            }
          }
        }
        break;

      default:
        break;
    }

    return 0;
  }

  WebSocketServerOptions options_;
  lws_context* context_ = nullptr;
  std::thread service_thread_;
  std::atomic<bool> interrupted_;

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, RawChannel*> channels_;
  std::unordered_map<struct lws*, std::unique_ptr<ClientSession>> sessions_;

  // Protocols definition — must be a static C array (not std::vector) because
  // lws keeps a raw pointer to this array for the lifetime of the context.
  static constexpr lws_protocols protocols_[] = {
      {kSubProtocol, lws_callback, 0, 4096, 0, nullptr, 0},
      {nullptr, nullptr, 0, 0, 0, nullptr, 0}  // terminator
  };
};

// WebSocketServer public implementation

WebSocketServer::WebSocketServer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

WebSocketServer::~WebSocketServer() = default;

FoxgloveResult<WebSocketServer> WebSocketServer::create(WebSocketServerOptions options) {
  auto impl = std::make_unique<Impl>(std::move(options));
  auto result = impl->start();
  if (!result.has_value()) {
    return tl::make_unexpected(result.error());
  }
  return WebSocketServer(std::move(impl));
}

void WebSocketServer::add_channel(RawChannel& channel) {
  if (impl_) {
    impl_->add_channel(channel);
  }
}

void WebSocketServer::remove_channel(uint32_t channel_id) {
  if (impl_) {
    impl_->remove_channel(channel_id);
  }
}

void WebSocketServer::broadcast_time(uint64_t timestamp) {
  if (impl_) {
    impl_->broadcast_time(timestamp);
  }
}

void WebSocketServer::shutdown() {
  if (impl_) {
    impl_->shutdown();
  }
}

WebSocketServer::WebSocketServer(WebSocketServer&& other) noexcept
    : impl_(std::move(other.impl_)) {}

WebSocketServer& WebSocketServer::operator=(WebSocketServer&& other) noexcept {
  if (this != &other) {
    impl_ = std::move(other.impl_);
  }
  return *this;
}

}  // namespace foxglove
