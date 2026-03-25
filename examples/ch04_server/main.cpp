/// @brief Chapter 4 Example: WebSocket Server
///
/// This example demonstrates:
/// - Creating a WebSocket server
/// - Creating a channel and adding it to the server
/// - Publishing messages to connected clients
/// - Graceful shutdown

#include <foxglove/channel.hpp>
#include <foxglove/schema.hpp>
#include <foxglove/server.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

int main() {
  std::printf("Chapter 4: WebSocket Server\n");
  std::printf("===========================\n\n");

  // Create server options
  foxglove::WebSocketServerOptions options;
  options.host = "0.0.0.0";
  options.port = 8765;
  options.name = "Chapter4ExampleServer";
  options.capabilities = 0;

  // Create the server
  auto server_result = foxglove::WebSocketServer::create(options);
  if (!server_result.has_value()) {
    std::printf("Failed to create server\n");
    return 1;
  }
  auto server = std::move(server_result.value());

  std::printf("Server running on ws://%s:%d\n", options.host.c_str(), options.port);
  std::printf("Connect with Foxglove Studio to see the data.\n\n");

  // Create a schema for counter messages
  std::string schema_json = R"({
    "type": "object",
    "properties": {
      "counter": {"type": "integer"},
      "timestamp": {"type": "integer"}
    }
  })";
  std::vector<uint8_t> schema_data(schema_json.begin(), schema_json.end());
  foxglove::Schema counter_schema{"CounterSchema", "jsonschema", schema_data};

  // Create a channel
  auto channel_result =
      foxglove::RawChannel::create("/counter", "json", counter_schema);
  if (!channel_result.has_value()) {
    std::printf("Failed to create channel\n");
    return 1;
  }
  auto channel = std::move(channel_result.value());

  std::printf("Created channel: ID=%u, topic=%s\n", channel.id(),
              channel.descriptor().topic.c_str());

  // Add the channel to the server
  server.add_channel(channel);
  std::printf("Added channel to server\n\n");

  // Publish incrementing counter messages for 30 seconds
  std::printf("Publishing counter messages every 100ms for 30 seconds...\n");
  std::printf("Press Ctrl+C to stop early\n\n");

  auto start_time = std::chrono::steady_clock::now();
  int counter = 0;

  while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
    // Build JSON message
    uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    std::string message = "{\"counter\":" + std::to_string(counter) +
                          ",\"timestamp\":" + std::to_string(timestamp) + "}";
    std::vector<uint8_t> msg_data(message.begin(), message.end());

    // Log the message
    channel.log(msg_data.data(), msg_data.size(), timestamp);

    if (counter % 10 == 0) {
      std::printf("Published counter=%d\n", counter);
    }

    ++counter;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::printf("\nShutting down server...\n");
  server.shutdown();
  std::printf("Server shut down gracefully.\n");
  std::printf("\nExample complete!\n");

  return 0;
}
