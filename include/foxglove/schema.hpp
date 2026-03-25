#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace foxglove {

/// @brief Schema descriptor for message data.
///
/// Describes the schema of messages published on a channel, including
/// the schema name, encoding format, and raw schema data.
struct Schema {
  /// @brief Name of the schema (e.g., "geometry_msgs/Twist").
  std::string name;

  /// @brief Encoding of the schema (e.g., "jsonschema", "protobuf").
  std::string encoding;

  /// @brief Raw schema data bytes.
  std::vector<uint8_t> data;

  /// @brief Construct a Schema with all fields.
  Schema(std::string name, std::string encoding, std::vector<uint8_t> data)
      : name(std::move(name)), encoding(std::move(encoding)), data(std::move(data)) {}
};

/// @brief Descriptor for a channel.
///
/// Contains metadata about a channel including its ID, topic,
/// message encoding, and associated schema.
struct ChannelDescriptor {
  /// @brief Unique channel ID.
  uint32_t id;

  /// @brief Topic name (e.g., "/robot/cmd_vel").
  std::string topic;

  /// @brief Message encoding (e.g., "json", "protobuf").
  std::string encoding;

  /// @brief Schema descriptor for messages on this channel.
  Schema schema;

  /// @brief Construct a ChannelDescriptor with all fields.
  ChannelDescriptor(uint32_t id, std::string topic, std::string encoding, Schema schema)
      : id(id),
        topic(std::move(topic)),
        encoding(std::move(encoding)),
        schema(std::move(schema)) {}
};

}  // namespace foxglove
