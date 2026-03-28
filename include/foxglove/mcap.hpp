#pragma once

#include <foxglove/context.hpp>
#include <foxglove/error.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace foxglove {

enum class McapCompression : uint8_t {
  None = 0,
  Zstd = 1,
};

struct McapWriterOptions {
  std::string profile = "";
  std::string library = "my_foxglove_sdk/0.1";
  bool use_chunks = false;
  McapCompression compression = McapCompression::None;
  size_t chunk_size = 1024 * 1024;
};

struct McapSchema {
  uint16_t id;
  std::string name;
  std::string encoding;
  std::vector<uint8_t> data;
};

struct McapChannel {
  uint16_t id;
  uint16_t schema_id;
  std::string topic;
  std::string message_encoding;
  std::map<std::string, std::string> metadata;
};

struct McapMessage {
  uint16_t channel_id;
  uint32_t sequence;
  uint64_t log_time;
  uint64_t publish_time;
  const uint8_t* data;
  size_t data_len;
};

class McapWriter {
public:
  /// Open a file-backed MCAP writer.
  static FoxgloveResult<McapWriter> open(
    const std::string& path, const McapWriterOptions& options = {}
  );

  /// Open an MCAP writer that appends bytes to an in-memory buffer.
  static McapWriter open_buffer(std::vector<uint8_t>& buf, const McapWriterOptions& options = {});

  /// Add a schema record and return its assigned schema ID.
  FoxgloveResult<uint16_t> add_schema(
    const std::string& name, const std::string& encoding, const std::vector<uint8_t>& data
  );

  /// Add a channel record and return its assigned channel ID.
  FoxgloveResult<uint16_t> add_channel(
    uint16_t schema_id, const std::string& topic, const std::string& message_encoding,
    const std::map<std::string, std::string>& metadata = {}
  );

  /// Write a message record.
  FoxgloveResult<void> write_message(const McapMessage& msg);

  /// Finalize the file with DataEnd, Footer, and trailing magic.
  FoxgloveResult<void> close();

  ~McapWriter();
  McapWriter(McapWriter&&) noexcept;
  McapWriter& operator=(McapWriter&&) noexcept;
  McapWriter(const McapWriter&) = delete;
  McapWriter& operator=(const McapWriter&) = delete;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit McapWriter(std::unique_ptr<Impl> impl);
};

class McapWriterSink : public Sink {
public:
  static FoxgloveResult<std::shared_ptr<McapWriterSink>> create(
    const std::string& path, const McapWriterOptions& options = {}
  );

  void on_channel_added(RawChannel& channel) override;
  void on_channel_removed(uint32_t channel_id) override;
  void on_message(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time) override;

  FoxgloveResult<void> close();

private:
  McapWriter writer_;
  std::mutex mutex_;
  std::unordered_map<uint32_t, uint16_t> channel_map_;
  uint32_t sequence_ = 0;

  explicit McapWriterSink(McapWriter writer);
};

}  // namespace foxglove
