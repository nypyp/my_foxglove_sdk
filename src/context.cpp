#include <foxglove/context.hpp>

namespace foxglove {

std::atomic<uint32_t> Context::next_sink_id_{1};

Context::Context(std::unordered_map<uint32_t, ChannelDescriptor> channel_descriptors)
    : channel_descriptors_(std::move(channel_descriptors)) {}

FoxgloveResult<Context> Context::create() {
  return Context{std::unordered_map<uint32_t, ChannelDescriptor>{}};
}

Context& Context::default_context() {
  static Context instance = []() {
    auto result = create();
    return std::move(result.value());
  }();
  return instance;
}

uint32_t Context::add_sink(std::shared_ptr<Sink> sink, ChannelFilter channel_filter) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint32_t sink_id = next_sink_id_.fetch_add(1, std::memory_order_relaxed);
  sinks_[sink_id] = SinkInfo{std::move(sink), std::move(channel_filter)};

  return sink_id;
}

void Context::remove_sink(uint32_t sink_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  sinks_.erase(sink_id);
}

FoxgloveResult<RawChannel> Context::create_channel(const std::string& topic,
                                                   const std::string& encoding, Schema schema) {
  auto channel_result = RawChannel::create(topic, encoding, schema, nullptr);
  if (!channel_result.has_value()) {
    return channel_result;
  }

  auto channel = std::move(channel_result.value());
  uint32_t channel_id = channel.id();

  MessageCallback callback = [this, channel_id](uint32_t, const uint8_t* data, size_t len,
                                                uint64_t log_time) {
    this->dispatch_message(channel_id, data, len, log_time);
  };
  channel.set_callback(std::move(callback));

  std::vector<std::shared_ptr<Sink>> notifiable_sinks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_descriptors_.insert_or_assign(channel_id, channel.descriptor());

    for (auto& [sink_id, info] : sinks_) {
      if (!info.filter || info.filter(channel_id)) {
        notifiable_sinks.push_back(info.sink);
      }
    }
  }

  for (auto& sink : notifiable_sinks) {
    sink->on_channel_added(channel);
  }

  return channel;
}

void Context::remove_channel(uint32_t channel_id) {
  std::vector<std::shared_ptr<Sink>> notifiable_sinks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_descriptors_.erase(channel_id);

    for (auto& [sink_id, info] : sinks_) {
      notifiable_sinks.push_back(info.sink);
    }
  }

  for (auto& sink : notifiable_sinks) {
    sink->on_channel_removed(channel_id);
  }
}

void Context::dispatch_message(uint32_t channel_id, const uint8_t* data, size_t len,
                               uint64_t log_time) {
  std::vector<std::shared_ptr<Sink>> active_sinks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [sink_id, info] : sinks_) {
      if (info.filter && !info.filter(channel_id)) {
        continue;
      }
      active_sinks.push_back(info.sink);
    }
  }

  for (auto& sink : active_sinks) {
    sink->on_message(channel_id, data, len, log_time);
  }
}

Context::Context(Context&& other) noexcept
    : channel_descriptors_(std::move(other.channel_descriptors_)),
      sinks_(std::move(other.sinks_)) {}

Context& Context::operator=(Context&& other) noexcept {
  if (this != &other) {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_descriptors_ = std::move(other.channel_descriptors_);
    sinks_ = std::move(other.sinks_);
  }
  return *this;
}

WebSocketServerSink::WebSocketServerSink(WebSocketServer& server) : server_(server) {}

void WebSocketServerSink::on_channel_added(RawChannel& channel) {
  server_.add_channel(channel);
}

void WebSocketServerSink::on_channel_removed(uint32_t channel_id) {
  server_.remove_channel(channel_id);
}

// WebSocket message delivery is handled by channel callbacks wired in
// WebSocketServer::add_channel(), not through the Sink dispatch path.
void WebSocketServerSink::on_message(uint32_t /*channel_id*/, const uint8_t* /*data*/,
                                     size_t /*len*/, uint64_t /*log_time*/) {}

}  // namespace foxglove
