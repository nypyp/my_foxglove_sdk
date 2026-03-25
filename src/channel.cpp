#include <foxglove/channel.hpp>

namespace foxglove {

// Static atomic counter for unique channel IDs, starting from 1
std::atomic<uint32_t> RawChannel::next_channel_id_{1};

RawChannel::RawChannel(uint32_t id, ChannelDescriptor descriptor, MessageCallback callback)
    : id_(id),
      descriptor_(std::move(descriptor)),
      callback_(std::move(callback)),
      mutex_(),
      closed_(false) {}

FoxgloveResult<RawChannel> RawChannel::create(const std::string& topic,
                                              const std::string& encoding, Schema schema,
                                              MessageCallback callback) {
  // Allocate unique ID atomically
  uint32_t id = next_channel_id_.fetch_add(1, std::memory_order_relaxed);

  // Construct the channel descriptor
  ChannelDescriptor descriptor{id, topic, encoding, std::move(schema)};

  // Construct and return the channel
  return RawChannel{id, std::move(descriptor), std::move(callback)};
}

void RawChannel::log(const uint8_t* data, size_t len, uint64_t log_time) {
  std::lock_guard<std::mutex> lock(mutex_);

  // If closed or no callback, this is a no-op
  if (closed_ || !callback_) {
    return;
  }

  // Invoke callback with channel ID, data, length, and log time
  callback_(id_, data, len, log_time);
}

void RawChannel::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  closed_ = true;
}

void RawChannel::set_callback(MessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
}

// Move constructor
RawChannel::RawChannel(RawChannel&& other) noexcept
    : id_(other.id_),
      descriptor_(std::move(other.descriptor_)),
      callback_(std::move(other.callback_)),
      mutex_(),
      closed_(other.closed_) {
  // Note: mutex_ is default-constructed, not moved
  // This is safe because the moved-from channel is in a valid but unspecified state
}

// Move assignment
RawChannel& RawChannel::operator=(RawChannel&& other) noexcept {
  if (this != &other) {
    // Need to lock both mutexes to avoid deadlock
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> lock(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> other_lock(other.mutex_, std::adopt_lock);

    id_ = other.id_;
    descriptor_ = std::move(other.descriptor_);
    callback_ = std::move(other.callback_);
    closed_ = other.closed_;
  }
  return *this;
}

}  // namespace foxglove
