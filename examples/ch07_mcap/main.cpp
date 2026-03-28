#include <foxglove/mcap.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool write_minimal_demo(const std::string& path) {
  auto writer_result = foxglove::McapWriter::open(path);
  if (!writer_result.has_value()) {
    return false;
  }

  auto writer = std::move(writer_result.value());

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  if (!schema_result.has_value()) {
    return false;
  }

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  if (!channel_result.has_value()) {
    return false;
  }

  constexpr uint64_t base_time = 1'700'000'000'000'000'000ULL;
  for (uint32_t i = 0; i < 3; ++i) {
    const std::array<uint8_t, 3> payload = {
      static_cast<uint8_t>('0' + i), static_cast<uint8_t>(':'), static_cast<uint8_t>('a' + i)
    };

    foxglove::McapMessage msg{
      channel_result.value(),
      i,
      base_time + static_cast<uint64_t>(i) * 1'000'000ULL,
      base_time + static_cast<uint64_t>(i) * 1'000'000ULL,
      payload.data(),
      payload.size(),
    };

    if (!writer.write_message(msg).has_value()) {
      return false;
    }
  }

  if (!writer.close().has_value()) {
    return false;
  }

  return true;
}

bool write_chunked_compressed_demo(const std::string& path) {
  foxglove::McapWriterOptions options;
  options.use_chunks = true;
  options.compression = foxglove::McapCompression::Zstd;
  options.chunk_size = 4 * 1024;

  auto writer_result = foxglove::McapWriter::open(path, options);
  if (!writer_result.has_value()) {
    return false;
  }

  auto writer = std::move(writer_result.value());
  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Telemetry", "jsonschema", schema_data);
  if (!schema_result.has_value()) {
    return false;
  }

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/chunked", "json");
  if (!channel_result.has_value()) {
    return false;
  }

  constexpr uint64_t base_time = 1'700'000'000'500'000'000ULL;
  const std::string payload(256U, 'A');
  for (uint32_t i = 0; i < 120U; ++i) {
    foxglove::McapMessage msg{
      channel_result.value(),
      i,
      base_time + static_cast<uint64_t>(i) * 500'000ULL,
      base_time + static_cast<uint64_t>(i) * 500'000ULL,
      reinterpret_cast<const uint8_t*>(payload.data()),
      payload.size(),
    };
    if (!writer.write_message(msg).has_value()) {
      return false;
    }
  }

  if (!writer.close().has_value()) {
    return false;
  }

  return true;
}

}  // namespace

int main() {
  if (!write_minimal_demo("output.mcap")) {
    return 1;
  }

  if (!write_chunked_compressed_demo("output_chunked_zstd.mcap")) {
    return 1;
  }

  std::cout << "MCAP written to output.mcap and output_chunked_zstd.mcap" << std::endl;
  return 0;
}
