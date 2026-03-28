#include <foxglove/mcap.hpp>

#include <array>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>

namespace foxglove {

namespace {

constexpr std::array<uint8_t, 8> kMagic = {0x89, 0x4D, 0x43, 0x41, 0x50, 0x30, 0x0D, 0x0A};

constexpr uint8_t kHeaderOp = 0x01;
constexpr uint8_t kFooterOp = 0x02;
constexpr uint8_t kSchemaOp = 0x03;
constexpr uint8_t kChannelOp = 0x04;
constexpr uint8_t kMessageOp = 0x05;
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

}  // namespace

struct McapWriter::Impl {
  std::function<void(const uint8_t*, size_t)> write_cb;
  std::FILE* file = nullptr;
  bool closed = false;
  bool io_error = false;
  uint16_t next_schema_id = 1;
  uint16_t next_channel_id = 1;
  std::set<uint16_t> schemas;
  std::set<uint16_t> channels;

  void write_raw(const uint8_t* data, size_t len) {
    if (closed || io_error) {
      return;
    }
    write_cb(data, len);
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

  FoxgloveResult<void> write_header(const McapWriterOptions& options) {
    std::vector<uint8_t> payload;
    append_string(payload, options.profile);
    append_string(payload, options.library);
    return write_record(kHeaderOp, payload);
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

  auto write_header_result = impl->write_header(options);
  if (!write_header_result.has_value()) {
    (void)impl->finalize_file();
    return tl::make_unexpected(write_header_result.error());
  }

  return McapWriter(std::move(impl));
}

McapWriter McapWriter::open_buffer(std::vector<uint8_t>& buf, const McapWriterOptions& options) {
  auto impl = std::make_unique<Impl>();

  impl->write_cb = [&buf](const uint8_t* data, size_t len) {
    if (len == 0) {
      return;
    }
    buf.insert(buf.end(), data, data + len);
  };

  impl->write_magic();
  impl->write_header(options);

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

  return impl_->write_record(kMessageOp, payload);
}

FoxgloveResult<void> McapWriter::close() {
  if (!impl_) {
    return {};
  }

  if (impl_->closed) {
    return {};
  }

  std::vector<uint8_t> data_end_payload;
  append_u32_le(data_end_payload, 0U);
  auto data_end_result = impl_->write_record(kDataEndOp, data_end_payload);
  if (!data_end_result.has_value()) {
    return tl::make_unexpected(data_end_result.error());
  }

  std::vector<uint8_t> footer_payload;
  append_u64_le(footer_payload, 0U);
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

}  // namespace foxglove
