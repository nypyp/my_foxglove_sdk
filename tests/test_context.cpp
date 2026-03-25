/// @brief Unit tests for Foxglove Context and Sink routing.

#include <foxglove/context.hpp>
#include <foxglove/schema.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

using namespace foxglove;

// MockSink for testing - tracks all calls for assertions
class MockSink : public Sink {
 public:
  struct ChannelAddedCall {
    RawChannel channel;
  };

  struct ChannelRemovedCall {
    uint32_t channel_id;
  };

  struct MessageCall {
    uint32_t channel_id;
    std::vector<uint8_t> data;
    uint64_t log_time;
  };

  mutable std::mutex mutex_;
  std::vector<ChannelDescriptor> channels_added_;
  std::vector<uint32_t> channels_removed_;
  std::vector<MessageCall> messages_received_;

  void on_channel_added(RawChannel& channel) override {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_added_.push_back(channel.descriptor());
  }

  void on_channel_removed(uint32_t channel_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_removed_.push_back(channel_id);
  }

  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_received_.push_back({channel_id, std::vector<uint8_t>(data, data + len), log_time});
  }

  // Helper to clear all tracked calls
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_added_.clear();
    channels_removed_.clear();
    messages_received_.clear();
  }

  // Helper to get message count
  size_t message_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_received_.size();
  }
};

TEST_CASE("Context - routes message to single sink") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink = std::make_shared<MockSink>();
  context.add_sink(sink);

  // Create a channel through the context
  auto channel_result =
      context.create_channel("/test/topic", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  // Log a message
  std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04};
  channel.log(test_data.data(), test_data.size(), 1234567890ULL);

  // Verify sink received the message
  REQUIRE(sink->message_count() == 1);
  {
    std::lock_guard<std::mutex> lock(sink->mutex_);
    REQUIRE(sink->messages_received_[0].channel_id == channel.id());
    REQUIRE(sink->messages_received_[0].data == test_data);
    REQUIRE(sink->messages_received_[0].log_time == 1234567890ULL);
  }

  // Verify sink was notified of channel addition
  REQUIRE(sink->channels_added_.size() == 1);
  REQUIRE(sink->channels_added_[0].topic == "/test/topic");
}

TEST_CASE("Context - routes to multiple sinks") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink_a = std::make_shared<MockSink>();
  auto sink_b = std::make_shared<MockSink>();

  context.add_sink(sink_a);
  context.add_sink(sink_b);

  // Create a channel and log a message
  auto channel_result =
      context.create_channel("/test/topic", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  std::vector<uint8_t> test_data = {0xAA, 0xBB, 0xCC};
  channel.log(test_data.data(), test_data.size(), 9876543210ULL);

  // Both sinks should receive the same message
  REQUIRE(sink_a->message_count() == 1);
  REQUIRE(sink_b->message_count() == 1);

  {
    std::lock_guard<std::mutex> lock(sink_a->mutex_);
    REQUIRE(sink_a->messages_received_[0].data == test_data);
  }
  {
    std::lock_guard<std::mutex> lock(sink_b->mutex_);
    REQUIRE(sink_b->messages_received_[0].data == test_data);
  }
}

TEST_CASE("Context - channel filter") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink_a = std::make_shared<MockSink>();
  auto sink_b = std::make_shared<MockSink>();

  // Create two channels first to capture their IDs
  auto result1 =
      context.create_channel("/topic1", "json", Schema{"Schema1", "jsonschema", {}});
  REQUIRE(result1.has_value());
  auto channel1 = std::move(result1.value());

  auto result2 =
      context.create_channel("/topic2", "json", Schema{"Schema2", "jsonschema", {}});
  REQUIRE(result2.has_value());
  auto channel2 = std::move(result2.value());

  uint32_t ch1_id = channel1.id();

  // Sink A receives all channels (no filter)
  context.add_sink(sink_a);

  // Sink B filters out channel1 specifically
  context.add_sink(sink_b, [ch1_id](uint32_t channel_id) { return channel_id != ch1_id; });

  // Log to channel 1 - only sink_a should receive
  std::vector<uint8_t> data1 = {0x01};
  channel1.log(data1.data(), data1.size(), 1ULL);

  REQUIRE(sink_a->message_count() == 1);
  REQUIRE(sink_b->message_count() == 0);  // Filtered out

  // Log to channel 2 - both sinks should receive
  std::vector<uint8_t> data2 = {0x02};
  channel2.log(data2.data(), data2.size(), 2ULL);

  REQUIRE(sink_a->message_count() == 2);
  REQUIRE(sink_b->message_count() == 1);  // Now received
}

TEST_CASE("Context - add/remove sink lifecycle") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink = std::make_shared<MockSink>();

  // Add sink and get its ID
  uint32_t sink_id = context.add_sink(sink);

  // Create channel and log - sink should receive
  auto result = context.create_channel("/test", "json", Schema{"Schema", "jsonschema", {}});
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> data = {0x01};
  channel.log(data.data(), data.size(), 1ULL);
  REQUIRE(sink->message_count() == 1);

  // Remove the sink
  context.remove_sink(sink_id);
  sink->clear();

  // Log again - removed sink should NOT receive
  channel.log(data.data(), data.size(), 2ULL);
  REQUIRE(sink->message_count() == 0);
}

TEST_CASE("Context - default context singleton") {
  // Get the default context twice
  Context& ctx1 = Context::default_context();
  Context& ctx2 = Context::default_context();

  // Should be the same instance
  REQUIRE(&ctx1 == &ctx2);

  // Create a separate context - should be different
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto ctx3 = std::move(ctx_result.value());

  // ctx3 is a different instance
  REQUIRE(&ctx1 != &ctx3);
}

TEST_CASE("Context - create_channel returns valid channel") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto result = context.create_channel("/robot/pose", "json",
                                       Schema{"geometry_msgs/Pose", "jsonschema", {}});
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  REQUIRE(channel.id() >= 1);
  REQUIRE(channel.descriptor().topic == "/robot/pose");
  REQUIRE(channel.descriptor().encoding == "json");
  REQUIRE(channel.descriptor().schema.name == "geometry_msgs/Pose");
}

TEST_CASE("Context - multiple channels independent routing") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink = std::make_shared<MockSink>();
  context.add_sink(sink);

  // Create two channels
  auto result1 = context.create_channel("/topic1", "json", Schema{"S1", "jsonschema", {}});
  auto result2 = context.create_channel("/topic2", "json", Schema{"S2", "jsonschema", {}});
  REQUIRE(result1.has_value());
  REQUIRE(result2.has_value());
  auto channel1 = std::move(result1.value());
  auto channel2 = std::move(result2.value());

  // Log to channel 1 only
  std::vector<uint8_t> data1 = {0x01};
  channel1.log(data1.data(), data1.size(), 100ULL);

  REQUIRE(sink->message_count() == 1);
  {
    std::lock_guard<std::mutex> lock(sink->mutex_);
    REQUIRE(sink->messages_received_[0].channel_id == channel1.id());
  }

  // Log to channel 2 only
  std::vector<uint8_t> data2 = {0x02};
  channel2.log(data2.data(), data2.size(), 200ULL);

  REQUIRE(sink->message_count() == 2);
  {
    std::lock_guard<std::mutex> lock(sink->mutex_);
    REQUIRE(sink->messages_received_[1].channel_id == channel2.id());
  }
}
