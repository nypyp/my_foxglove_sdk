#include <foxglove/context.hpp>
#include <foxglove/mcap.hpp>
#include <foxglove/messages.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace foxglove;

namespace {

constexpr std::array<uint8_t, 8> kMcapMagic = {0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};

bool file_exists(const char* path) {
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    return false;
  }
  std::fclose(file);
  return true;
}

std::vector<uint8_t> read_file_bytes(const char* path) {
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    return {};
  }

  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return {};
  }

  const long size = std::ftell(file);
  if (size <= 0) {
    std::fclose(file);
    return {};
  }

  if (std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return {};
  }

  std::vector<uint8_t> data(static_cast<size_t>(size));
  const size_t read_count = std::fread(data.data(), 1, data.size(), file);
  std::fclose(file);

  if (read_count != data.size()) {
    return {};
  }

  return data;
}

bool has_magic_prefix(const std::vector<uint8_t>& data) {
  if (data.size() < kMcapMagic.size()) {
    return false;
  }

  for (size_t i = 0; i < kMcapMagic.size(); ++i) {
    if (data[i] != kMcapMagic[i]) {
      return false;
    }
  }
  return true;
}

bool has_magic_suffix(const std::vector<uint8_t>& data) {
  if (data.size() < kMcapMagic.size()) {
    return false;
  }

  const size_t start = data.size() - kMcapMagic.size();
  for (size_t i = 0; i < kMcapMagic.size(); ++i) {
    if (data[start + i] != kMcapMagic[i]) {
      return false;
    }
  }
  return true;
}

uint64_t now_ns() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
    duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count()
  );
}

}  // namespace

TEST_CASE("E2E - McapWriter file has header and footer magic") {
  constexpr const char* kPath = "/tmp/test_e2e_magic.mcap";
  (void)std::remove(kPath);

  auto writer_result = McapWriter::open(kPath);
  REQUIRE(writer_result.has_value());
  auto writer = std::move(writer_result.value());

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_id = writer.add_schema("foxglove.SceneUpdate", "jsonschema", schema_data);
  REQUIRE(schema_id.has_value());

  auto channel_id = writer.add_channel(schema_id.value(), "/scene", "json");
  REQUIRE(channel_id.has_value());

  const std::string payload = "{\"entities\":[],\"deletions\":[]}";
  McapMessage msg{
    channel_id.value(),
    0,
    1'000ULL,
    1'000ULL,
    reinterpret_cast<const uint8_t*>(payload.data()),
    payload.size(),
  };

  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  const auto file_bytes = read_file_bytes(kPath);
  REQUIRE(file_bytes.size() > 0);
  REQUIRE(has_magic_prefix(file_bytes));
  REQUIRE(has_magic_suffix(file_bytes));

  REQUIRE(std::remove(kPath) == 0);
}

TEST_CASE("E2E - Context with McapWriterSink logs message to file") {
  constexpr const char* kPath = "/tmp/test_e2e_sink.mcap";
  (void)std::remove(kPath);

  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink_result = McapWriterSink::create(kPath);
  REQUIRE(sink_result.has_value());
  auto sink = sink_result.value();

  context.add_sink(sink);

  auto scene_raw_result = context.create_channel(
    "/scene", "json", Schema{"foxglove.SceneUpdate", "jsonschema", {'{', '}'}}
  );
  REQUIRE(scene_raw_result.has_value());

  SceneUpdateChannel scene_channel(std::move(scene_raw_result.value()));

  SceneUpdate update;
  SceneEntity entity;
  entity.frame_id = "map";
  entity.id = "entity_test";
  entity.timestamp.sec = 1;
  entity.timestamp.nsec = 2;

  CubePrimitive cube;
  cube.pose.orientation.w = 1.0;
  cube.size = {1.0, 1.0, 1.0};
  cube.color = {0.1, 0.8, 0.2, 1.0};
  entity.cubes.push_back(cube);

  update.entities.push_back(entity);

  REQUIRE(scene_channel.log(update, now_ns()).has_value());
  REQUIRE(sink->close().has_value());

  REQUIRE(file_exists(kPath));
  const auto file_bytes = read_file_bytes(kPath);
  REQUIRE(file_bytes.size() > 0);
  REQUIRE(has_magic_prefix(file_bytes));
  REQUIRE(has_magic_suffix(file_bytes));

  REQUIRE(std::remove(kPath) == 0);
}
