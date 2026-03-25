#include <foxglove/messages.hpp>

namespace foxglove {

// Timestamp
nlohmann::json Timestamp::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"nsec", {{"type", "integer"}, {"minimum", 0}}},
      {"sec", {{"type", "integer"}, {"minimum", 0}}}}},
    {"required", {"nsec", "sec"}}  // alphabetical
  };
}

void to_json(nlohmann::json& j, const Timestamp& ts) {
  j = nlohmann::json{{"nsec", ts.nsec}, {"sec", ts.sec}};  // alphabetical
}

void from_json(const nlohmann::json& j, Timestamp& ts) {
  j.at("nsec").get_to(ts.nsec);
  j.at("sec").get_to(ts.sec);
}

// Duration
nlohmann::json Duration::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"nsec", {{"type", "integer"}, {"minimum", 0}}}, {"sec", {{"type", "integer"}}}}},
    {"required", {"nsec", "sec"}}  // alphabetical
  };
}

void to_json(nlohmann::json& j, const Duration& dur) {
  j = nlohmann::json{{"nsec", dur.nsec}, {"sec", dur.sec}};  // alphabetical
}

void from_json(const nlohmann::json& j, Duration& dur) {
  j.at("nsec").get_to(dur.nsec);
  j.at("sec").get_to(dur.sec);
}

// Vector3
nlohmann::json Vector3::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}, {"z", {{"type", "number"}}}}},
    {"required", {"x", "y", "z"}}  // alphabetical
  };
}

void to_json(nlohmann::json& j, const Vector3& vec) {
  j = nlohmann::json{
      {"x", detail::double_to_json(vec.x)},
      {"y", detail::double_to_json(vec.y)},
      {"z", detail::double_to_json(vec.z)}};
}

void from_json(const nlohmann::json& j, Vector3& vec) {
  vec.x = detail::double_from_json(j.at("x"));
  vec.y = detail::double_from_json(j.at("y"));
  vec.z = detail::double_from_json(j.at("z"));
}

// Quaternion
nlohmann::json Quaternion::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"w", {{"type", "number"}}},
      {"x", {{"type", "number"}}},
      {"y", {{"type", "number"}}},
      {"z", {{"type", "number"}}}}},
    {"required", {"w", "x", "y", "z"}}  // alphabetical
  };
}

void to_json(nlohmann::json& j, const Quaternion& quat) {
  j = nlohmann::json{
      {"w", detail::double_to_json(quat.w)},
      {"x", detail::double_to_json(quat.x)},
      {"y", detail::double_to_json(quat.y)},
      {"z", detail::double_to_json(quat.z)}};
}

void from_json(const nlohmann::json& j, Quaternion& quat) {
  quat.w = detail::double_from_json(j.at("w"));
  quat.x = detail::double_from_json(j.at("x"));
  quat.y = detail::double_from_json(j.at("y"));
  quat.z = detail::double_from_json(j.at("z"));
}

// Pose
nlohmann::json Pose::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"orientation", Quaternion::json_schema()}, {"position", Vector3::json_schema()}}},
    {"required", {"orientation", "position"}}  // alphabetical
  };
}

void to_json(nlohmann::json& j, const Pose& pose) {
  j = nlohmann::json{{"orientation", pose.orientation}, {"position", pose.position}};  // alphabetical
}

void from_json(const nlohmann::json& j, Pose& pose) {
  j.at("orientation").get_to(pose.orientation);
  j.at("position").get_to(pose.position);
}

// Color
nlohmann::json Color::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"a", {{"type", "number"}}},
      {"b", {{"type", "number"}}},
      {"g", {{"type", "number"}}},
      {"r", {{"type", "number"}}}}},
    {"required", {"a", "b", "g", "r"}}  // alphabetical
  };
}

void to_json(nlohmann::json& j, const Color& color) {
  j = nlohmann::json{
      {"a", detail::double_to_json(color.a)},
      {"b", detail::double_to_json(color.b)},
      {"g", detail::double_to_json(color.g)},
      {"r", detail::double_to_json(color.r)}};
}

void from_json(const nlohmann::json& j, Color& color) {
  color.a = detail::double_from_json(j.at("a"));
  color.b = detail::double_from_json(j.at("b"));
  color.g = detail::double_from_json(j.at("g"));
  color.r = detail::double_from_json(j.at("r"));
}

// Template specializations for encode/decode
template <>
FoxgloveResult<std::string> encode<Timestamp>(const Timestamp& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<Timestamp> decode<Timestamp>(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    Timestamp msg = j.get<Timestamp>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<std::string> encode<Duration>(const Duration& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<Duration> decode<Duration>(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    Duration msg = j.get<Duration>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<std::string> encode<Vector3>(const Vector3& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<Vector3> decode<Vector3>(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    Vector3 msg = j.get<Vector3>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<std::string> encode<Quaternion>(const Quaternion& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<Quaternion> decode<Quaternion>(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    Quaternion msg = j.get<Quaternion>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<std::string> encode<Pose>(const Pose& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<Pose> decode<Pose>(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    Pose msg = j.get<Pose>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<std::string> encode<Color>(const Color& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template <>
FoxgloveResult<Color> decode<Color>(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    Color msg = j.get<Color>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

}  // namespace foxglove
