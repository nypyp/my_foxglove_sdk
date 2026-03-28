#include <foxglove/messages.hpp>

#include <array>
#include <stdexcept>

namespace foxglove {

namespace {

std::string encode_base64(const std::vector<uint8_t>& data) {
  static constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string output;
  output.reserve(((data.size() + 2U) / 3U) * 4U);

  size_t index = 0;
  while (index + 3U <= data.size()) {
    const uint32_t chunk = (static_cast<uint32_t>(data[index]) << 16U) |
                           (static_cast<uint32_t>(data[index + 1U]) << 8U) |
                           static_cast<uint32_t>(data[index + 2U]);
    output.push_back(kEncodeTable[(chunk >> 18U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 12U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 6U) & 0x3FU]);
    output.push_back(kEncodeTable[chunk & 0x3FU]);
    index += 3U;
  }

  const size_t remain = data.size() - index;
  if (remain == 1U) {
    const uint32_t chunk = static_cast<uint32_t>(data[index]) << 16U;
    output.push_back(kEncodeTable[(chunk >> 18U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 12U) & 0x3FU]);
    output.push_back('=');
    output.push_back('=');
  } else if (remain == 2U) {
    const uint32_t chunk =
      (static_cast<uint32_t>(data[index]) << 16U) | (static_cast<uint32_t>(data[index + 1U]) << 8U);
    output.push_back(kEncodeTable[(chunk >> 18U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 12U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 6U) & 0x3FU]);
    output.push_back('=');
  }

  return output;
}

FoxgloveResult<std::vector<uint8_t>> decode_base64(const std::string& text) {
  static const std::array<int8_t, 256> kDecodeTable = [] {
    std::array<int8_t, 256> table{};
    table.fill(-1);
    for (int value = 0; value < 26; ++value) {
      table[static_cast<size_t>('A' + value)] = static_cast<int8_t>(value);
      table[static_cast<size_t>('a' + value)] = static_cast<int8_t>(26 + value);
    }
    for (int value = 0; value < 10; ++value) {
      table[static_cast<size_t>('0' + value)] = static_cast<int8_t>(52 + value);
    }
    table[static_cast<size_t>('+')] = 62;
    table[static_cast<size_t>('/')] = 63;
    return table;
  }();

  if (text.size() % 4U != 0U) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }

  size_t padding = 0;
  if (!text.empty() && text.back() == '=') {
    padding = 1;
    if (text.size() >= 2U && text[text.size() - 2U] == '=') {
      padding = 2;
    }
  }

  std::vector<uint8_t> output;
  output.reserve((text.size() / 4U) * 3U - padding);

  for (size_t i = 0; i < text.size(); i += 4U) {
    const char c0 = text[i];
    const char c1 = text[i + 1U];
    const char c2 = text[i + 2U];
    const char c3 = text[i + 3U];

    if (c0 == '=' || c1 == '=') {
      return tl::make_unexpected(FoxgloveError::SerializationError);
    }

    const int8_t v0 = kDecodeTable[static_cast<uint8_t>(c0)];
    const int8_t v1 = kDecodeTable[static_cast<uint8_t>(c1)];
    if (v0 < 0 || v1 < 0) {
      return tl::make_unexpected(FoxgloveError::SerializationError);
    }

    int8_t v2 = 0;
    int8_t v3 = 0;
    const bool pad2 = (c2 == '=');
    const bool pad3 = (c3 == '=');

    if (pad2 && !pad3) {
      return tl::make_unexpected(FoxgloveError::SerializationError);
    }

    if (!pad2) {
      v2 = kDecodeTable[static_cast<uint8_t>(c2)];
      if (v2 < 0) {
        return tl::make_unexpected(FoxgloveError::SerializationError);
      }
    }
    if (!pad3) {
      v3 = kDecodeTable[static_cast<uint8_t>(c3)];
      if (v3 < 0) {
        return tl::make_unexpected(FoxgloveError::SerializationError);
      }
    }

    const uint32_t chunk = (static_cast<uint32_t>(v0) << 18U) | (static_cast<uint32_t>(v1) << 12U) |
                           (static_cast<uint32_t>(v2) << 6U) | static_cast<uint32_t>(v3);

    output.push_back(static_cast<uint8_t>((chunk >> 16U) & 0xFFU));
    if (!pad2) {
      output.push_back(static_cast<uint8_t>((chunk >> 8U) & 0xFFU));
      if (!pad3) {
        output.push_back(static_cast<uint8_t>(chunk & 0xFFU));
      }
    }
  }

  return output;
}

template<typename T>
FoxgloveResult<std::string> encode_impl(const T& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template<typename T>
FoxgloveResult<T> decode_impl(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    T msg = j.get<T>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

}  // namespace

nlohmann::json Timestamp::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"nsec", {{"type", "integer"}, {"minimum", 0}}},
      {"sec", {{"type", "integer"}, {"minimum", 0}}}}},
    {"required", {"nsec", "sec"}}
  };
}

void to_json(nlohmann::json& j, const Timestamp& ts) {
  j = nlohmann::json{{"nsec", ts.nsec}, {"sec", ts.sec}};
}

void from_json(const nlohmann::json& j, Timestamp& ts) {
  j.at("nsec").get_to(ts.nsec);
  j.at("sec").get_to(ts.sec);
}

nlohmann::json Duration::json_schema() {
  return {
    {"type", "object"},
    {"properties", {{"nsec", {{"type", "integer"}, {"minimum", 0}}}, {"sec", {{"type", "integer"}}}}
    },
    {"required", {"nsec", "sec"}}
  };
}

void to_json(nlohmann::json& j, const Duration& dur) {
  j = nlohmann::json{{"nsec", dur.nsec}, {"sec", dur.sec}};
}

void from_json(const nlohmann::json& j, Duration& dur) {
  j.at("nsec").get_to(dur.nsec);
  j.at("sec").get_to(dur.sec);
}

nlohmann::json Vector3::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}, {"z", {{"type", "number"}}}}},
    {"required", {"x", "y", "z"}}
  };
}

void to_json(nlohmann::json& j, const Vector3& vec) {
  j = nlohmann::json{
    {"x", detail::double_to_json(vec.x)},
    {"y", detail::double_to_json(vec.y)},
    {"z", detail::double_to_json(vec.z)}
  };
}

void from_json(const nlohmann::json& j, Vector3& vec) {
  vec.x = detail::double_from_json(j.at("x"));
  vec.y = detail::double_from_json(j.at("y"));
  vec.z = detail::double_from_json(j.at("z"));
}

nlohmann::json Quaternion::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"w", {{"type", "number"}}},
      {"x", {{"type", "number"}}},
      {"y", {{"type", "number"}}},
      {"z", {{"type", "number"}}}}},
    {"required", {"w", "x", "y", "z"}}
  };
}

void to_json(nlohmann::json& j, const Quaternion& quat) {
  j = nlohmann::json{
    {"w", detail::double_to_json(quat.w)},
    {"x", detail::double_to_json(quat.x)},
    {"y", detail::double_to_json(quat.y)},
    {"z", detail::double_to_json(quat.z)}
  };
}

void from_json(const nlohmann::json& j, Quaternion& quat) {
  quat.w = detail::double_from_json(j.at("w"));
  quat.x = detail::double_from_json(j.at("x"));
  quat.y = detail::double_from_json(j.at("y"));
  quat.z = detail::double_from_json(j.at("z"));
}

nlohmann::json Pose::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"orientation", Quaternion::json_schema()}, {"position", Vector3::json_schema()}}},
    {"required", {"orientation", "position"}}
  };
}

void to_json(nlohmann::json& j, const Pose& pose) {
  j = nlohmann::json{{"orientation", pose.orientation}, {"position", pose.position}};
}

void from_json(const nlohmann::json& j, Pose& pose) {
  j.at("orientation").get_to(pose.orientation);
  j.at("position").get_to(pose.position);
}

nlohmann::json Color::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"a", {{"type", "number"}}},
      {"b", {{"type", "number"}}},
      {"g", {{"type", "number"}}},
      {"r", {{"type", "number"}}}}},
    {"required", {"a", "b", "g", "r"}}
  };
}

void to_json(nlohmann::json& j, const Color& color) {
  j = nlohmann::json{
    {"a", detail::double_to_json(color.a)},
    {"b", detail::double_to_json(color.b)},
    {"g", detail::double_to_json(color.g)},
    {"r", detail::double_to_json(color.r)}
  };
}

void from_json(const nlohmann::json& j, Color& color) {
  color.a = detail::double_from_json(j.at("a"));
  color.b = detail::double_from_json(j.at("b"));
  color.g = detail::double_from_json(j.at("g"));
  color.r = detail::double_from_json(j.at("r"));
}

nlohmann::json Point3::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}, {"z", {{"type", "number"}}}}},
    {"required", {"x", "y", "z"}}
  };
}

void to_json(nlohmann::json& j, const Point3& point) {
  j = nlohmann::json{
    {"x", detail::double_to_json(point.x)},
    {"y", detail::double_to_json(point.y)},
    {"z", detail::double_to_json(point.z)}
  };
}

void from_json(const nlohmann::json& j, Point3& point) {
  point.x = detail::double_from_json(j.at("x"));
  point.y = detail::double_from_json(j.at("y"));
  point.z = detail::double_from_json(j.at("z"));
}

nlohmann::json ArrowPrimitive::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"color", Color::json_schema()},
      {"head_diameter", {{"type", "number"}}},
      {"head_length", {{"type", "number"}}},
      {"pose", Pose::json_schema()},
      {"shaft_diameter", {{"type", "number"}}},
      {"shaft_length", {{"type", "number"}}}}},
    {"required", {"color", "head_diameter", "head_length", "pose", "shaft_diameter", "shaft_length"}
    }
  };
}

void to_json(nlohmann::json& j, const ArrowPrimitive& primitive) {
  j = nlohmann::json{
    {"color", primitive.color},
    {"head_diameter", detail::double_to_json(primitive.head_diameter)},
    {"head_length", detail::double_to_json(primitive.head_length)},
    {"pose", primitive.pose},
    {"shaft_diameter", detail::double_to_json(primitive.shaft_diameter)},
    {"shaft_length", detail::double_to_json(primitive.shaft_length)}
  };
}

void from_json(const nlohmann::json& j, ArrowPrimitive& primitive) {
  if (j.contains("color")) {
    j.at("color").get_to(primitive.color);
  }
  if (j.contains("head_diameter")) {
    primitive.head_diameter = detail::double_from_json(j.at("head_diameter"));
  }
  if (j.contains("head_length")) {
    primitive.head_length = detail::double_from_json(j.at("head_length"));
  }
  if (j.contains("pose")) {
    j.at("pose").get_to(primitive.pose);
  }
  if (j.contains("shaft_diameter")) {
    primitive.shaft_diameter = detail::double_from_json(j.at("shaft_diameter"));
  }
  if (j.contains("shaft_length")) {
    primitive.shaft_length = detail::double_from_json(j.at("shaft_length"));
  }
}

nlohmann::json CubePrimitive::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"color", Color::json_schema()},
      {"pose", Pose::json_schema()},
      {"size", Vector3::json_schema()}}},
    {"required", {"color", "pose", "size"}}
  };
}

void to_json(nlohmann::json& j, const CubePrimitive& primitive) {
  j =
    nlohmann::json{{"color", primitive.color}, {"pose", primitive.pose}, {"size", primitive.size}};
}

void from_json(const nlohmann::json& j, CubePrimitive& primitive) {
  if (j.contains("color")) {
    j.at("color").get_to(primitive.color);
  }
  if (j.contains("pose")) {
    j.at("pose").get_to(primitive.pose);
  }
  if (j.contains("size")) {
    j.at("size").get_to(primitive.size);
  }
}

nlohmann::json SpherePrimitive::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"color", Color::json_schema()},
      {"pose", Pose::json_schema()},
      {"size", Vector3::json_schema()}}},
    {"required", {"color", "pose", "size"}}
  };
}

void to_json(nlohmann::json& j, const SpherePrimitive& primitive) {
  j =
    nlohmann::json{{"color", primitive.color}, {"pose", primitive.pose}, {"size", primitive.size}};
}

void from_json(const nlohmann::json& j, SpherePrimitive& primitive) {
  if (j.contains("color")) {
    j.at("color").get_to(primitive.color);
  }
  if (j.contains("pose")) {
    j.at("pose").get_to(primitive.pose);
  }
  if (j.contains("size")) {
    j.at("size").get_to(primitive.size);
  }
}

nlohmann::json CylinderPrimitive::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"bottom_scale", {{"type", "number"}}},
      {"color", Color::json_schema()},
      {"pose", Pose::json_schema()},
      {"size", Vector3::json_schema()},
      {"top_scale", {{"type", "number"}}}}},
    {"required", {"bottom_scale", "color", "pose", "size", "top_scale"}}
  };
}

void to_json(nlohmann::json& j, const CylinderPrimitive& primitive) {
  j = nlohmann::json{
    {"bottom_scale", detail::double_to_json(primitive.bottom_scale)},
    {"color", primitive.color},
    {"pose", primitive.pose},
    {"size", primitive.size},
    {"top_scale", detail::double_to_json(primitive.top_scale)}
  };
}

void from_json(const nlohmann::json& j, CylinderPrimitive& primitive) {
  if (j.contains("bottom_scale")) {
    primitive.bottom_scale = detail::double_from_json(j.at("bottom_scale"));
  }
  if (j.contains("color")) {
    j.at("color").get_to(primitive.color);
  }
  if (j.contains("pose")) {
    j.at("pose").get_to(primitive.pose);
  }
  if (j.contains("size")) {
    j.at("size").get_to(primitive.size);
  }
  if (j.contains("top_scale")) {
    primitive.top_scale = detail::double_from_json(j.at("top_scale"));
  }
}

nlohmann::json LinePrimitive::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"color", Color::json_schema()},
      {"colors", {"type", "array", "items", Color::json_schema()}},
      {"indices", {"type", "array", "items", {{"type", "integer"}, {"minimum", 0}}}},
      {"points", {"type", "array", "items", Point3::json_schema()}},
      {"pose", Pose::json_schema()},
      {"scale_invariant", {{"type", "boolean"}}},
      {"thickness", {{"type", "number"}}},
      {"type", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}}}},
    {"required",
     {"color", "colors", "indices", "points", "pose", "scale_invariant", "thickness", "type"}}
  };
}

void to_json(nlohmann::json& j, const LinePrimitive& primitive) {
  j = nlohmann::json{
    {"color", primitive.color},
    {"colors", primitive.colors},
    {"indices", primitive.indices},
    {"points", primitive.points},
    {"pose", primitive.pose},
    {"scale_invariant", primitive.scale_invariant},
    {"thickness", detail::double_to_json(primitive.thickness)},
    {"type", static_cast<uint8_t>(primitive.type)}
  };
}

void from_json(const nlohmann::json& j, LinePrimitive& primitive) {
  if (j.contains("color")) {
    j.at("color").get_to(primitive.color);
  }
  if (j.contains("colors")) {
    j.at("colors").get_to(primitive.colors);
  }
  if (j.contains("indices")) {
    j.at("indices").get_to(primitive.indices);
  }
  if (j.contains("points")) {
    j.at("points").get_to(primitive.points);
  }
  if (j.contains("pose")) {
    j.at("pose").get_to(primitive.pose);
  }
  if (j.contains("scale_invariant")) {
    j.at("scale_invariant").get_to(primitive.scale_invariant);
  }
  if (j.contains("thickness")) {
    primitive.thickness = detail::double_from_json(j.at("thickness"));
  }
  if (j.contains("type")) {
    primitive.type = static_cast<LineType>(j.at("type").get<uint8_t>());
  }
}

nlohmann::json SceneEntityDeletion::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"id", {{"type", "string"}}},
      {"timestamp", Timestamp::json_schema()},
      {"type", {{"type", "integer"}, {"minimum", 0}, {"maximum", 1}}}}},
    {"required", {"id", "timestamp", "type"}}
  };
}

void to_json(nlohmann::json& j, const SceneEntityDeletion& deletion) {
  j = nlohmann::json{
    {"id", deletion.id},
    {"timestamp", deletion.timestamp},
    {"type", static_cast<uint8_t>(deletion.type)}
  };
}

void from_json(const nlohmann::json& j, SceneEntityDeletion& deletion) {
  if (j.contains("id")) {
    j.at("id").get_to(deletion.id);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(deletion.timestamp);
  }
  if (j.contains("type")) {
    deletion.type = static_cast<DeletionType>(j.at("type").get<uint8_t>());
  }
}

nlohmann::json KeyValuePair::json_schema() {
  return {
    {"type", "object"},
    {"properties", {{"key", {{"type", "string"}}}, {"value", {{"type", "string"}}}}},
    {"required", {"key", "value"}}
  };
}

void to_json(nlohmann::json& j, const KeyValuePair& key_value) {
  j = nlohmann::json{{"key", key_value.key}, {"value", key_value.value}};
}

void from_json(const nlohmann::json& j, KeyValuePair& key_value) {
  if (j.contains("key")) {
    j.at("key").get_to(key_value.key);
  }
  if (j.contains("value")) {
    j.at("value").get_to(key_value.value);
  }
}

nlohmann::json SceneEntity::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"arrows", {"type", "array", "items", ArrowPrimitive::json_schema()}},
      {"cubes", {"type", "array", "items", CubePrimitive::json_schema()}},
      {"cylinders", {"type", "array", "items", CylinderPrimitive::json_schema()}},
      {"frame_id", {{"type", "string"}}},
      {"frame_locked", {{"type", "boolean"}}},
      {"id", {{"type", "string"}}},
      {"lifetime", Duration::json_schema()},
      {"lines", {"type", "array", "items", LinePrimitive::json_schema()}},
      {"metadata", {"type", "array", "items", KeyValuePair::json_schema()}},
      {"spheres", {"type", "array", "items", SpherePrimitive::json_schema()}},
      {"timestamp", Timestamp::json_schema()}}},
    {"required",
     {"arrows",
      "cubes",
      "cylinders",
      "frame_id",
      "frame_locked",
      "id",
      "lifetime",
      "lines",
      "metadata",
      "spheres",
      "timestamp"}}
  };
}

void to_json(nlohmann::json& j, const SceneEntity& entity) {
  j = nlohmann::json{
    {"arrows", entity.arrows},
    {"cubes", entity.cubes},
    {"cylinders", entity.cylinders},
    {"frame_id", entity.frame_id},
    {"frame_locked", entity.frame_locked},
    {"id", entity.id},
    {"lifetime", entity.lifetime},
    {"lines", entity.lines},
    {"metadata", entity.metadata},
    {"spheres", entity.spheres},
    {"timestamp", entity.timestamp}
  };
}

void from_json(const nlohmann::json& j, SceneEntity& entity) {
  if (j.contains("arrows")) {
    j.at("arrows").get_to(entity.arrows);
  }
  if (j.contains("cubes")) {
    j.at("cubes").get_to(entity.cubes);
  }
  if (j.contains("cylinders")) {
    j.at("cylinders").get_to(entity.cylinders);
  }
  if (j.contains("frame_id")) {
    j.at("frame_id").get_to(entity.frame_id);
  }
  if (j.contains("frame_locked")) {
    j.at("frame_locked").get_to(entity.frame_locked);
  }
  if (j.contains("id")) {
    j.at("id").get_to(entity.id);
  }
  if (j.contains("lifetime")) {
    j.at("lifetime").get_to(entity.lifetime);
  }
  if (j.contains("lines")) {
    j.at("lines").get_to(entity.lines);
  }
  if (j.contains("metadata")) {
    j.at("metadata").get_to(entity.metadata);
  }
  if (j.contains("spheres")) {
    j.at("spheres").get_to(entity.spheres);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(entity.timestamp);
  }
}

nlohmann::json SceneUpdate::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"deletions", {"type", "array", "items", SceneEntityDeletion::json_schema()}},
      {"entities", {"type", "array", "items", SceneEntity::json_schema()}}}},
    {"required", {"deletions", "entities"}}
  };
}

void to_json(nlohmann::json& j, const SceneUpdate& update) {
  j = nlohmann::json{{"deletions", update.deletions}, {"entities", update.entities}};
}

void from_json(const nlohmann::json& j, SceneUpdate& update) {
  if (j.contains("deletions")) {
    j.at("deletions").get_to(update.deletions);
  }
  if (j.contains("entities")) {
    j.at("entities").get_to(update.entities);
  }
}

nlohmann::json FrameTransform::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"child_frame_id", {{"type", "string"}}},
      {"parent_frame_id", {{"type", "string"}}},
      {"rotation", Quaternion::json_schema()},
      {"timestamp", Timestamp::json_schema()},
      {"translation", Vector3::json_schema()}}},
    {"required", {"child_frame_id", "parent_frame_id", "rotation", "timestamp", "translation"}}
  };
}

void to_json(nlohmann::json& j, const FrameTransform& transform) {
  j = nlohmann::json{
    {"child_frame_id", transform.child_frame_id},
    {"parent_frame_id", transform.parent_frame_id},
    {"rotation", transform.rotation},
    {"timestamp", transform.timestamp},
    {"translation", transform.translation}
  };
}

void from_json(const nlohmann::json& j, FrameTransform& transform) {
  if (j.contains("child_frame_id")) {
    j.at("child_frame_id").get_to(transform.child_frame_id);
  }
  if (j.contains("parent_frame_id")) {
    j.at("parent_frame_id").get_to(transform.parent_frame_id);
  }
  if (j.contains("rotation")) {
    j.at("rotation").get_to(transform.rotation);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(transform.timestamp);
  }
  if (j.contains("translation")) {
    j.at("translation").get_to(transform.translation);
  }
}

nlohmann::json Log::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"file", {{"type", "string"}}},
      {"level", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
      {"line", {{"type", "integer"}, {"minimum", 0}}},
      {"message", {{"type", "string"}}},
      {"name", {{"type", "string"}}},
      {"timestamp", Timestamp::json_schema()}}},
    {"required", {"file", "level", "line", "message", "name", "timestamp"}}
  };
}

void to_json(nlohmann::json& j, const Log& log) {
  j = nlohmann::json{
    {"file", log.file},
    {"level", static_cast<uint8_t>(log.level)},
    {"line", log.line},
    {"message", log.message},
    {"name", log.name},
    {"timestamp", log.timestamp}
  };
}

void from_json(const nlohmann::json& j, Log& log) {
  if (j.contains("file")) {
    j.at("file").get_to(log.file);
  }
  if (j.contains("level")) {
    log.level = static_cast<LogLevel>(j.at("level").get<uint8_t>());
  }
  if (j.contains("line")) {
    j.at("line").get_to(log.line);
  }
  if (j.contains("message")) {
    j.at("message").get_to(log.message);
  }
  if (j.contains("name")) {
    j.at("name").get_to(log.name);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(log.timestamp);
  }
}

nlohmann::json CompressedImage::json_schema() {
  return {
    {"type", "object"},
    {"properties",
     {{"data", {{"type", "string"}}},
      {"format", {{"type", "string"}}},
      {"frame_id", {{"type", "string"}}},
      {"timestamp", Timestamp::json_schema()}}},
    {"required", {"data", "format", "frame_id", "timestamp"}}
  };
}

void to_json(nlohmann::json& j, const CompressedImage& image) {
  j = nlohmann::json{
    {"data", encode_base64(image.data)},
    {"format", image.format},
    {"frame_id", image.frame_id},
    {"timestamp", image.timestamp}
  };
}

void from_json(const nlohmann::json& j, CompressedImage& image) {
  if (j.contains("data")) {
    const auto decoded = decode_base64(j.at("data").get<std::string>());
    if (!decoded.has_value()) {
      throw std::runtime_error("invalid base64 data");
    }
    image.data = decoded.value();
  }
  if (j.contains("format")) {
    j.at("format").get_to(image.format);
  }
  if (j.contains("frame_id")) {
    j.at("frame_id").get_to(image.frame_id);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(image.timestamp);
  }
}

template<>
FoxgloveResult<std::string> encode<Timestamp>(const Timestamp& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Timestamp> decode<Timestamp>(const std::string& json_str) {
  return decode_impl<Timestamp>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Duration>(const Duration& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Duration> decode<Duration>(const std::string& json_str) {
  return decode_impl<Duration>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Vector3>(const Vector3& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Vector3> decode<Vector3>(const std::string& json_str) {
  return decode_impl<Vector3>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Quaternion>(const Quaternion& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Quaternion> decode<Quaternion>(const std::string& json_str) {
  return decode_impl<Quaternion>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Pose>(const Pose& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Pose> decode<Pose>(const std::string& json_str) {
  return decode_impl<Pose>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Color>(const Color& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Color> decode<Color>(const std::string& json_str) {
  return decode_impl<Color>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Point3>(const Point3& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Point3> decode<Point3>(const std::string& json_str) {
  return decode_impl<Point3>(json_str);
}

template<>
FoxgloveResult<std::string> encode<ArrowPrimitive>(const ArrowPrimitive& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<ArrowPrimitive> decode<ArrowPrimitive>(const std::string& json_str) {
  return decode_impl<ArrowPrimitive>(json_str);
}

template<>
FoxgloveResult<std::string> encode<CubePrimitive>(const CubePrimitive& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<CubePrimitive> decode<CubePrimitive>(const std::string& json_str) {
  return decode_impl<CubePrimitive>(json_str);
}

template<>
FoxgloveResult<std::string> encode<SpherePrimitive>(const SpherePrimitive& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<SpherePrimitive> decode<SpherePrimitive>(const std::string& json_str) {
  return decode_impl<SpherePrimitive>(json_str);
}

template<>
FoxgloveResult<std::string> encode<CylinderPrimitive>(const CylinderPrimitive& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<CylinderPrimitive> decode<CylinderPrimitive>(const std::string& json_str) {
  return decode_impl<CylinderPrimitive>(json_str);
}

template<>
FoxgloveResult<std::string> encode<LinePrimitive>(const LinePrimitive& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<LinePrimitive> decode<LinePrimitive>(const std::string& json_str) {
  return decode_impl<LinePrimitive>(json_str);
}

template<>
FoxgloveResult<std::string> encode<SceneEntityDeletion>(const SceneEntityDeletion& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<SceneEntityDeletion> decode<SceneEntityDeletion>(const std::string& json_str) {
  return decode_impl<SceneEntityDeletion>(json_str);
}

template<>
FoxgloveResult<std::string> encode<KeyValuePair>(const KeyValuePair& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<KeyValuePair> decode<KeyValuePair>(const std::string& json_str) {
  return decode_impl<KeyValuePair>(json_str);
}

template<>
FoxgloveResult<std::string> encode<SceneEntity>(const SceneEntity& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<SceneEntity> decode<SceneEntity>(const std::string& json_str) {
  return decode_impl<SceneEntity>(json_str);
}

template<>
FoxgloveResult<std::string> encode<SceneUpdate>(const SceneUpdate& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<SceneUpdate> decode<SceneUpdate>(const std::string& json_str) {
  return decode_impl<SceneUpdate>(json_str);
}

template<>
FoxgloveResult<std::string> encode<FrameTransform>(const FrameTransform& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<FrameTransform> decode<FrameTransform>(const std::string& json_str) {
  return decode_impl<FrameTransform>(json_str);
}

template<>
FoxgloveResult<std::string> encode<Log>(const Log& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<Log> decode<Log>(const std::string& json_str) {
  return decode_impl<Log>(json_str);
}

template<>
FoxgloveResult<std::string> encode<CompressedImage>(const CompressedImage& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<CompressedImage> decode<CompressedImage>(const std::string& json_str) {
  return decode_impl<CompressedImage>(json_str);
}

}  // namespace foxglove
