#pragma once

#include <foxglove/error.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <string>

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

/// @brief Encode a message to JSON string.
///
/// @tparam T The message type (Timestamp, Vector3, etc.)
/// @param msg The message to encode
/// @return Result containing the JSON string on success, error on failure
template <typename T>
FoxgloveResult<std::string> encode(const T& msg);

/// @brief Decode a message from JSON string.
///
/// @tparam T The message type to decode into
/// @param json_str The JSON string to decode
/// @return Result containing the decoded message on success, error on failure
template <typename T>
FoxgloveResult<T> decode(const std::string& json_str);

}  // namespace foxglove
