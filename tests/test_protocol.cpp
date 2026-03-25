/// @brief Unit tests for Foxglove WebSocket Protocol encode/decode.

#include <foxglove/protocol.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace foxglove;

TEST_CASE("Protocol - serverInfo encodes to JSON") {
  ServerInfo info;
  info.name = "Test Server";
  info.capabilities = 3;  // ClientPublish | ConnectionGraph
  info.supported_encodings = {"json", "protobuf"};
  info.metadata = {{"version", "1.0.0"}, {"environment", "test"}};
  info.session_id = "test-session-123";

  auto json_str = encode_server_info(info);
  REQUIRE(json_str.has_value());

  auto json = nlohmann::json::parse(json_str.value());
  REQUIRE(json["op"] == "serverInfo");
  REQUIRE(json["name"] == "Test Server");
  REQUIRE(json["capabilities"] == 3);
  REQUIRE(json["supportedEncodings"].size() == 2);
  REQUIRE(json["supportedEncodings"][0] == "json");
  REQUIRE(json["supportedEncodings"][1] == "protobuf");
  REQUIRE(json["metadata"]["version"] == "1.0.0");
  REQUIRE(json["metadata"]["environment"] == "test");
  REQUIRE(json["sessionId"] == "test-session-123");
}

TEST_CASE("Protocol - advertise encodes to JSON") {
  std::vector<ChannelAdvertisement> channels;
  ChannelAdvertisement ch1;
  ch1.id = 1;
  ch1.topic = "/robot/cmd_vel";
  ch1.encoding = "json";
  ch1.schema_name = "geometry_msgs/Twist";
  ch1.schema_encoding = "jsonschema";
  ch1.schema_data = "{\"type\": \"object\"}";
  channels.push_back(ch1);

  ChannelAdvertisement ch2;
  ch2.id = 2;
  ch2.topic = "/camera/image";
  ch2.encoding = "protobuf";
  ch2.schema_name = "sensor_msgs/Image";
  ch2.schema_encoding = "protobuf";
  ch2.schema_data = "binary_schema_data_here";
  channels.push_back(ch2);

  auto json_str = encode_advertise(channels);
  REQUIRE(json_str.has_value());

  auto json = nlohmann::json::parse(json_str.value());
  REQUIRE(json["op"] == "advertise");
  REQUIRE(json["channels"].size() == 2);

  REQUIRE(json["channels"][0]["id"] == 1);
  REQUIRE(json["channels"][0]["topic"] == "/robot/cmd_vel");
  REQUIRE(json["channels"][0]["encoding"] == "json");
  REQUIRE(json["channels"][0]["schemaName"] == "geometry_msgs/Twist");
  REQUIRE(json["channels"][0]["schemaEncoding"] == "jsonschema");
  REQUIRE(json["channels"][0]["schema"] == "{\"type\": \"object\"}");

  REQUIRE(json["channels"][1]["id"] == 2);
  REQUIRE(json["channels"][1]["topic"] == "/camera/image");
}

TEST_CASE("Protocol - subscribe decodes from JSON") {
  std::string json_str = R"({
    "op": "subscribe",
    "subscriptions": [
      {"id": 0, "channelId": 3},
      {"id": 1, "channelId": 5}
    ]
  })";

  auto result = decode_client_message(json_str);
  REQUIRE(result.has_value());

  auto* subscribe = std::get_if<Subscribe>(&result.value());
  REQUIRE(subscribe != nullptr);
  REQUIRE(subscribe->subscriptions.size() == 2);
  REQUIRE(subscribe->subscriptions[0].subscription_id == 0);
  REQUIRE(subscribe->subscriptions[0].channel_id == 3);
  REQUIRE(subscribe->subscriptions[1].subscription_id == 1);
  REQUIRE(subscribe->subscriptions[1].channel_id == 5);
}

TEST_CASE("Protocol - unsubscribe decodes from JSON") {
  std::string json_str = R"({
    "op": "unsubscribe",
    "subscriptionIds": [0, 1, 2]
  })";

  auto result = decode_client_message(json_str);
  REQUIRE(result.has_value());

  auto* unsubscribe = std::get_if<Unsubscribe>(&result.value());
  REQUIRE(unsubscribe != nullptr);
  REQUIRE(unsubscribe->subscription_ids.size() == 3);
  REQUIRE(unsubscribe->subscription_ids[0] == 0);
  REQUIRE(unsubscribe->subscription_ids[1] == 1);
  REQUIRE(unsubscribe->subscription_ids[2] == 2);
}

TEST_CASE("Protocol - messageData binary roundtrip") {
  MessageData msg;
  msg.subscription_id = 42;
  msg.log_time = 1234567890ULL;
  msg.data = {0x01, 0x02, 0x03, 0x04, 0x05};

  // Encode
  auto encoded = encode_message_data(msg);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value().size() == 1 + 4 + 8 + 5);  // opcode + sub_id + log_time + data

  // Verify opcode
  REQUIRE(encoded.value()[0] == 0x01);

  // Decode
  auto decoded = decode_message_data_binary(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value().subscription_id == 42);
  REQUIRE(decoded.value().log_time == 1234567890ULL);
  REQUIRE(decoded.value().data.size() == 5);
  REQUIRE(decoded.value().data == std::vector<uint8_t>({0x01, 0x02, 0x03, 0x04, 0x05}));
}

TEST_CASE("Protocol - malformed JSON returns error") {
  std::string malformed = "{not valid json";
  auto result = decode_client_message(malformed);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::ProtocolError);
}

TEST_CASE("Protocol - malformed binary returns error") {
  // Too short - less than 13 bytes (opcode + subscription_id + log_time)
  std::vector<uint8_t> truncated = {0x01, 0x01, 0x00, 0x00, 0x00};  // opcode + 4 bytes, missing log_time
  auto result = decode_message_data_binary(truncated);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::ProtocolError);

  // Wrong opcode
  std::vector<uint8_t> wrong_opcode = {0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  result = decode_message_data_binary(wrong_opcode);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::ProtocolError);
}
