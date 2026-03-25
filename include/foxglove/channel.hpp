#pragma once

#include <foxglove/error.hpp>
#include <foxglove/schema.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace foxglove {

/// @brief Callback type for message logging.
///
/// Called when a message is logged to a channel. The callback receives
/// the channel ID, message data pointer, data length, and log timestamp.
using MessageCallback =
    std::function<void(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time)>;

/// @brief A raw (untyped) channel for publishing messages.
///
/// Channels are thread-safe for concurrent logging. The channel uses a
/// callback-based output model where logged messages are dispatched through
/// a callback function that can be set at construction or rebound later.
///
/// Key design: The callback can be rebound via set_callback(), allowing the
/// server or context to wire its dispatch logic without tight coupling.
class RawChannel final {
 public:
  /// @brief Create a new channel.
  ///
  /// @param topic The topic name for this channel.
  /// @param encoding The message encoding (e.g., "json", "protobuf").
  /// @param schema The schema descriptor for messages on this channel.
  /// @param callback Optional callback for message dispatch.
  /// @return Result containing the channel on success, error on failure.
  static FoxgloveResult<RawChannel> create(const std::string& topic, const std::string& encoding,
                                           Schema schema,
                                           MessageCallback callback = nullptr);

  /// @brief Get the unique channel ID.
  [[nodiscard]] uint32_t id() const noexcept { return id_; }

  /// @brief Get the channel descriptor.
  [[nodiscard]] const ChannelDescriptor& descriptor() const noexcept { return descriptor_; }

  /// @brief Log a message to the channel.
  ///
  /// Thread-safe. If the channel is closed or has no callback, the call is a no-op.
  ///
  /// @param data Pointer to message data.
  /// @param len Length of message data in bytes.
  /// @param log_time Timestamp in nanoseconds since epoch.
  void log(const uint8_t* data, size_t len, uint64_t log_time);

  /// @brief Close the channel.
  ///
  /// After closing, further log() calls will be no-ops.
  void close();

  /// @brief Rebind the message callback.
  ///
  /// Thread-safe. Allows the server/context to wire dispatch after construction.
  /// Setting to nullptr silences output.
  ///
  /// @param callback New callback function, or nullptr.
  void set_callback(MessageCallback callback);

  // Move-only semantics
  RawChannel(RawChannel&& other) noexcept;
  RawChannel& operator=(RawChannel&& other) noexcept;
  RawChannel(const RawChannel&) = delete;
  RawChannel& operator=(const RawChannel&) = delete;

  /// @brief Destructor.
  ~RawChannel() = default;

 private:
  explicit RawChannel(uint32_t id, ChannelDescriptor descriptor, MessageCallback callback);

  uint32_t id_;
  ChannelDescriptor descriptor_;
  MessageCallback callback_;
  mutable std::mutex mutex_;
  bool closed_ = false;

  // Static atomic counter for unique channel IDs
  static std::atomic<uint32_t> next_channel_id_;
};

}  // namespace foxglove
