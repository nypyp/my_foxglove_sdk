#include <foxglove/messages.hpp>

#include <cmath>
#include <cstdio>
#include <string>

using namespace foxglove;

void print_result(const char* name, const FoxgloveResult<std::string>& result) {
  if (result.has_value()) {
    printf("%s: %s\n", name, result.value().c_str());
  } else {
    printf("%s: [encoding failed]\n", name);
  }
}

template <typename T>
void test_roundtrip(const char* name, const T& msg) {
  auto encoded = encode(msg);
  if (!encoded.has_value()) {
    printf("%s encode: FAILED\n", name);
    return;
  }

  printf("%s encoded: %s\n", name, encoded.value().c_str());

  auto decoded = decode<T>(encoded.value());
  if (!decoded.has_value()) {
    printf("%s decode: FAILED\n", name);
    return;
  }

  if (decoded.value() == msg) {
    printf("%s roundtrip: OK\n\n", name);
  } else {
    printf("%s roundtrip: FAILED (values differ)\n\n", name);
  }
}

int main() {
  printf("=== Chapter 5: Message Serialization Demo ===\n\n");

  // Timestamp
  Timestamp ts;
  ts.sec = 1234567890;
  ts.nsec = 123456789;
  test_roundtrip("Timestamp", ts);

  // Duration (with negative seconds)
  Duration dur;
  dur.sec = -5;
  dur.nsec = 500000000;
  test_roundtrip("Duration", dur);

  // Vector3
  Vector3 vec;
  vec.x = 1.0;
  vec.y = 2.0;
  vec.z = 3.0;
  test_roundtrip("Vector3", vec);

  // Quaternion (identity)
  Quaternion quat;
  quat.x = 0.0;
  quat.y = 0.0;
  quat.z = 0.0;
  quat.w = 1.0;
  test_roundtrip("Quaternion", quat);

  // Pose
  Pose pose;
  pose.position.x = 1.0;
  pose.position.y = 2.0;
  pose.position.z = 3.0;
  pose.orientation.w = 1.0;  // identity orientation
  test_roundtrip("Pose", pose);

  // Color
  Color color;
  color.r = 1.0;
  color.g = 0.5;
  color.b = 0.0;
  color.a = 1.0;
  test_roundtrip("Color", color);

  // Demonstrate NaN handling
  printf("=== NaN Handling Demo ===\n");
  Vector3 nan_vec;
  nan_vec.x = std::nan("");
  nan_vec.y = 2.0;
  nan_vec.z = 3.0;

  auto nan_encoded = encode(nan_vec);
  if (nan_encoded.has_value()) {
    printf("Vector3 with NaN: %s\n", nan_encoded.value().c_str());

    auto nan_decoded = decode<Vector3>(nan_encoded.value());
    if (nan_decoded.has_value()) {
      printf("Decoded x is NaN: %s\n",
             std::isnan(nan_decoded.value().x) ? "yes" : "no");
      printf("Decoded y: %f\n", nan_decoded.value().y);
      printf("Decoded z: %f\n", nan_decoded.value().z);
    }
  }

  printf("\n=== Demo Complete ===\n");
  return 0;
}
