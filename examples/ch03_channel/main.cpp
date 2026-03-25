/// @brief Chapter 3 Example: Channel and Schema Abstraction
///
/// This example demonstrates:
/// - Creating a Schema for message validation
/// - Creating a RawChannel with a callback
/// - Logging messages through the channel
/// - Closing the channel

#include <foxglove/channel.hpp>
#include <foxglove/schema.hpp>

#include <cstdio>
#include <string>
#include <vector>

int main() {
  std::printf("Chapter 3: Channel and Schema Abstraction\n");
  std::printf("=========================================\n\n");

  // Define a simple JSON schema for our messages
  std::string schema_json = R"({
    "type": "object",
    "properties": {
      "timestamp": {"type": "number"},
      "value": {"type": "number"}
    }
  })";
  std::vector<uint8_t> schema_data(schema_json.begin(), schema_json.end());

  // Create the schema
  foxglove::Schema sensor_schema{"SensorData", "jsonschema", schema_data};
  std::printf("Created schema: %s (encoding: %s, %zu bytes)\n", sensor_schema.name.c_str(),
              sensor_schema.encoding.c_str(), schema_data.size());

  // Create a callback that prints received messages
  foxglove::MessageCallback print_callback = [](uint32_t channel_id, const uint8_t* data,
                                                size_t len, uint64_t log_time) {
    std::printf("[Channel %u] Received %llu bytes at time %llu: ", channel_id,
                static_cast<unsigned long long>(len),
                static_cast<unsigned long long>(log_time));
    // Print first few bytes as hex
    for (size_t i = 0; i < len && i < 16; ++i) {
      std::printf("%02x ", data[i]);
    }
    if (len > 16) {
      std::printf("...");
    }
    std::printf("\n");
  };

  // Create a channel
  auto result =
      foxglove::RawChannel::create("/sensor/data", "json", sensor_schema, print_callback);

  if (!result.has_value()) {
    std::printf("Failed to create channel\n");
    return 1;
  }

  auto channel = std::move(result.value());
  std::printf("Created channel: ID=%u, topic=%s\n", channel.id(),
              channel.descriptor().topic.c_str());

  // Log some messages
  std::printf("\nLogging messages...\n");

  for (int i = 0; i < 5; ++i) {
    std::string message = "{\"timestamp\":" + std::to_string(i) + ",\"value\":" +
                          std::to_string(i * 10.0) + "}";
    std::vector<uint8_t> msg_data(message.begin(), message.end());

    uint64_t log_time = 1000000000ULL + i * 1000000ULL;  // Simulated timestamps
    channel.log(msg_data.data(), msg_data.size(), log_time);
  }

  // Demonstrate callback rebinding
  std::printf("\nRebinding callback to silence output...\n");
  channel.set_callback(nullptr);

  // This log will not be delivered (no crash)
  std::string silent_msg = "{\"silent\":true}";
  std::vector<uint8_t> silent_data(silent_msg.begin(), silent_msg.end());
  channel.log(silent_data.data(), silent_data.size(), 2000000000ULL);
  std::printf("Logged message with nullptr callback (no crash, no output)\n");

  // Rebind to a new callback
  std::printf("\nRebinding to a new callback...\n");
  int message_count = 0;
  foxglove::MessageCallback counting_callback =
      [&message_count](uint32_t, const uint8_t*, size_t, uint64_t) { ++message_count; };
  channel.set_callback(counting_callback);

  channel.log(silent_data.data(), silent_data.size(), 3000000000ULL);
  std::printf("Logged 1 message, callback count = %d\n", message_count);

  // Close the channel
  std::printf("\nClosing channel...\n");
  channel.close();

  // Try to log after close - should be no-op
  channel.log(silent_data.data(), silent_data.size(), 4000000000ULL);
  std::printf("Logged after close (no-op), callback count still = %d\n", message_count);

  std::printf("\nExample complete!\n");
  return 0;
}
