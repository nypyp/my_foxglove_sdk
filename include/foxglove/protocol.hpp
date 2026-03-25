#pragma once

#include <foxglove/error.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace foxglove {

/// @brief Server info message sent to clients upon connection.
///
/// Spec: https://github.com/foxglove/ws-protocol/blob/main/docs/spec.md#server-info
struct ServerInfo {
  /// @brief Free-form information about the server.
  std::string name;
  /// @brief Bitmask of server capabilities.
  uint32_t capabilities = 0;
  /// @brief Supported message encodings for client publishing.
  std::vector<std::string> supported_encodings;
  /// @brief Optional metadata key-value pairs.
  std::map<std::string, std::string> metadata;
  /// @brief Optional session ID for connection state management.
  std::string session_id;
};

/// @brief Channel advertisement for a single channel.
///
/// Part of the Advertise message sent by the server.
struct ChannelAdvertisement {
  /// @brief Unique channel ID.
  uint32_t id;
  /// @brief Topic name.
  std::string topic;
  /// @brief Message encoding (e.g., "json", "protobuf").
  std::string encoding;
  /// @brief Schema name.
  std::string schema_name;
  /// @brief Schema encoding (e.g., "jsonschema", "protobuf").
  std::string schema_encoding;
  /// @brief Schema data as string.
  std::string schema_data;
};

/// @brief Subscription entry for Subscribe message.
///
/// Maps a subscription ID to a channel ID.
struct Subscription {
  /// @brief Client-chosen subscription ID.
  uint32_t subscription_id;
  /// @brief Channel ID to subscribe to.
  uint32_t channel_id;
};

/// @brief Subscribe message from client.
///
/// Spec: https://github.com/foxglove/ws-protocol/blob/main/docs/spec.md#subscribe
struct Subscribe {
  /// @brief List of subscriptions.
  std::vector<Subscription> subscriptions;
};

/// @brief Unsubscribe message from client.
///
/// Spec: https://github.com/foxglove/ws-protocol/blob/main/docs/spec.md#unsubscribe
struct Unsubscribe {
  /// @brief List of subscription IDs to unsubscribe.
  std::vector<uint32_t> subscription_ids;
};

/// @brief Client message variant - either Subscribe or Unsubscribe.
using ClientMessage = std::variant<Subscribe, Unsubscribe>;

/// @brief Binary message data frame sent by server.
///
/// Spec: https://github.com/foxglove/ws-protocol/blob/main/docs/spec.md#message-data
struct MessageData {
  /// @brief Subscription ID this data is for.
  uint32_t subscription_id;
  /// @brief Log/receive timestamp in nanoseconds.
  uint64_t log_time;
  /// @brief Message payload data.
  std::vector<uint8_t> data;
};

/// @brief Encode server info to JSON string.
///
/// @param info Server info to encode.
/// @return JSON string on success, error on failure.
FoxgloveResult<std::string> encode_server_info(const ServerInfo& info);

/// @brief Encode channel advertisements to JSON string.
///
/// @param channels List of channel advertisements.
/// @return JSON string on success, error on failure.
FoxgloveResult<std::string> encode_advertise(
    const std::vector<ChannelAdvertisement>& channels);

/// @brief Encode message data to binary frame.
///
/// Binary format: opcode(1) | subscription_id(4) | log_time(8) | data(N)
/// All integers are little-endian.
///
/// @param msg Message data to encode.
/// @return Binary frame on success, error on failure.
FoxgloveResult<std::vector<uint8_t>> encode_message_data(const MessageData& msg);

/// @brief Decode client message from JSON string.
///
/// @param json JSON string to decode.
/// @return Client message on success, error on failure.
FoxgloveResult<ClientMessage> decode_client_message(const std::string& json);

/// @brief Decode message data from binary frame.
///
/// @param data Binary frame to decode.
/// @return Message data on success, error on failure.
FoxgloveResult<MessageData> decode_message_data_binary(
    const std::vector<uint8_t>& data);

}  // namespace foxglove
