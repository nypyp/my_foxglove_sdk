#include <foxglove/mcap.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  auto writer_result = foxglove::McapWriter::open("output.mcap");
  if (!writer_result.has_value()) {
    return 1;
  }

  auto writer = std::move(writer_result.value());

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  if (!schema_result.has_value()) {
    return 1;
  }

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  if (!channel_result.has_value()) {
    return 1;
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
      return 1;
    }
  }

  if (!writer.close().has_value()) {
    return 1;
  }

  std::cout << "MCAP written to output.mcap" << std::endl;
  return 0;
}
