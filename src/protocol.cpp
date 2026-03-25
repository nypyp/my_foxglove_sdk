#include <foxglove/protocol.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>

namespace foxglove {

// Binary opcodes for server messages
constexpr uint8_t OPCODE_MESSAGE_DATA = 0x01;

FoxgloveResult<std::string> encode_server_info(const ServerInfo& info) {
  try {
    nlohmann::json json;
    json["op"] = "serverInfo";
    json["name"] = info.name;
    json["capabilities"] = info.capabilities;

    if (!info.supported_encodings.empty()) {
      json["supportedEncodings"] = info.supported_encodings;
    }

    if (!info.metadata.empty()) {
      json["metadata"] = info.metadata;
    }

    if (!info.session_id.empty()) {
      json["sessionId"] = info.session_id;
    }

    return json.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

FoxgloveResult<std::string> encode_advertise(
    const std::vector<ChannelAdvertisement>& channels) {
  try {
    nlohmann::json json;
    json["op"] = "advertise";

    nlohmann::json channels_json = nlohmann::json::array();
    for (const auto& ch : channels) {
      nlohmann::json ch_json;
      ch_json["id"] = ch.id;
      ch_json["topic"] = ch.topic;
      ch_json["encoding"] = ch.encoding;
      ch_json["schemaName"] = ch.schema_name;

      if (!ch.schema_encoding.empty()) {
        ch_json["schemaEncoding"] = ch.schema_encoding;
      }

      if (!ch.schema_data.empty()) {
        ch_json["schema"] = ch.schema_data;
      }

      channels_json.push_back(ch_json);
    }
    json["channels"] = channels_json;

    return json.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

FoxgloveResult<std::vector<uint8_t>> encode_message_data(const MessageData& msg) {
  try {
    // Binary format: opcode(1) | subscription_id(4) | log_time(8) | data(N)
    // All integers are little-endian
    size_t total_size = 1 + 4 + 8 + msg.data.size();
    std::vector<uint8_t> result;
    result.reserve(total_size);

    // Opcode
    result.push_back(OPCODE_MESSAGE_DATA);

    // Subscription ID (little-endian)
    result.push_back(static_cast<uint8_t>(msg.subscription_id & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 24) & 0xFF));

    // Log time (little-endian, 8 bytes)
    for (size_t i = 0; i < 8; ++i) {
      result.push_back(static_cast<uint8_t>((msg.log_time >> (i * 8)) & 0xFF));
    }

    // Data payload
    result.insert(result.end(), msg.data.begin(), msg.data.end());

    return result;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

FoxgloveResult<ClientMessage> decode_client_message(const std::string& json_str) {
  try {
    auto json = nlohmann::json::parse(json_str);

    if (!json.contains("op") || !json["op"].is_string()) {
      return tl::make_unexpected(FoxgloveError::ProtocolError);
    }

    std::string op = json["op"];

    if (op == "subscribe") {
      if (!json.contains("subscriptions") || !json["subscriptions"].is_array()) {
        return tl::make_unexpected(FoxgloveError::ProtocolError);
      }

      Subscribe subscribe;
      for (const auto& sub_json : json["subscriptions"]) {
        if (!sub_json.contains("id") || !sub_json.contains("channelId")) {
          return tl::make_unexpected(FoxgloveError::ProtocolError);
        }
        Subscription sub;
        sub.subscription_id = sub_json["id"].get<uint32_t>();
        sub.channel_id = sub_json["channelId"].get<uint32_t>();
        subscribe.subscriptions.push_back(sub);
      }
      return subscribe;

    } else if (op == "unsubscribe") {
      if (!json.contains("subscriptionIds") || !json["subscriptionIds"].is_array()) {
        return tl::make_unexpected(FoxgloveError::ProtocolError);
      }

      Unsubscribe unsubscribe;
      for (const auto& id_json : json["subscriptionIds"]) {
        unsubscribe.subscription_ids.push_back(id_json.get<uint32_t>());
      }
      return unsubscribe;

    } else {
      return tl::make_unexpected(FoxgloveError::ProtocolError);
    }
  } catch (const nlohmann::json::exception&) {
    return tl::make_unexpected(FoxgloveError::ProtocolError);
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::ProtocolError);
  }
}

FoxgloveResult<MessageData> decode_message_data_binary(
    const std::vector<uint8_t>& data) {
  // Minimum size: opcode(1) + subscription_id(4) + log_time(8) = 13 bytes
  if (data.size() < 13) {
    return tl::make_unexpected(FoxgloveError::ProtocolError);
  }

  // Check opcode
  if (data[0] != OPCODE_MESSAGE_DATA) {
    return tl::make_unexpected(FoxgloveError::ProtocolError);
  }

  MessageData msg;

  // Parse subscription ID (little-endian)
  msg.subscription_id = static_cast<uint32_t>(data[1]) |
                        (static_cast<uint32_t>(data[2]) << 8) |
                        (static_cast<uint32_t>(data[3]) << 16) |
                        (static_cast<uint32_t>(data[4]) << 24);

  // Parse log time (little-endian, 8 bytes)
  msg.log_time = 0;
  for (size_t i = 0; i < 8; ++i) {
    msg.log_time |= static_cast<uint64_t>(data[5 + i]) << (i * 8);
  }

  // Parse payload (remaining bytes)
  msg.data.assign(data.begin() + 13, data.end());

  return msg;
}

}  // namespace foxglove
