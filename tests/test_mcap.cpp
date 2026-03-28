#include <foxglove/context.hpp>
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

static bool find_opcode(const std::vector<uint8_t>& buf, uint8_t opcode) {
  size_t i = 8;
  while (i + 9 <= buf.size()) {
    const uint8_t op = buf[i];
    uint64_t len = 0;
    for (int b = 0; b < 8; ++b) {
      len |= static_cast<uint64_t>(buf[i + 1 + static_cast<size_t>(b)]) << (8U * b);
    }
    if (op == opcode) {
      return true;
    }
    i += 9 + static_cast<size_t>(len);
  }
  return false;
}

size_t find_opcode_offset(const std::vector<uint8_t>& buf, uint8_t opcode) {
  size_t i = 8;
  while (i + 9 <= buf.size()) {
    const uint8_t op = buf[i];
    uint64_t len = 0;
    for (int b = 0; b < 8; ++b) {
      len |= static_cast<uint64_t>(buf[i + 1 + static_cast<size_t>(b)]) << (8U * b);
    }
    if (op == opcode) {
      return i;
    }
    i += 9 + static_cast<size_t>(len);
  }
  return buf.size();
}

uint16_t read_u16_le(const std::vector<uint8_t>& buf, size_t offset) {
  return static_cast<uint16_t>(buf[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(buf[offset + 1]) << 8U);
}

uint32_t read_u32_le(const std::vector<uint8_t>& buf, size_t offset) {
  return static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8U) |
         (static_cast<uint32_t>(buf[offset + 2]) << 16U) |
         (static_cast<uint32_t>(buf[offset + 3]) << 24U);
}

uint64_t read_u64_le(const std::vector<uint8_t>& buf, size_t offset) {
  return static_cast<uint64_t>(buf[offset]) | (static_cast<uint64_t>(buf[offset + 1]) << 8U) |
         (static_cast<uint64_t>(buf[offset + 2]) << 16U) |
         (static_cast<uint64_t>(buf[offset + 3]) << 24U) |
         (static_cast<uint64_t>(buf[offset + 4]) << 32U) |
         (static_cast<uint64_t>(buf[offset + 5]) << 40U) |
         (static_cast<uint64_t>(buf[offset + 6]) << 48U) |
         (static_cast<uint64_t>(buf[offset + 7]) << 56U);
}

uint32_t crc32_bytes(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      crc = (crc >> 1U) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1U)));
    }
  }
  return ~crc;
}

bool has_opcode(const std::vector<uint8_t>& buf, uint8_t opcode) {
  return find_opcode(buf, opcode);
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

  const size_t footer_index = find_opcode_offset(buf, 0x02U);
  REQUIRE(footer_index < buf.size() - 8U);
}

TEST_CASE("McapWriter - writes schema record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  REQUIRE(writer.close().has_value());
  REQUIRE(find_opcode(buf, 0x03U));
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
  REQUIRE(find_opcode(buf, 0x04U));
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

  const size_t message_index = find_opcode_offset(buf, 0x05U);
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

TEST_CASE("McapWriter - chunked output contains Chunk record") {
  std::vector<uint8_t> buf;
  McapWriterOptions opts;
  opts.use_chunks = true;
  opts.compression = McapCompression::None;

  auto writer = McapWriter::open_buffer(buf, opts);
  auto schema_id = writer.add_schema("test", "jsonschema", {});
  REQUIRE(schema_id.has_value());
  auto channel_id = writer.add_channel(schema_id.value(), "/test", "json");
  REQUIRE(channel_id.has_value());

  const std::string payload = "{\"x\":1}";
  McapMessage msg{
    channel_id.value(),
    0,
    1000,
    1000,
    reinterpret_cast<const uint8_t*>(payload.data()),
    payload.size()
  };
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  REQUIRE(has_opcode(buf, 0x06U));
}

TEST_CASE("McapWriter - zstd compression reduces size") {
  auto make_writer = [](std::vector<uint8_t>& buf, McapCompression compression) {
    McapWriterOptions opts;
    opts.use_chunks = true;
    opts.compression = compression;
    return McapWriter::open_buffer(buf, opts);
  };

  auto write_messages = [](McapWriter& writer) {
    auto schema_id = writer.add_schema("test", "jsonschema", {});
    REQUIRE(schema_id.has_value());
    auto channel_id = writer.add_channel(schema_id.value(), "/test", "json");
    REQUIRE(channel_id.has_value());

    const std::string payload(1024U, 'A');
    for (uint32_t i = 0; i < 100U; ++i) {
      McapMessage msg{
        channel_id.value(),
        i,
        1000 + i,
        1000 + i,
        reinterpret_cast<const uint8_t*>(payload.data()),
        payload.size()
      };
      REQUIRE(writer.write_message(msg).has_value());
    }
  };

  std::vector<uint8_t> uncompressed;
  auto writer_uncompressed = make_writer(uncompressed, McapCompression::None);
  write_messages(writer_uncompressed);
  REQUIRE(writer_uncompressed.close().has_value());

  std::vector<uint8_t> compressed;
  auto writer_compressed = make_writer(compressed, McapCompression::Zstd);
  write_messages(writer_compressed);
  REQUIRE(writer_compressed.close().has_value());

  REQUIRE(compressed.size() < uncompressed.size());
}

TEST_CASE("McapWriter - ChunkIndex records present in summary") {
  std::vector<uint8_t> buf;
  McapWriterOptions opts;
  opts.use_chunks = true;

  auto writer = McapWriter::open_buffer(buf, opts);
  auto schema_id = writer.add_schema("test", "jsonschema", {});
  REQUIRE(schema_id.has_value());
  auto channel_id = writer.add_channel(schema_id.value(), "/test", "json");
  REQUIRE(channel_id.has_value());

  const std::string payload = "{\"x\":1}";
  McapMessage msg{
    channel_id.value(), 0, 42, 42, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()
  };
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  REQUIRE(has_opcode(buf, 0x08U));
}

TEST_CASE("McapWriter - close writes ChunkIndex records before DataEnd") {
  std::vector<uint8_t> buf;
  McapWriterOptions opts;
  opts.use_chunks = true;

  auto writer = McapWriter::open_buffer(buf, opts);
  auto schema_id = writer.add_schema("test", "jsonschema", {});
  REQUIRE(schema_id.has_value());
  auto channel_id = writer.add_channel(schema_id.value(), "/test", "json");
  REQUIRE(channel_id.has_value());

  const std::string payload = "{\"x\":1}";
  McapMessage msg{
    channel_id.value(),
    0,
    1000,
    1000,
    reinterpret_cast<const uint8_t*>(payload.data()),
    payload.size()
  };
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  const size_t chunk_index_pos = find_opcode_offset(buf, 0x08U);
  const size_t data_end_pos = find_opcode_offset(buf, 0x0FU);
  REQUIRE(chunk_index_pos < buf.size());
  REQUIRE(data_end_pos < buf.size());
  REQUIRE(chunk_index_pos < data_end_pos);
}

TEST_CASE("McapWriter - CRC32 of chunk matches stored value") {
  std::vector<uint8_t> buf;
  McapWriterOptions opts;
  opts.use_chunks = true;
  opts.compression = McapCompression::None;

  auto writer = McapWriter::open_buffer(buf, opts);
  auto schema_id = writer.add_schema("test", "jsonschema", {});
  REQUIRE(schema_id.has_value());
  auto channel_id = writer.add_channel(schema_id.value(), "/test", "json");
  REQUIRE(channel_id.has_value());

  const std::string payload = "{\"v\":12345}";
  McapMessage msg{
    channel_id.value(),
    7,
    100,
    100,
    reinterpret_cast<const uint8_t*>(payload.data()),
    payload.size()
  };
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  const size_t chunk_pos = find_opcode_offset(buf, 0x06U);
  REQUIRE(chunk_pos < buf.size());

  const uint64_t payload_len = read_u64_le(buf, chunk_pos + 1U);
  const size_t payload_begin = chunk_pos + 9U;
  REQUIRE(payload_begin + static_cast<size_t>(payload_len) <= buf.size());

  size_t off = payload_begin;
  off += 8U;
  off += 8U;
  const uint64_t uncompressed_size = read_u64_le(buf, off);
  off += 8U;
  const uint32_t stored_crc = read_u32_le(buf, off);
  off += 4U;

  const uint32_t compression_len = read_u32_le(buf, off);
  off += 4U;
  REQUIRE(off + compression_len <= payload_begin + static_cast<size_t>(payload_len));
  off += compression_len;

  const uint64_t compressed_size = read_u64_le(buf, off);
  off += 8U;
  REQUIRE(compressed_size == uncompressed_size);
  REQUIRE(
    off + static_cast<size_t>(compressed_size) <= payload_begin + static_cast<size_t>(payload_len)
  );

  const uint8_t* chunk_data = buf.data() + off;
  const uint32_t computed_crc = crc32_bytes(chunk_data, static_cast<size_t>(uncompressed_size));
  REQUIRE(stored_crc == computed_crc);
}

TEST_CASE("McapWriterSink - routes Context messages into MCAP") {
  constexpr const char* kPath = "/tmp/test_mcap_sink_output.mcap";

  McapWriterOptions opts;
  opts.use_chunks = true;

  auto sink_result = McapWriterSink::create(kPath, opts);
  REQUIRE(sink_result.has_value());
  auto sink = sink_result.value();

  auto context_result = Context::create();
  REQUIRE(context_result.has_value());
  auto context = std::move(context_result.value());
  context.add_sink(sink);

  auto channel_result =
    context.create_channel("/sink/topic", "json", Schema{"SinkSchema", "jsonschema", {}});
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  const std::string payload = "{\"sink\":true}";
  channel.log(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), 123456ULL);

  REQUIRE(sink->close().has_value());

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

  REQUIRE(has_opcode(disk_data, 0x06U));
  REQUIRE(std::remove(kPath) == 0);
}
