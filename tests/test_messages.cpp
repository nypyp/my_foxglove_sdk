/// @brief Unit tests for Foxglove message serialization.

#include <foxglove/messages.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace foxglove;

// Helper to read golden file
std::string read_golden_file(const std::string& filename) {
  std::ifstream file(filename);
  REQUIRE(file.is_open());
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  // Trim trailing whitespace/newline
  while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
    content.pop_back();
  }
  return content;
}

// Helper to get golden file path
std::string golden_path(const std::string& filename) {
  return std::string(FOXGLOVE_TEST_GOLDEN_DIR) + "/" + filename;
}

TEST_CASE("Timestamp - roundtrip serialization") {
  Timestamp ts;
  ts.sec = 1234567890;
  ts.nsec = 123456789;

  auto encoded = encode(ts);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Timestamp>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().sec == ts.sec);
  REQUIRE(decoded.value().nsec == ts.nsec);
}

TEST_CASE("Timestamp - golden file match") {
  Timestamp ts;
  ts.sec = 1234567890;
  ts.nsec = 123456789;

  auto encoded = encode(ts);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("timestamp.json"));
  REQUIRE(encoded.value() == expected);
}

TEST_CASE("Duration - roundtrip serialization") {
  Duration dur;
  dur.sec = -5;
  dur.nsec = 500000000;

  auto encoded = encode(dur);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Duration>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().sec == dur.sec);
  REQUIRE(decoded.value().nsec == dur.nsec);
}

TEST_CASE("Duration - golden file match") {
  Duration dur;
  dur.sec = 1234567890;
  dur.nsec = 123456789;

  auto encoded = encode(dur);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("duration.json"));
  REQUIRE(encoded.value() == expected);
}

TEST_CASE("Vector3 - roundtrip serialization") {
  Vector3 vec;
  vec.x = 1.0;
  vec.y = 2.0;
  vec.z = 3.0;

  auto encoded = encode(vec);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Vector3>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().x == vec.x);
  REQUIRE(decoded.value().y == vec.y);
  REQUIRE(decoded.value().z == vec.z);
}

TEST_CASE("Vector3 - golden file match") {
  Vector3 vec;
  vec.x = 1.0;
  vec.y = 2.0;
  vec.z = 3.0;

  auto encoded = encode(vec);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("vector3.json"));
  REQUIRE(encoded.value() == expected);
}

TEST_CASE("Quaternion - roundtrip serialization") {
  Quaternion quat;
  quat.x = 0.0;
  quat.y = 0.0;
  quat.z = 0.0;
  quat.w = 1.0;

  auto encoded = encode(quat);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Quaternion>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().x == quat.x);
  REQUIRE(decoded.value().y == quat.y);
  REQUIRE(decoded.value().z == quat.z);
  REQUIRE(decoded.value().w == quat.w);
}

TEST_CASE("Quaternion - golden file match") {
  Quaternion quat;
  quat.x = 0.0;
  quat.y = 0.0;
  quat.z = 0.0;
  quat.w = 1.0;

  auto encoded = encode(quat);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("quaternion.json"));
  REQUIRE(encoded.value() == expected);
}

TEST_CASE("Pose - roundtrip serialization") {
  Pose pose;
  pose.position.x = 1.0;
  pose.position.y = 2.0;
  pose.position.z = 3.0;
  pose.orientation.x = 0.0;
  pose.orientation.y = 0.0;
  pose.orientation.z = 0.0;
  pose.orientation.w = 1.0;

  auto encoded = encode(pose);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Pose>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().position.x == pose.position.x);
  REQUIRE(decoded.value().position.y == pose.position.y);
  REQUIRE(decoded.value().position.z == pose.position.z);
  REQUIRE(decoded.value().orientation.x == pose.orientation.x);
  REQUIRE(decoded.value().orientation.y == pose.orientation.y);
  REQUIRE(decoded.value().orientation.z == pose.orientation.z);
  REQUIRE(decoded.value().orientation.w == pose.orientation.w);
}

TEST_CASE("Pose - golden file match") {
  Pose pose;
  pose.position.x = 1.0;
  pose.position.y = 2.0;
  pose.position.z = 3.0;
  pose.orientation.x = 0.0;
  pose.orientation.y = 0.0;
  pose.orientation.z = 0.0;
  pose.orientation.w = 1.0;

  auto encoded = encode(pose);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("pose.json"));
  REQUIRE(encoded.value() == expected);
}

TEST_CASE("Color - roundtrip serialization") {
  Color color;
  color.r = 1.0;
  color.g = 1.0;
  color.b = 0.0;
  color.a = 1.0;

  auto encoded = encode(color);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Color>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().r == color.r);
  REQUIRE(decoded.value().g == color.g);
  REQUIRE(decoded.value().b == color.b);
  REQUIRE(decoded.value().a == color.a);
}

TEST_CASE("Color - golden file match") {
  Color color;
  color.r = 1.0;
  color.g = 1.0;
  color.b = 0.0;
  color.a = 1.0;

  auto encoded = encode(color);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("color.json"));
  REQUIRE(encoded.value() == expected);
}

TEST_CASE("NaN handling - serializes to null") {
  Vector3 vec;
  vec.x = std::nan("");
  vec.y = 2.0;
  vec.z = 3.0;

  auto encoded = encode(vec);
  REQUIRE(encoded.has_value());

  // NaN should serialize as null
  REQUIRE(encoded.value().find("null") != std::string::npos);
}

TEST_CASE("NaN handling - deserializes null back to NaN") {
  std::string json = R"({"x":null,"y":2.0,"z":3.0})";

  auto decoded = decode<Vector3>(json);
  REQUIRE(decoded.has_value());

  REQUIRE(std::isnan(decoded.value().x));
  REQUIRE(decoded.value().y == 2.0);
  REQUIRE(decoded.value().z == 3.0);
}

TEST_CASE("Default values - Timestamp") {
  Timestamp ts;
  REQUIRE(ts.sec == 0);
  REQUIRE(ts.nsec == 0);
}

TEST_CASE("Default values - Duration") {
  Duration dur;
  REQUIRE(dur.sec == 0);
  REQUIRE(dur.nsec == 0);
}

TEST_CASE("Default values - Vector3") {
  Vector3 vec;
  REQUIRE(vec.x == 0.0);
  REQUIRE(vec.y == 0.0);
  REQUIRE(vec.z == 0.0);
}

TEST_CASE("Default values - Quaternion") {
  Quaternion quat;
  REQUIRE(quat.x == 0.0);
  REQUIRE(quat.y == 0.0);
  REQUIRE(quat.z == 0.0);
  REQUIRE(quat.w == 1.0);  // Identity quaternion convention
}

TEST_CASE("Default values - Pose") {
  Pose pose;
  REQUIRE(pose.position.x == 0.0);
  REQUIRE(pose.position.y == 0.0);
  REQUIRE(pose.position.z == 0.0);
  REQUIRE(pose.orientation.x == 0.0);
  REQUIRE(pose.orientation.y == 0.0);
  REQUIRE(pose.orientation.z == 0.0);
  REQUIRE(pose.orientation.w == 1.0);
}

TEST_CASE("Default values - Color") {
  Color color;
  REQUIRE(color.r == 0.0);
  REQUIRE(color.g == 0.0);
  REQUIRE(color.b == 0.0);
  REQUIRE(color.a == 0.0);
}

TEST_CASE("JsonSchema - Timestamp returns valid schema") {
  auto schema = Timestamp::json_schema();
  REQUIRE(schema["type"] == "object");
  REQUIRE(schema["properties"].contains("sec"));
  REQUIRE(schema["properties"].contains("nsec"));
  REQUIRE(schema["required"].size() == 2);
}

TEST_CASE("JsonSchema - Duration returns valid schema") {
  auto schema = Duration::json_schema();
  REQUIRE(schema["type"] == "object");
  REQUIRE(schema["properties"].contains("sec"));
  REQUIRE(schema["properties"].contains("nsec"));
  REQUIRE(schema["required"].size() == 2);
}

TEST_CASE("JsonSchema - Vector3 returns valid schema") {
  auto schema = Vector3::json_schema();
  REQUIRE(schema["type"] == "object");
  REQUIRE(schema["properties"].contains("x"));
  REQUIRE(schema["properties"].contains("y"));
  REQUIRE(schema["properties"].contains("z"));
  REQUIRE(schema["required"].size() == 3);
}

TEST_CASE("JsonSchema - Quaternion returns valid schema") {
  auto schema = Quaternion::json_schema();
  REQUIRE(schema["type"] == "object");
  REQUIRE(schema["properties"].contains("x"));
  REQUIRE(schema["properties"].contains("y"));
  REQUIRE(schema["properties"].contains("z"));
  REQUIRE(schema["properties"].contains("w"));
  REQUIRE(schema["required"].size() == 4);
}

TEST_CASE("JsonSchema - Pose returns valid schema") {
  auto schema = Pose::json_schema();
  REQUIRE(schema["type"] == "object");
  REQUIRE(schema["properties"].contains("position"));
  REQUIRE(schema["properties"].contains("orientation"));
  REQUIRE(schema["required"].size() == 2);
}

TEST_CASE("JsonSchema - Color returns valid schema") {
  auto schema = Color::json_schema();
  REQUIRE(schema["type"] == "object");
  REQUIRE(schema["properties"].contains("r"));
  REQUIRE(schema["properties"].contains("g"));
  REQUIRE(schema["properties"].contains("b"));
  REQUIRE(schema["properties"].contains("a"));
  REQUIRE(schema["required"].size() == 4);
}

TEST_CASE("Decode - invalid JSON returns error") {
  std::string invalid_json = "{invalid json";
  auto result = decode<Vector3>(invalid_json);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::SerializationError);
}

TEST_CASE("Decode - missing required field returns error") {
  std::string incomplete_json = R"({"x":1.0,"y":2.0})";  // missing z
  auto result = decode<Vector3>(incomplete_json);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::SerializationError);
}

TEST_CASE("Operator== - Vector3 equality") {
  Vector3 a{1.0, 2.0, 3.0};
  Vector3 b{1.0, 2.0, 3.0};
  Vector3 c{1.0, 2.0, 4.0};

  REQUIRE(a == b);
  REQUIRE(!(a == c));
}

TEST_CASE("Operator== - Quaternion equality") {
  Quaternion a{0.0, 0.0, 0.0, 1.0};
  Quaternion b{0.0, 0.0, 0.0, 1.0};
  Quaternion c{0.0, 0.0, 0.0, 0.5};

  REQUIRE(a == b);
  REQUIRE(!(a == c));
}

TEST_CASE("Operator== - Pose equality") {
  Pose a{{1.0, 2.0, 3.0}, {0.0, 0.0, 0.0, 1.0}};
  Pose b{{1.0, 2.0, 3.0}, {0.0, 0.0, 0.0, 1.0}};
  Pose c{{1.0, 2.0, 4.0}, {0.0, 0.0, 0.0, 1.0}};

  REQUIRE(a == b);
  REQUIRE(!(a == c));
}

TEST_CASE("Operator== - Color equality") {
  Color a{1.0, 0.5, 0.0, 1.0};
  Color b{1.0, 0.5, 0.0, 1.0};
  Color c{1.0, 0.5, 0.1, 1.0};

  REQUIRE(a == b);
  REQUIRE(!(a == c));
}
