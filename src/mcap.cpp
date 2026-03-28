#include <foxglove/mcap.hpp>

#include <array>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>
#include <zstd.h>

namespace foxglove {

namespace {

constexpr std::array<uint8_t, 8> kMagic = {0x89, 0x4D, 0x43, 0x41, 0x50, 0x30, 0x0D, 0x0A};

constexpr uint8_t kHeaderOp = 0x01;
constexpr uint8_t kFooterOp = 0x02;
constexpr uint8_t kSchemaOp = 0x03;
constexpr uint8_t kChannelOp = 0x04;
constexpr uint8_t kMessageOp = 0x05;
constexpr uint8_t kChunkOp = 0x06;
constexpr uint8_t kChunkIndexOp = 0x08;
constexpr uint8_t kDataEndOp = 0x0F;

void append_u16_le(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32_le(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
}

void append_u64_le(std::vector<uint8_t>& out, uint64_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 32U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 40U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 48U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 56U) & 0xFFU));
}

void append_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
  if (len == 0) {
    return;
  }
  out.insert(out.end(), data, data + len);
}

void append_string(std::vector<uint8_t>& out, const std::string& value) {
  append_u32_le(out, static_cast<uint32_t>(value.size()));
  append_bytes(out, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void append_u16_u64_map(std::vector<uint8_t>& out, const std::map<uint16_t, uint64_t>& values) {
  append_u32_le(out, static_cast<uint32_t>(values.size()));
  for (const auto& [key, value] : values) {
    append_u16_le(out, key);
    append_u64_le(out, value);
  }
}

uint32_t crc32_compute(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      crc = (crc >> 1U) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1U)));
    }
  }
  return ~crc;
}

std::vector<uint8_t> make_record(uint8_t opcode, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out;
  out.reserve(1U + 8U + payload.size());
  out.push_back(opcode);
  append_u64_le(out, static_cast<uint64_t>(payload.size()));
  append_bytes(out, payload.data(), payload.size());
  return out;
}

}  // namespace

struct McapWriter::Impl {
  struct ChunkIndexEntry {
    uint64_t message_start_time = 0;
    uint64_t message_end_time = 0;
    uint64_t chunk_start_offset = 0;
    uint64_t chunk_length = 0;
    std::string compression;
    uint64_t compressed_size = 0;
    uint64_t uncompressed_size = 0;
  };

  std::function<void(const uint8_t*, size_t)> write_cb;
  std::FILE* file = nullptr;
  McapWriterOptions options;
  bool closed = false;
  bool io_error = false;
  uint16_t next_schema_id = 1;
  uint16_t next_channel_id = 1;
  std::set<uint16_t> schemas;
  std::set<uint16_t> channels;
  uint64_t bytes_written = 0;
  std::vector<uint8_t> chunk_data;
  bool chunk_has_messages = false;
  uint64_t chunk_start_time = 0;
  uint64_t chunk_end_time = 0;
  std::vector<ChunkIndexEntry> chunk_indexes;

  void write_raw(const uint8_t* data, size_t len) {
    if (closed || io_error) {
      return;
    }
    write_cb(data, len);
    if (!io_error) {
      bytes_written += static_cast<uint64_t>(len);
    }
  }

  FoxgloveResult<void> write_record(uint8_t opcode, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> header;
    header.reserve(9);
    header.push_back(opcode);
    append_u64_le(header, static_cast<uint64_t>(payload.size()));

    write_raw(header.data(), header.size());
    if (!payload.empty()) {
      write_raw(payload.data(), payload.size());
    }

    if (io_error) {
      return tl::make_unexpected(FoxgloveError::IoError);
    }
    return {};
  }

  FoxgloveResult<void> write_magic() {
    write_raw(kMagic.data(), kMagic.size());
    if (io_error) {
      return tl::make_unexpected(FoxgloveError::IoError);
    }
    return {};
  }

  FoxgloveResult<void> write_header() {
    std::vector<uint8_t> payload;
    append_string(payload, options.profile);
    append_string(payload, options.library);
    return write_record(kHeaderOp, payload);
  }

  FoxgloveResult<void> flush_chunk() {
    if (!options.use_chunks || chunk_data.empty()) {
      return {};
    }

    const uint64_t uncompressed_size = static_cast<uint64_t>(chunk_data.size());
    const uint32_t uncompressed_crc =
      chunk_data.empty() ? 0U : crc32_compute(chunk_data.data(), chunk_data.size());

    std::string compression;
    std::vector<uint8_t> compressed_data;
    if (options.compression == McapCompression::Zstd && !chunk_data.empty()) {
      compression = "zstd";
      compressed_data.resize(ZSTD_compressBound(chunk_data.size()));
      const size_t compressed_size = ZSTD_compress(
        compressed_data.data(), compressed_data.size(), chunk_data.data(), chunk_data.size(), 1
      );
      if (ZSTD_isError(compressed_size) != 0U) {
        return tl::make_unexpected(FoxgloveError::IoError);
      }
      compressed_data.resize(compressed_size);
    } else {
      compression.clear();
      compressed_data = chunk_data;
    }

    std::vector<uint8_t> payload;
    append_u64_le(payload, chunk_start_time);
    append_u64_le(payload, chunk_end_time);
    append_u64_le(payload, uncompressed_size);
    append_u32_le(payload, uncompressed_crc);
    append_string(payload, compression);
    append_u64_le(payload, static_cast<uint64_t>(compressed_data.size()));
    append_bytes(payload, compressed_data.data(), compressed_data.size());

    const uint64_t chunk_record_start = bytes_written;
    auto write_result = write_record(kChunkOp, payload);
    if (!write_result.has_value()) {
      return write_result;
    }

    ChunkIndexEntry entry;
    entry.message_start_time = chunk_start_time;
    entry.message_end_time = chunk_end_time;
    entry.chunk_start_offset = chunk_record_start;
    entry.chunk_length = static_cast<uint64_t>(1U + 8U + payload.size());
    entry.compression = compression;
    entry.compressed_size = static_cast<uint64_t>(compressed_data.size());
    entry.uncompressed_size = uncompressed_size;
    chunk_indexes.push_back(std::move(entry));

    chunk_data.clear();
    chunk_has_messages = false;
    chunk_start_time = 0;
    chunk_end_time = 0;
    return {};
  }

  FoxgloveResult<void> write_summary_if_needed(uint64_t& summary_start) {
    if (!options.use_chunks) {
      summary_start = 0;
      return {};
    }

    summary_start = chunk_indexes.empty() ? 0U : bytes_written;
    for (const auto& index : chunk_indexes) {
      std::vector<uint8_t> payload;
      append_u64_le(payload, index.message_start_time);
      append_u64_le(payload, index.message_end_time);
      append_u64_le(payload, index.chunk_start_offset);
      append_u64_le(payload, index.chunk_length);
      append_u16_u64_map(payload, {});
      append_u64_le(payload, 0U);
      append_string(payload, index.compression);
      append_u64_le(payload, index.compressed_size);
      append_u64_le(payload, index.uncompressed_size);
      auto result = write_record(kChunkIndexOp, payload);
      if (!result.has_value()) {
        return result;
      }
    }

    return {};
  }

  FoxgloveResult<void> finalize_file() {
    if (file != nullptr) {
      if (std::fclose(file) != 0) {
        io_error = true;
      }
      file = nullptr;
    }

    if (io_error) {
      return tl::make_unexpected(FoxgloveError::IoError);
    }
    return {};
  }
};

McapWriter::McapWriter(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

FoxgloveResult<McapWriter> McapWriter::open(
  const std::string& path, const McapWriterOptions& options
) {
  auto impl = std::make_unique<Impl>();
  impl->options = options;

  impl->file = std::fopen(path.c_str(), "wb");
  if (impl->file == nullptr) {
    return tl::make_unexpected(FoxgloveError::IoError);
  }

  Impl* impl_ptr = impl.get();
  impl->write_cb = [impl_ptr](const uint8_t* data, size_t len) {
    if (len == 0) {
      return;
    }
    const size_t wrote = std::fwrite(data, 1, len, impl_ptr->file);
    if (wrote != len) {
      impl_ptr->io_error = true;
    }
  };

  auto write_magic_result = impl->write_magic();
  if (!write_magic_result.has_value()) {
    (void)impl->finalize_file();
    return tl::make_unexpected(write_magic_result.error());
  }

  auto write_header_result = impl->write_header();
  if (!write_header_result.has_value()) {
    (void)impl->finalize_file();
    return tl::make_unexpected(write_header_result.error());
  }

  return McapWriter(std::move(impl));
}

McapWriter McapWriter::open_buffer(std::vector<uint8_t>& buf, const McapWriterOptions& options) {
  auto impl = std::make_unique<Impl>();
  impl->options = options;

  impl->write_cb = [&buf](const uint8_t* data, size_t len) {
    if (len == 0) {
      return;
    }
    buf.insert(buf.end(), data, data + len);
  };

  impl->write_magic();
  impl->write_header();

  return McapWriter(std::move(impl));
}

FoxgloveResult<uint16_t> McapWriter::add_schema(
  const std::string& name, const std::string& encoding, const std::vector<uint8_t>& data
) {
  if (!impl_ || impl_->closed) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  const uint16_t schema_id = impl_->next_schema_id++;

  std::vector<uint8_t> payload;
  append_u16_le(payload, schema_id);
  append_string(payload, name);
  append_string(payload, encoding);
  append_u32_le(payload, static_cast<uint32_t>(data.size()));
  append_bytes(payload, data.data(), data.size());

  auto write_schema_result = impl_->write_record(kSchemaOp, payload);
  if (!write_schema_result.has_value()) {
    return tl::make_unexpected(write_schema_result.error());
  }
  impl_->schemas.insert(schema_id);

  return schema_id;
}

FoxgloveResult<uint16_t> McapWriter::add_channel(
  uint16_t schema_id, const std::string& topic, const std::string& message_encoding,
  const std::map<std::string, std::string>& metadata
) {
  if (!impl_ || impl_->closed) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  if (schema_id != 0 && impl_->schemas.find(schema_id) == impl_->schemas.end()) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  const uint16_t channel_id = impl_->next_channel_id++;

  std::vector<uint8_t> payload;
  append_u16_le(payload, channel_id);
  append_u16_le(payload, schema_id);
  append_string(payload, topic);
  append_string(payload, message_encoding);
  append_u32_le(payload, static_cast<uint32_t>(metadata.size()));
  for (const auto& [key, value] : metadata) {
    append_string(payload, key);
    append_string(payload, value);
  }

  auto write_channel_result = impl_->write_record(kChannelOp, payload);
  if (!write_channel_result.has_value()) {
    return tl::make_unexpected(write_channel_result.error());
  }
  impl_->channels.insert(channel_id);

  return channel_id;
}

FoxgloveResult<void> McapWriter::write_message(const McapMessage& msg) {
  if (!impl_ || impl_->closed) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  if (impl_->channels.find(msg.channel_id) == impl_->channels.end()) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  if (msg.data == nullptr && msg.data_len > 0) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  std::vector<uint8_t> payload;
  append_u16_le(payload, msg.channel_id);
  append_u32_le(payload, msg.sequence);
  append_u64_le(payload, msg.log_time);
  append_u64_le(payload, msg.publish_time);
  append_bytes(payload, msg.data, msg.data_len);

  if (!impl_->options.use_chunks) {
    return impl_->write_record(kMessageOp, payload);
  }

  std::vector<uint8_t> record = make_record(kMessageOp, payload);
  append_bytes(impl_->chunk_data, record.data(), record.size());

  if (!impl_->chunk_has_messages) {
    impl_->chunk_has_messages = true;
    impl_->chunk_start_time = msg.log_time;
    impl_->chunk_end_time = msg.log_time;
  } else {
    if (msg.log_time < impl_->chunk_start_time) {
      impl_->chunk_start_time = msg.log_time;
    }
    if (msg.log_time > impl_->chunk_end_time) {
      impl_->chunk_end_time = msg.log_time;
    }
  }

  if (impl_->chunk_data.size() >= impl_->options.chunk_size) {
    return impl_->flush_chunk();
  }

  return {};
}

FoxgloveResult<void> McapWriter::close() {
  if (!impl_) {
    return {};
  }

  if (impl_->closed) {
    return {};
  }

  auto flush_result = impl_->flush_chunk();
  if (!flush_result.has_value()) {
    return tl::make_unexpected(flush_result.error());
  }

  uint64_t summary_start = 0;
  auto summary_result = impl_->write_summary_if_needed(summary_start);
  if (!summary_result.has_value()) {
    return tl::make_unexpected(summary_result.error());
  }

  std::vector<uint8_t> data_end_payload;
  append_u32_le(data_end_payload, 0U);
  auto data_end_result = impl_->write_record(kDataEndOp, data_end_payload);
  if (!data_end_result.has_value()) {
    return tl::make_unexpected(data_end_result.error());
  }

  std::vector<uint8_t> footer_payload;
  append_u64_le(footer_payload, summary_start);
  append_u64_le(footer_payload, 0U);
  append_u32_le(footer_payload, 0U);
  auto footer_result = impl_->write_record(kFooterOp, footer_payload);
  if (!footer_result.has_value()) {
    return tl::make_unexpected(footer_result.error());
  }

  auto tail_magic_result = impl_->write_magic();
  if (!tail_magic_result.has_value()) {
    return tl::make_unexpected(tail_magic_result.error());
  }

  impl_->closed = true;
  return impl_->finalize_file();
}

McapWriter::~McapWriter() {
  if (impl_ && !impl_->closed) {
    (void)close();
  }
}

McapWriter::McapWriter(McapWriter&&) noexcept = default;

McapWriter& McapWriter::operator=(McapWriter&&) noexcept = default;

FoxgloveResult<std::shared_ptr<McapWriterSink>> McapWriterSink::create(
  const std::string& path, const McapWriterOptions& options
) {
  auto writer_result = McapWriter::open(path, options);
  if (!writer_result.has_value()) {
    return tl::make_unexpected(writer_result.error());
  }

  return std::shared_ptr<McapWriterSink>(new McapWriterSink(std::move(writer_result.value())));
}

McapWriterSink::McapWriterSink(McapWriter writer)
    : writer_(std::move(writer)) {}

void McapWriterSink::on_channel_added(RawChannel& channel) {
  std::lock_guard<std::mutex> lock(mutex_);

  const uint32_t context_id = channel.id();
  const auto& desc = channel.descriptor();

  auto schema_result = writer_.add_schema(desc.schema.name, desc.schema.encoding, desc.schema.data);
  if (!schema_result.has_value()) {
    return;
  }

  auto channel_result = writer_.add_channel(schema_result.value(), desc.topic, desc.encoding);
  if (!channel_result.has_value()) {
    return;
  }

  channel_map_[context_id] = channel_result.value();
}

void McapWriterSink::on_channel_removed(uint32_t) {}

void McapWriterSink::on_message(
  uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time
) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = channel_map_.find(channel_id);
  if (it == channel_map_.end()) {
    return;
  }

  McapMessage msg{it->second, sequence_++, log_time, log_time, data, len};
  (void)writer_.write_message(msg);
}

FoxgloveResult<void> McapWriterSink::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  return writer_.close();
}

}  // namespace foxglove
