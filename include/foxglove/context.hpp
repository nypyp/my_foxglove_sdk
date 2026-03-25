#pragma once

#include <foxglove/channel.hpp>
#include <foxglove/error.hpp>
#include <foxglove/schema.hpp>
#include <foxglove/server.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace foxglove {

// Forward declarations
class Context;

/// @brief Sink interface for receiving channel and message events.
///
/// A Sink is any consumer of channel data: WebSocket server, MCAP writer,
/// custom logger, etc. The Context routes all channel events to registered sinks.
class Sink {
 public:
  virtual ~Sink() = default;

  /// @brief Called when a channel is added to the context.
  ///
  /// @param channel The channel that was added. Non-const because sinks
  ///                may need to wire callbacks (e.g., WebSocketServer::add_channel).
  virtual void on_channel_added(RawChannel& channel) = 0;

  /// @brief Called when a channel is removed from the context.
  ///
  /// @param channel_id The ID of the channel that was removed.
  virtual void on_channel_removed(uint32_t channel_id) = 0;

  /// @brief Called when a message is logged on a channel.
  ///
  /// @param channel_id The channel ID the message was logged on.
  /// @param data Pointer to message data.
  /// @param len Length of message data in bytes.
  /// @param log_time Timestamp in nanoseconds since epoch.
  virtual void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                          uint64_t log_time) = 0;
};

/// @brief Channel filter function type.
///
/// Returns true if the sink should receive messages from the given channel.
using ChannelFilter = std::function<bool(uint32_t channel_id)>;

/// @brief Context manages channels and routes messages to registered sinks.
///
/// The Context is the central routing hub in the Foxglove SDK.
/// - Channels are created on and owned by a Context
/// - Messages logged to channels are dispatched to all registered Sinks
/// - Sinks can optionally filter which channels they receive
///
/// Thread-safety: All public methods are thread-safe.
class Context final {
 public:
  /// @brief Create a new Context.
  ///
  /// @return Result containing the context on success, error on failure.
  static FoxgloveResult<Context> create();

  /// @brief Get or create the default global context singleton.
  ///
  /// Thread-safe. The singleton is created on first call.
  ///
  /// @return Reference to the default context.
  static Context& default_context();

  /// @brief Add a sink to this context.
  ///
  /// The sink will receive all channel events and messages (subject to
  /// the optional channel filter). Sinks are held by shared_ptr to ensure
  /// they stay alive while referenced by the context.
  ///
  /// @param sink The sink to add.
  /// @param channel_filter Optional filter function. If provided, only channels
  ///                       passing the filter will have their messages routed
  ///                       to this sink. If nullptr, all channels are routed.
  /// @return A sink ID that can be used to remove the sink later.
  uint32_t add_sink(std::shared_ptr<Sink> sink, ChannelFilter channel_filter = nullptr);

  /// @brief Remove a sink by its ID.
  ///
  /// The sink will no longer receive channel events or messages.
  /// If the sink ID is not found, this is a no-op.
  ///
  /// @param sink_id The ID returned by add_sink().
  void remove_sink(uint32_t sink_id);

  /// @brief Create a channel on this context.
  ///
  /// The channel's callback is automatically wired to dispatch through
  /// this context. All registered sinks are notified of the new channel.
  ///
  /// @param topic The topic name for this channel.
  /// @param encoding The message encoding (e.g., "json", "protobuf").
  /// @param schema The schema descriptor for messages on this channel.
  /// @return Result containing the channel on success, error on failure.
  FoxgloveResult<RawChannel> create_channel(const std::string& topic, const std::string& encoding,
                                            Schema schema);

  /// @brief Remove a channel from this context.
  ///
  /// All registered sinks are notified of the removal.
  /// If the channel ID is not found, this is a no-op.
  ///
  /// @param channel_id The ID of the channel to remove.
  void remove_channel(uint32_t channel_id);

  // Move-only semantics
  Context(Context&& other) noexcept;
  Context& operator=(Context&& other) noexcept;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  /// @brief Destructor.
  ~Context() = default;

 private:
  /// @brief Internal sink registration info.
  struct SinkInfo {
    std::shared_ptr<Sink> sink;
    ChannelFilter filter;
  };

  explicit Context(std::unordered_map<uint32_t, ChannelDescriptor> channel_descriptors);

  void dispatch_message(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time);

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, ChannelDescriptor> channel_descriptors_;
  std::unordered_map<uint32_t, SinkInfo> sinks_;

  // Static atomic counter for unique sink IDs
  static std::atomic<uint32_t> next_sink_id_;
};

/// @brief Sink adapter that wraps a WebSocketServer.
///
/// This adapter bridges the Context/Sink model with the existing WebSocketServer.
/// When Context dispatches a message, the adapter forwards it to the WebSocket
/// server for delivery to subscribers.
class WebSocketServerSink : public Sink {
 public:
  /// @brief Create a WebSocketServerSink wrapping the given server.
  ///
  /// @param server The WebSocketServer to forward messages to.
  explicit WebSocketServerSink(WebSocketServer& server);

  void on_channel_added(RawChannel& channel) override;
  void on_channel_removed(uint32_t channel_id) override;
  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override;

 private:
  WebSocketServer& server_;
};

}  // namespace foxglove
