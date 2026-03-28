#pragma once

#include <foxglove/channel.hpp>
#include <foxglove/error.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace foxglove {

/// @brief A timestamp composed of seconds and nanoseconds.
///
/// Used to represent a point in time relative to a user-defined epoch.
/// Both sec and nsec are non-negative, with nsec in the range [0, 999999999].
struct Timestamp {
  /// @brief The number of seconds since a user-defined epoch.
  uint32_t sec = 0;

  /// @brief The number of nanoseconds since the sec value.
  uint32_t nsec = 0;

  /// @brief Returns the JSON schema for this type.
  static nlohmann::json json_schema();
};

/// @brief A duration of time, composed of seconds and nanoseconds.
///
/// Unlike Timestamp, Duration can represent negative time intervals.
/// The sec field can be negative, while nsec is always non-negative.
struct Duration {
  /// @brief The number of seconds in the duration (can be negative).
  int32_t sec = 0;

  /// @brief The number of nanoseconds in the positive direction.
  uint32_t nsec = 0;

  /// @brief Returns the JSON schema for this type.
  static nlohmann::json json_schema();
};

/// @brief A vector in 3D space.
///
/// Represents a direction or position with x, y, z components.
struct Vector3 {
  /// @brief x coordinate.
  double x = 0.0;

  /// @brief y coordinate.
  double y = 0.0;

  /// @brief z coordinate.
  double z = 0.0;

  /// @brief Returns the JSON schema for this type.
  static nlohmann::json json_schema();
};

/// @brief A quaternion representing a rotation in 3D space.
///
/// Quaternions are used to represent rotations without suffering from
/// gimbal lock. The default value is the identity quaternion (w=1).
struct Quaternion {
  /// @brief x component.
  double x = 0.0;

  /// @brief y component.
  double y = 0.0;

  /// @brief z component.
  double z = 0.0;

  /// @brief w component (scalar part). Defaults to 1.0 for identity.
  double w = 1.0;

  /// @brief Returns the JSON schema for this type.
  static nlohmann::json json_schema();
};

/// @brief A position and orientation for an object in 3D space.
///
/// Combines a Vector3 position with a Quaternion orientation.
struct Pose {
  /// @brief Position in 3D space.
  Vector3 position;

  /// @brief Orientation in 3D space.
  Quaternion orientation;

  /// @brief Returns the JSON schema for this type.
  static nlohmann::json json_schema();
};

/// @brief A color in RGBA format.
///
/// All components are in the range [0, 1], where 0 is minimum intensity
/// and 1 is maximum intensity. Alpha of 0 is fully transparent, 1 is opaque.
struct Color {
  /// @brief Red component (0-1).
  double r = 0.0;

  /// @brief Green component (0-1).
  double g = 0.0;

  /// @brief Blue component (0-1).
  double b = 0.0;

  /// @brief Alpha component (0-1), where 0 is transparent and 1 is opaque.
  double a = 0.0;

  /// @brief Returns the JSON schema for this type.
  static nlohmann::json json_schema();
};

struct Point3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  static nlohmann::json json_schema();
};

struct ArrowPrimitive {
  Pose pose;
  double shaft_length = 0.0;
  double shaft_diameter = 0.0;
  double head_length = 0.0;
  double head_diameter = 0.0;
  Color color;

  static nlohmann::json json_schema();
};

struct CubePrimitive {
  Pose pose;
  Vector3 size;
  Color color;

  static nlohmann::json json_schema();
};

struct SpherePrimitive {
  Pose pose;
  Vector3 size;
  Color color;

  static nlohmann::json json_schema();
};

struct CylinderPrimitive {
  Pose pose;
  Vector3 size;
  double bottom_scale = 0.0;
  double top_scale = 0.0;
  Color color;

  static nlohmann::json json_schema();
};

enum class LineType : uint8_t { LINE_STRIP = 0, LINE_LOOP = 1, LINE_LIST = 2 };

struct LinePrimitive {
  LineType type = LineType::LINE_STRIP;
  Pose pose;
  double thickness = 0.0;
  bool scale_invariant = false;
  std::vector<Point3> points;
  Color color;
  std::vector<Color> colors;
  std::vector<uint32_t> indices;

  static nlohmann::json json_schema();
};

enum class DeletionType : uint8_t { MATCHING_ID = 0, ALL = 1 };

struct SceneEntityDeletion {
  Timestamp timestamp;
  DeletionType type = DeletionType::MATCHING_ID;
  std::string id;

  static nlohmann::json json_schema();
};

struct KeyValuePair {
  std::string key;
  std::string value;

  static nlohmann::json json_schema();
};

struct SceneEntity {
  Timestamp timestamp;
  std::string frame_id;
  std::string id;
  Duration lifetime;
  bool frame_locked = false;
  std::vector<KeyValuePair> metadata;
  std::vector<ArrowPrimitive> arrows;
  std::vector<CubePrimitive> cubes;
  std::vector<SpherePrimitive> spheres;
  std::vector<CylinderPrimitive> cylinders;
  std::vector<LinePrimitive> lines;

  static nlohmann::json json_schema();
};

struct SceneUpdate {
  std::vector<SceneEntityDeletion> deletions;
  std::vector<SceneEntity> entities;

  static nlohmann::json json_schema();
};

struct FrameTransform {
  Timestamp timestamp;
  std::string parent_frame_id;
  std::string child_frame_id;
  Vector3 translation;
  Quaternion rotation;

  static nlohmann::json json_schema();
};

enum class LogLevel : uint8_t {
  UNKNOWN = 0,
  DEBUG = 1,
  INFO = 2,
  WARNING = 3,
  ERROR = 4,
  FATAL = 5
};

struct Log {
  Timestamp timestamp;
  LogLevel level = LogLevel::UNKNOWN;
  std::string message;
  std::string name;
  std::string file;
  uint32_t line = 0;

  static nlohmann::json json_schema();
};

struct CompressedImage {
  Timestamp timestamp;
  std::string frame_id;
  std::vector<uint8_t> data;
  std::string format;

  static nlohmann::json json_schema();
};

/// @brief Equality comparison for Timestamp.
inline bool operator==(const Timestamp& lhs, const Timestamp& rhs) {
  return lhs.sec == rhs.sec && lhs.nsec == rhs.nsec;
}

/// @brief Equality comparison for Duration.
inline bool operator==(const Duration& lhs, const Duration& rhs) {
  return lhs.sec == rhs.sec && lhs.nsec == rhs.nsec;
}

/// @brief Equality comparison for Vector3.
inline bool operator==(const Vector3& lhs, const Vector3& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

/// @brief Equality comparison for Quaternion.
inline bool operator==(const Quaternion& lhs, const Quaternion& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

/// @brief Equality comparison for Pose.
inline bool operator==(const Pose& lhs, const Pose& rhs) {
  return lhs.position == rhs.position && lhs.orientation == rhs.orientation;
}

/// @brief Equality comparison for Color.
inline bool operator==(const Color& lhs, const Color& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

inline bool operator==(const Point3& lhs, const Point3& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

inline bool operator==(const ArrowPrimitive& lhs, const ArrowPrimitive& rhs) {
  return lhs.pose == rhs.pose && lhs.shaft_length == rhs.shaft_length &&
         lhs.shaft_diameter == rhs.shaft_diameter && lhs.head_length == rhs.head_length &&
         lhs.head_diameter == rhs.head_diameter && lhs.color == rhs.color;
}

inline bool operator==(const CubePrimitive& lhs, const CubePrimitive& rhs) {
  return lhs.pose == rhs.pose && lhs.size == rhs.size && lhs.color == rhs.color;
}

inline bool operator==(const SpherePrimitive& lhs, const SpherePrimitive& rhs) {
  return lhs.pose == rhs.pose && lhs.size == rhs.size && lhs.color == rhs.color;
}

inline bool operator==(const CylinderPrimitive& lhs, const CylinderPrimitive& rhs) {
  return lhs.pose == rhs.pose && lhs.size == rhs.size && lhs.bottom_scale == rhs.bottom_scale &&
         lhs.top_scale == rhs.top_scale && lhs.color == rhs.color;
}

inline bool operator==(const LinePrimitive& lhs, const LinePrimitive& rhs) {
  return lhs.type == rhs.type && lhs.pose == rhs.pose && lhs.thickness == rhs.thickness &&
         lhs.scale_invariant == rhs.scale_invariant && lhs.points == rhs.points &&
         lhs.color == rhs.color && lhs.colors == rhs.colors && lhs.indices == rhs.indices;
}

inline bool operator==(const SceneEntityDeletion& lhs, const SceneEntityDeletion& rhs) {
  return lhs.timestamp == rhs.timestamp && lhs.type == rhs.type && lhs.id == rhs.id;
}

inline bool operator==(const KeyValuePair& lhs, const KeyValuePair& rhs) {
  return lhs.key == rhs.key && lhs.value == rhs.value;
}

inline bool operator==(const SceneEntity& lhs, const SceneEntity& rhs) {
  return lhs.timestamp == rhs.timestamp && lhs.frame_id == rhs.frame_id && lhs.id == rhs.id &&
         lhs.lifetime == rhs.lifetime && lhs.frame_locked == rhs.frame_locked &&
         lhs.metadata == rhs.metadata && lhs.arrows == rhs.arrows && lhs.cubes == rhs.cubes &&
         lhs.spheres == rhs.spheres && lhs.cylinders == rhs.cylinders && lhs.lines == rhs.lines;
}

inline bool operator==(const SceneUpdate& lhs, const SceneUpdate& rhs) {
  return lhs.deletions == rhs.deletions && lhs.entities == rhs.entities;
}

inline bool operator==(const FrameTransform& lhs, const FrameTransform& rhs) {
  return lhs.timestamp == rhs.timestamp && lhs.parent_frame_id == rhs.parent_frame_id &&
         lhs.child_frame_id == rhs.child_frame_id && lhs.translation == rhs.translation &&
         lhs.rotation == rhs.rotation;
}

inline bool operator==(const Log& lhs, const Log& rhs) {
  return lhs.timestamp == rhs.timestamp && lhs.level == rhs.level && lhs.message == rhs.message &&
         lhs.name == rhs.name && lhs.file == rhs.file && lhs.line == rhs.line;
}

inline bool operator==(const CompressedImage& lhs, const CompressedImage& rhs) {
  return lhs.timestamp == rhs.timestamp && lhs.frame_id == rhs.frame_id && lhs.data == rhs.data &&
         lhs.format == rhs.format;
}

/// @brief Inequality comparison for Timestamp.
inline bool operator!=(const Timestamp& lhs, const Timestamp& rhs) {
  return !(lhs == rhs);
}

/// @brief Inequality comparison for Duration.
inline bool operator!=(const Duration& lhs, const Duration& rhs) {
  return !(lhs == rhs);
}

/// @brief Inequality comparison for Vector3.
inline bool operator!=(const Vector3& lhs, const Vector3& rhs) {
  return !(lhs == rhs);
}

/// @brief Inequality comparison for Quaternion.
inline bool operator!=(const Quaternion& lhs, const Quaternion& rhs) {
  return !(lhs == rhs);
}

/// @brief Inequality comparison for Pose.
inline bool operator!=(const Pose& lhs, const Pose& rhs) {
  return !(lhs == rhs);
}

/// @brief Inequality comparison for Color.
inline bool operator!=(const Color& lhs, const Color& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const Point3& lhs, const Point3& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const ArrowPrimitive& lhs, const ArrowPrimitive& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const CubePrimitive& lhs, const CubePrimitive& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const SpherePrimitive& lhs, const SpherePrimitive& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const CylinderPrimitive& lhs, const CylinderPrimitive& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const LinePrimitive& lhs, const LinePrimitive& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const SceneEntityDeletion& lhs, const SceneEntityDeletion& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const KeyValuePair& lhs, const KeyValuePair& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const SceneEntity& lhs, const SceneEntity& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const SceneUpdate& lhs, const SceneUpdate& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const FrameTransform& lhs, const FrameTransform& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const Log& lhs, const Log& rhs) {
  return !(lhs == rhs);
}

inline bool operator!=(const CompressedImage& lhs, const CompressedImage& rhs) {
  return !(lhs == rhs);
}

namespace detail {

/// @brief Serialize a double value with NaN → null conversion.
///
/// Foxglove protocol requires that NaN values be serialized as JSON null.
/// This is an internal helper used by message to_json functions. It is NOT
/// a global ADL overload for double — that would interfere with all double
/// serialization in the foxglove namespace.
inline nlohmann::json double_to_json(double value) {
  if (std::isnan(value)) {
    return nullptr;
  }
  return value;
}

/// @brief Deserialize a double value with null → NaN conversion.
///
/// When deserializing, JSON null values are converted back to NaN.
inline double double_from_json(const nlohmann::json& j) {
  if (j.is_null()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return j.get<double>();
}

}  // namespace detail

/// @brief Serialize Timestamp to JSON.
void to_json(nlohmann::json& j, const Timestamp& ts);

/// @brief Deserialize Timestamp from JSON.
void from_json(const nlohmann::json& j, Timestamp& ts);

/// @brief Serialize Duration to JSON.
void to_json(nlohmann::json& j, const Duration& dur);

/// @brief Deserialize Duration from JSON.
void from_json(const nlohmann::json& j, Duration& dur);

/// @brief Serialize Vector3 to JSON.
void to_json(nlohmann::json& j, const Vector3& vec);

/// @brief Deserialize Vector3 from JSON.
void from_json(const nlohmann::json& j, Vector3& vec);

/// @brief Serialize Quaternion to JSON.
void to_json(nlohmann::json& j, const Quaternion& quat);

/// @brief Deserialize Quaternion from JSON.
void from_json(const nlohmann::json& j, Quaternion& quat);

/// @brief Serialize Pose to JSON.
void to_json(nlohmann::json& j, const Pose& pose);

/// @brief Deserialize Pose from JSON.
void from_json(const nlohmann::json& j, Pose& pose);

/// @brief Serialize Color to JSON.
void to_json(nlohmann::json& j, const Color& color);

/// @brief Deserialize Color from JSON.
void from_json(const nlohmann::json& j, Color& color);

void to_json(nlohmann::json& j, const Point3& point);
void from_json(const nlohmann::json& j, Point3& point);

void to_json(nlohmann::json& j, const ArrowPrimitive& primitive);
void from_json(const nlohmann::json& j, ArrowPrimitive& primitive);

void to_json(nlohmann::json& j, const CubePrimitive& primitive);
void from_json(const nlohmann::json& j, CubePrimitive& primitive);

void to_json(nlohmann::json& j, const SpherePrimitive& primitive);
void from_json(const nlohmann::json& j, SpherePrimitive& primitive);

void to_json(nlohmann::json& j, const CylinderPrimitive& primitive);
void from_json(const nlohmann::json& j, CylinderPrimitive& primitive);

void to_json(nlohmann::json& j, const LinePrimitive& primitive);
void from_json(const nlohmann::json& j, LinePrimitive& primitive);

void to_json(nlohmann::json& j, const SceneEntityDeletion& deletion);
void from_json(const nlohmann::json& j, SceneEntityDeletion& deletion);

void to_json(nlohmann::json& j, const KeyValuePair& key_value);
void from_json(const nlohmann::json& j, KeyValuePair& key_value);

void to_json(nlohmann::json& j, const SceneEntity& entity);
void from_json(const nlohmann::json& j, SceneEntity& entity);

void to_json(nlohmann::json& j, const SceneUpdate& update);
void from_json(const nlohmann::json& j, SceneUpdate& update);

void to_json(nlohmann::json& j, const FrameTransform& transform);
void from_json(const nlohmann::json& j, FrameTransform& transform);

void to_json(nlohmann::json& j, const Log& log);
void from_json(const nlohmann::json& j, Log& log);

void to_json(nlohmann::json& j, const CompressedImage& image);
void from_json(const nlohmann::json& j, CompressedImage& image);

/// @brief Encode a message to JSON string.
///
/// @tparam T The message type (Timestamp, Vector3, etc.)
/// @param msg The message to encode
/// @return Result containing the JSON string on success, error on failure
template<typename T>
FoxgloveResult<std::string> encode(const T& msg);

/// @brief Decode a message from JSON string.
///
/// @tparam T The message type to decode into
/// @param json_str The JSON string to decode
/// @return Result containing the decoded message on success, error on failure
template<typename T>
FoxgloveResult<T> decode(const std::string& json_str);

template<typename T>
class TypedChannel {
public:
  explicit TypedChannel(RawChannel channel)
      : channel_(std::move(channel)) {}

  FoxgloveResult<void> log(const T& msg, uint64_t timestamp_ns) {
    auto json = encode(msg);
    if (!json.has_value()) {
      return tl::make_unexpected(json.error());
    }
    const auto& data = json.value();
    channel_.log(reinterpret_cast<const uint8_t*>(data.data()), data.size(), timestamp_ns);
    return {};
  }

  RawChannel& raw() {
    return channel_;
  }

private:
  RawChannel channel_;
};

using SceneUpdateChannel = TypedChannel<SceneUpdate>;
using FrameTransformChannel = TypedChannel<FrameTransform>;
using LogChannel = TypedChannel<Log>;

}  // namespace foxglove
