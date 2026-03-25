#include <foxglove/context.hpp>
#include <foxglove/schema.hpp>

#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

using namespace foxglove;

class PrintSink : public Sink {
 public:
  void on_channel_added(RawChannel& channel) override {
    printf("[PrintSink] Channel added: %s (ID: %u)\n", channel.descriptor().topic.c_str(),
           channel.id());
  }

  void on_channel_removed(uint32_t channel_id) override {
    printf("[PrintSink] Channel removed: ID %u\n", channel_id);
  }

  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override {
    printf("[PrintSink] Message on channel %u: %zu bytes at time %llu\n", channel_id, len,
           static_cast<unsigned long long>(log_time));
  }
};

class CounterSink : public Sink {
 public:
  std::mutex mutex_;
  int message_count_ = 0;

  void on_channel_added(RawChannel&) override {}

  void on_channel_removed(uint32_t) override {}

  void on_message(uint32_t, const uint8_t*, size_t, uint64_t) override {
    std::lock_guard<std::mutex> lock(mutex_);
    message_count_++;
  }
};

int main() {
  printf("Chapter 6: Context and Sink Routing Demo\n");
  printf("=========================================\n\n");

  auto ctx_result = Context::create();
  if (!ctx_result.has_value()) {
    printf("Failed to create context\n");
    return 1;
  }
  auto context = std::move(ctx_result.value());

  auto print_sink = std::make_shared<PrintSink>();
  auto counter_sink = std::make_shared<CounterSink>();

  context.add_sink(print_sink);
  context.add_sink(counter_sink);

  printf("Created context with 2 sinks\n\n");

  auto result1 = context.create_channel("/robot/pose", "json",
                                        Schema{"geometry_msgs/Pose", "jsonschema", {}});
  if (!result1.has_value()) {
    printf("Failed to create channel 1\n");
    return 1;
  }
  auto pose_channel = std::move(result1.value());

  auto result2 = context.create_channel("/robot/velocity", "json",
                                        Schema{"geometry_msgs/Twist", "jsonschema", {}});
  if (!result2.has_value()) {
    printf("Failed to create channel 2\n");
    return 1;
  }
  auto velocity_channel = std::move(result2.value());

  printf("\nLogging messages...\n");

  std::vector<uint8_t> pose_data = {0x7B, 0x7D};
  std::vector<uint8_t> vel_data = {0x5B, 0x5D};

  pose_channel.log(pose_data.data(), pose_data.size(), 1000000000ULL);
  velocity_channel.log(vel_data.data(), vel_data.size(), 2000000000ULL);
  pose_channel.log(pose_data.data(), pose_data.size(), 3000000000ULL);

  printf("\nCounter sink received %d messages\n", counter_sink->message_count_);

  printf("\nDemonstrating default context singleton:\n");
  Context& default_ctx1 = Context::default_context();
  Context& default_ctx2 = Context::default_context();
  printf("Same instance? %s\n", (&default_ctx1 == &default_ctx2) ? "YES" : "NO");

  printf("\nChapter 6 demo complete!\n");
  return 0;
}
