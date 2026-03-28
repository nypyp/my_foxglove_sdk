#include <foxglove/mcap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace foxglove;

namespace {

constexpr std::array<uint8_t, 8> kMcapMagic = {0x89, 0x4D, 0x43, 0x41, 0x50, 0x30, 0x0D, 0x0A};

bool has_magic_prefix(const std::vector<uint8_t>& buf) {
  if (buf.size() < kMcapMagic.size()) {
    return false;
  }

  for (size_t i = 0; i < kMcapMagic.size(); ++i) {
    if (buf[i] != kMcapMagic[i]) {
      return false;
    }
  }
  return true;
}

bool has_magic_suffix(const std::vector<uint8_t>& buf) {
  if (buf.size() < kMcapMagic.size()) {
    return false;
  }

  const size_t start = buf.size() - kMcapMagic.size();
  for (size_t i = 0; i < kMcapMagic.size(); ++i) {
    if (buf[start + i] != kMcapMagic[i]) {
      return false;
    }
  }
  return true;
}

size_t find_opcode(const std::vector<uint8_t>& buf, uint8_t opcode) {
  constexpr size_t kRecordPrefixLen = 1U + 8U;
  if (buf.size() < 8U + kRecordPrefixLen) {
    return buf.size();
  }

  for (size_t i = 8; i + kRecordPrefixLen <= buf.size(); ++i) {
    if (buf[i] == opcode) {
      return i;
    }
  }
  return buf.size();
}

uint16_t read_u16_le(const std::vector<uint8_t>& buf, size_t offset) {
  return static_cast<uint16_t>(buf[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(buf[offset + 1]) << 8U);
}

}  // namespace

TEST_CASE("McapWriter - empty file is valid") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  auto close_result = writer.close();
  REQUIRE(close_result.has_value());

  REQUIRE(has_magic_prefix(buf));
  REQUIRE(has_magic_suffix(buf));
}

TEST_CASE("McapWriter - writes valid header and footer") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  REQUIRE(writer.close().has_value());

  REQUIRE(has_magic_prefix(buf));
  REQUIRE(has_magic_suffix(buf));
  REQUIRE(buf.size() > 8U);
  REQUIRE(buf[8] == 0x01U);

  const size_t footer_index = find_opcode(buf, 0x02U);
  REQUIRE(footer_index < buf.size() - 8U);
}

TEST_CASE("McapWriter - writes schema record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  REQUIRE(writer.close().has_value());
  REQUIRE(find_opcode(buf, 0x03U) < buf.size());
}

TEST_CASE("McapWriter - writes channel record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  REQUIRE(channel_result.has_value());

  REQUIRE(writer.close().has_value());
  REQUIRE(find_opcode(buf, 0x04U) < buf.size());
}

TEST_CASE("McapWriter - writes message record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  REQUIRE(channel_result.has_value());

  const std::array<uint8_t, 3> payload = {0x10U, 0x20U, 0x30U};
  McapMessage msg{
    channel_result.value(),
    7,
    1'234'567'890ULL,
    1'234'567'999ULL,
    payload.data(),
    payload.size(),
  };
  REQUIRE(writer.write_message(msg).has_value());

  REQUIRE(writer.close().has_value());

  const size_t message_index = find_opcode(buf, 0x05U);
  REQUIRE(message_index < buf.size());

  const size_t payload_offset = message_index + 1U + 8U;
  REQUIRE(payload_offset + 2U <= buf.size());
  REQUIRE(read_u16_le(buf, payload_offset) == channel_result.value());
}

TEST_CASE("McapWriter - schema and channel IDs start at 1") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'x'};
  auto s1 = writer.add_schema("S1", "jsonschema", schema_data);
  auto s2 = writer.add_schema("S2", "jsonschema", schema_data);
  REQUIRE(s1.has_value());
  REQUIRE(s2.has_value());
  REQUIRE(s1.value() == 1U);
  REQUIRE(s2.value() == 2U);

  auto c1 = writer.add_channel(s1.value(), "/topic/1", "json");
  auto c2 = writer.add_channel(s2.value(), "/topic/2", "json");
  REQUIRE(c1.has_value());
  REQUIRE(c2.has_value());
  REQUIRE(c1.value() == 1U);
  REQUIRE(c2.value() == 2U);
}

TEST_CASE("McapWriter - write to file") {
  constexpr const char* kPath = "/tmp/test_mcap_output.mcap";

  auto open_result = McapWriter::open(kPath);
  REQUIRE(open_result.has_value());
  auto writer = std::move(open_result.value());

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  REQUIRE(channel_result.has_value());

  const std::array<uint8_t, 2> payload = {0xABU, 0xCDU};
  McapMessage msg{
    channel_result.value(),
    1,
    100ULL,
    100ULL,
    payload.data(),
    payload.size(),
  };
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  FILE* file = std::fopen(kPath, "rb");
  REQUIRE(file != nullptr);

  REQUIRE(std::fseek(file, 0, SEEK_END) == 0);
  const long file_size = std::ftell(file);
  REQUIRE(file_size > 16);
  REQUIRE(std::fseek(file, 0, SEEK_SET) == 0);

  std::vector<uint8_t> disk_data(static_cast<size_t>(file_size));
  const size_t read_count = std::fread(disk_data.data(), 1, disk_data.size(), file);
  REQUIRE(read_count == disk_data.size());
  std::fclose(file);

  REQUIRE(has_magic_prefix(disk_data));
  REQUIRE(has_magic_suffix(disk_data));

  REQUIRE(std::remove(kPath) == 0);
}
