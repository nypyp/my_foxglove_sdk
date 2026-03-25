#pragma once

#include <foxglove/channel.hpp>
#include <foxglove/error.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace foxglove {

// Forward declaration
class RawChannel;

/// @brief Callback for channel subscription events.
using SubscribeCallback = std::function<void(uint32_t channel_id, uint32_t subscription_id)>;

/// @brief Callback for channel unsubscription events.
using UnsubscribeCallback = std::function<void(uint32_t subscription_id)>;

/// @brief WebSocket server callbacks.
struct WebSocketServerCallbacks {
  /// @brief Called when a client subscribes to a channel.
  SubscribeCallback on_subscribe;
  /// @brief Called when a client unsubscribes from a channel.
  UnsubscribeCallback on_unsubscribe;
};

/// @brief WebSocket server configuration options.
struct WebSocketServerOptions {
  /// @brief Host address to bind to.
  std::string host = "0.0.0.0";
  /// @brief Port to listen on.
  uint16_t port = 8765;
  /// @brief Server name (sent in serverInfo).
  std::string name;
  /// @brief Server capability bitmask.
  uint32_t capabilities = 0;
  /// @brief Server event callbacks.
  WebSocketServerCallbacks callbacks;
};

/// @brief WebSocket server for Foxglove protocol.
///
/// Implements the Foxglove WebSocket protocol, handling client connections,
/// channel advertisements, subscriptions, and message delivery.
/// Uses PIMPL pattern to hide libwebsockets implementation details.
class WebSocketServer final {
 public:
  /// @brief Create a new WebSocket server.
  ///
  /// @param options Server configuration options.
  /// @return Result containing the server on success, error on failure.
  static FoxgloveResult<WebSocketServer> create(WebSocketServerOptions options);

  /// @brief Add a channel to the server.
  ///
  /// The channel will be advertised to all connected clients.
  /// The server stores a pointer to the channel and wires its
  /// callback to dispatch messages to WebSocket subscribers.
  ///
  /// @param channel The channel to add.
  void add_channel(RawChannel& channel);

  /// @brief Remove a channel from the server.
  ///
  /// The channel will be unadvertised to all connected clients.
  ///
  /// @param channel_id The ID of the channel to remove.
  void remove_channel(uint32_t channel_id);

  /// @brief Broadcast time message to all connected clients.
  ///
  /// @param timestamp Timestamp in nanoseconds.
  void broadcast_time(uint64_t timestamp);

  /// @brief Gracefully shut down the server.
  void shutdown();

  // Move-only semantics
  WebSocketServer(WebSocketServer&& other) noexcept;
  WebSocketServer& operator=(WebSocketServer&& other) noexcept;
  WebSocketServer(const WebSocketServer&) = delete;
  WebSocketServer& operator=(const WebSocketServer&) = delete;

  /// @brief Destructor.
  ~WebSocketServer();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  explicit WebSocketServer(std::unique_ptr<Impl> impl);
};

}  // namespace foxglove
