/// @brief Unit tests for Foxglove Channel and Schema abstraction.

#include <foxglove/channel.hpp>
#include <foxglove/schema.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace foxglove;

TEST_CASE("Channel - assigns unique sequential IDs") {
  // Create 3 channels → IDs should be 1, 2, 3
  auto result1 = RawChannel::create("/topic1", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result1.has_value());
  auto channel1 = std::move(result1.value());
  REQUIRE(channel1.id() == 1);

  auto result2 = RawChannel::create("/topic2", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result2.has_value());
  auto channel2 = std::move(result2.value());
  REQUIRE(channel2.id() == 2);

  auto result3 = RawChannel::create("/topic3", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result3.has_value());
  auto channel3 = std::move(result3.value());
  REQUIRE(channel3.id() == 3);
}

TEST_CASE("Channel - log invokes callback with correct data") {
  std::vector<uint8_t> received_data;
  uint32_t received_channel_id = 0;
  uint64_t received_log_time = 0;
  bool callback_invoked = false;

  MessageCallback callback = [&](uint32_t channel_id, const uint8_t* data, size_t len,
                                 uint64_t log_time) {
    received_channel_id = channel_id;
    received_data.assign(data, data + len);
    received_log_time = log_time;
    callback_invoked = true;
  };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
  channel.log(test_data.data(), test_data.size(), 1234567890ULL);

  REQUIRE(callback_invoked);
  REQUIRE(received_channel_id == channel.id());
  REQUIRE(received_data == test_data);
  REQUIRE(received_log_time == 1234567890ULL);
}

TEST_CASE("Channel - close prevents further logging") {
  int callback_count = 0;
  MessageCallback callback = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> test_data = {0x01, 0x02};

  // Log before close - should invoke callback
  channel.log(test_data.data(), test_data.size(), 1ULL);
  REQUIRE(callback_count == 1);

  // Close the channel
  channel.close();

  // Log after close - should NOT invoke callback
  channel.log(test_data.data(), test_data.size(), 2ULL);
  REQUIRE(callback_count == 1);  // Count should not increase
}

TEST_CASE("Channel - concurrent log from multiple threads") {
  constexpr int num_threads = 4;
  constexpr int messages_per_thread = 100;

  std::atomic<int> callback_count{0};
  std::mutex received_mutex;
  std::vector<std::vector<uint8_t>> received_messages;

  MessageCallback callback = [&](uint32_t, const uint8_t* data, size_t len, uint64_t) {
    callback_count++;
    std::lock_guard<std::mutex> lock(received_mutex);
    received_messages.emplace_back(data, data + len);
  };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < messages_per_thread; ++i) {
        // Each thread sends unique data
        std::vector<uint8_t> data = {static_cast<uint8_t>(t), static_cast<uint8_t>(i)};
        channel.log(data.data(), data.size(), static_cast<uint64_t>(t * 1000 + i));
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(callback_count == num_threads * messages_per_thread);
  REQUIRE(received_messages.size() == static_cast<size_t>(num_threads * messages_per_thread));
}

TEST_CASE("Channel - close during log is safe") {
  constexpr int log_iterations = 1000;

  std::atomic<int> callback_count{0};
  MessageCallback callback = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::atomic<bool> close_done{false};

  // Thread 1: logs in a loop
  std::thread log_thread([&]() {
    std::vector<uint8_t> data = {0x01, 0x02};
    for (int i = 0; i < log_iterations; ++i) {
      channel.log(data.data(), data.size(), static_cast<uint64_t>(i));
      // Small yield to allow other thread to run
      if (i % 100 == 0) {
        std::this_thread::yield();
      }
    }
  });

  // Thread 2: closes after a short delay
  std::thread close_thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    channel.close();
    close_done = true;
  });

  log_thread.join();
  close_thread.join();

  // Test completed without crash - that's the success criteria
  REQUIRE(close_done);
}

TEST_CASE("Channel - set_callback rebinds output") {
  int callback_a_count = 0;
  int callback_b_count = 0;

  MessageCallback callback_a = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_a_count++; };
  MessageCallback callback_b = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_b_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback_a);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> test_data = {0x01, 0x02};

  // Log with callback A
  channel.log(test_data.data(), test_data.size(), 1ULL);
  REQUIRE(callback_a_count == 1);
  REQUIRE(callback_b_count == 0);

  // Rebind to callback B
  channel.set_callback(callback_b);

  // Log with callback B
  channel.log(test_data.data(), test_data.size(), 2ULL);
  REQUIRE(callback_a_count == 1);  // Should not increase
  REQUIRE(callback_b_count == 1);  // Should increase
}

TEST_CASE("Channel - set_callback to nullptr silences output") {
  int callback_count = 0;
  MessageCallback callback = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> test_data = {0x01, 0x02};

  // Log with callback
  channel.log(test_data.data(), test_data.size(), 1ULL);
  REQUIRE(callback_count == 1);

  // Rebind to nullptr
  channel.set_callback(nullptr);

  // Log after setting nullptr - should not crash, no delivery
  channel.log(test_data.data(), test_data.size(), 2ULL);
  REQUIRE(callback_count == 1);  // Should not increase
}

TEST_CASE("Schema - construction") {
  std::vector<uint8_t> schema_data = {0x01, 0x02, 0x03};
  Schema schema{"TestSchema", "jsonschema", schema_data};

  REQUIRE(schema.name == "TestSchema");
  REQUIRE(schema.encoding == "jsonschema");
  REQUIRE(schema.data == schema_data);
}

TEST_CASE("ChannelDescriptor - construction and accessors") {
  std::vector<uint8_t> schema_data = {0x01, 0x02};
  Schema schema{"MySchema", "protobuf", schema_data};

  ChannelDescriptor descriptor{42, "/my/topic", "protobuf", schema};

  REQUIRE(descriptor.id == 42);
  REQUIRE(descriptor.topic == "/my/topic");
  REQUIRE(descriptor.encoding == "protobuf");
  REQUIRE(descriptor.schema.name == "MySchema");
  REQUIRE(descriptor.schema.encoding == "protobuf");
  REQUIRE(descriptor.schema.data == schema_data);
}

TEST_CASE("Channel - move semantics") {
  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result.has_value());
  auto channel1 = std::move(result.value());

  // Move constructor
  auto channel2 = std::move(channel1);
  REQUIRE(channel2.id() >= 1);

  // Move assignment
  auto result3 = RawChannel::create("/test_topic2", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result3.has_value());
  auto channel3 = std::move(result3.value());

  channel3 = std::move(channel2);
  REQUIRE(channel3.id() >= 1);
}

TEST_CASE("Channel - descriptor accessor") {
  Schema schema{"TestSchema", "jsonschema", {0x01, 0x02}};
  auto result = RawChannel::create("/test_topic", "json", schema);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  const auto& descriptor = channel.descriptor();
  REQUIRE(descriptor.id == channel.id());
  REQUIRE(descriptor.topic == "/test_topic");
  REQUIRE(descriptor.encoding == "json");
  REQUIRE(descriptor.schema.name == "TestSchema");
}
