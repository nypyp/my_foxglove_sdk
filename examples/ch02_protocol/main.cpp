#include <foxglove/protocol.hpp>

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// Helper to print hex dump of binary data
void print_hex_dump(const std::vector<uint8_t>& data) {
  std::cout << "Hex dump (" << data.size() << " bytes):\n";
  for (size_t i = 0; i < data.size(); ++i) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(data[i]) << " ";
    if ((i + 1) % 16 == 0) {
      std::cout << "\n";
    }
  }
  if (data.size() % 16 != 0) {
    std::cout << "\n";
  }
  std::cout << std::dec;  // Reset to decimal
}

int main() {
  // Example 1: Construct a ServerInfo and print its JSON encoding
  std::cout << "=== Example 1: ServerInfo JSON Encoding ===\n";
  {
    foxglove::ServerInfo info;
    info.name = "My Foxglove Server";
    info.capabilities = 3;  // ClientPublish | ConnectionGraph
    info.supported_encodings = {"json", "protobuf", "ros1"};
    info.metadata = {{"version", "1.0.0"}, {"author", "Tutorial"}};
    info.session_id = "session-123456";

    auto result = foxglove::encode_server_info(info);
    if (result.has_value()) {
      std::cout << "Encoded ServerInfo JSON:\n" << result.value() << "\n\n";
    } else {
      std::cout << "Failed to encode ServerInfo: "
                << foxglove::foxglove_error_string(result.error()) << "\n\n";
    }
  }

  // Example 2: Construct ChannelAdvertisement list and print JSON
  std::cout << "=== Example 2: Channel Advertisement JSON Encoding ===\n";
  {
    std::vector<foxglove::ChannelAdvertisement> channels;

    foxglove::ChannelAdvertisement ch1;
    ch1.id = 1;
    ch1.topic = "/robot/pose";
    ch1.encoding = "json";
    ch1.schema_name = "geometry_msgs/Pose";
    ch1.schema_encoding = "jsonschema";
    ch1.schema_data =
        "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\"}}}";
    channels.push_back(ch1);

    foxglove::ChannelAdvertisement ch2;
    ch2.id = 2;
    ch2.topic = "/camera/image";
    ch2.encoding = "protobuf";
    ch2.schema_name = "sensor_msgs/Image";
    ch2.schema_encoding = "protobuf";
    ch2.schema_data = "binary_schema_placeholder";
    channels.push_back(ch2);

    auto result = foxglove::encode_advertise(channels);
    if (result.has_value()) {
      std::cout << "Encoded Advertise JSON:\n" << result.value() << "\n\n";
    } else {
      std::cout << "Failed to encode Advertise: "
                << foxglove::foxglove_error_string(result.error()) << "\n\n";
    }
  }

  // Example 3: Encode/decode MessageData binary frame and print hex dump
  std::cout << "=== Example 3: MessageData Binary Roundtrip ===\n";
  {
    foxglove::MessageData msg;
    msg.subscription_id = 42;
    msg.log_time = 1234567890123ULL;
    msg.data = {0x7B, 0x22, 0x78, 0x22, 0x3A, 0x20, 0x31, 0x2E, 0x30, 0x7D};  // {"x": 1.0}

    std::cout << "Original MessageData:\n";
    std::cout << "  subscription_id: " << msg.subscription_id << "\n";
    std::cout << "  log_time: " << msg.log_time << "\n";
    std::cout << "  data size: " << msg.data.size() << " bytes\n\n";

    // Encode
    auto encoded = foxglove::encode_message_data(msg);
    if (encoded.has_value()) {
      std::cout << "Encoded binary frame:\n";
      print_hex_dump(encoded.value());
      std::cout << "\n";

      // Decode
      auto decoded = foxglove::decode_message_data_binary(encoded.value());
      if (decoded.has_value()) {
        std::cout << "Decoded MessageData:\n";
        std::cout << "  subscription_id: " << decoded.value().subscription_id
                  << "\n";
        std::cout << "  log_time: " << decoded.value().log_time << "\n";
        std::cout << "  data size: " << decoded.value().data.size()
                  << " bytes\n";
        std::cout << "  data matches: "
                  << (decoded.value().data == msg.data ? "yes" : "no")
                  << "\n\n";
      } else {
        std::cout << "Failed to decode MessageData: "
                  << foxglove::foxglove_error_string(decoded.error())
                  << "\n\n";
      }
    } else {
      std::cout << "Failed to encode MessageData: "
                << foxglove::foxglove_error_string(encoded.error()) << "\n\n";
    }
  }

  // Example 4: Decode client messages
  std::cout << "=== Example 4: Decode Client Messages ===\n";
  {
    // Subscribe message
    std::string subscribe_json = R"({
      "op": "subscribe",
      "subscriptions": [
        {"id": 0, "channelId": 1},
        {"id": 1, "channelId": 2}
      ]
    })";

    std::cout << "Parsing Subscribe JSON...\n";
    auto result = foxglove::decode_client_message(subscribe_json);
    if (result.has_value()) {
      if (auto* subscribe = std::get_if<foxglove::Subscribe>(&result.value())) {
        std::cout << "Parsed Subscribe message:\n";
        std::cout << "  Number of subscriptions: "
                  << subscribe->subscriptions.size() << "\n";
        for (const auto& sub : subscribe->subscriptions) {
          std::cout << "    subscription_id=" << sub.subscription_id
                    << ", channel_id=" << sub.channel_id << "\n";
        }
      }
    } else {
      std::cout << "Failed to parse: "
                << foxglove::foxglove_error_string(result.error()) << "\n";
    }

    // Unsubscribe message
    std::string unsubscribe_json =
        R"({"op": "unsubscribe", "subscriptionIds": [0, 1]})";

    std::cout << "\nParsing Unsubscribe JSON...\n";
    result = foxglove::decode_client_message(unsubscribe_json);
    if (result.has_value()) {
      if (auto* unsubscribe =
              std::get_if<foxglove::Unsubscribe>(&result.value())) {
        std::cout << "Parsed Unsubscribe message:\n";
        std::cout << "  Subscription IDs to remove: ";
        for (size_t i = 0; i < unsubscribe->subscription_ids.size(); ++i) {
          if (i > 0) std::cout << ", ";
          std::cout << unsubscribe->subscription_ids[i];
        }
        std::cout << "\n";
      }
    } else {
      std::cout << "Failed to parse: "
                << foxglove::foxglove_error_string(result.error()) << "\n";
    }
  }

  std::cout << "\nAll examples completed successfully!\n";
  return 0;
}
