#include <foxglove/channel.hpp>
#include <foxglove/messages.hpp>
#include <foxglove/schema.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace foxglove;

int main() {
  SceneEntity entity;
  entity.timestamp.sec = 100;
  entity.timestamp.nsec = 200;
  entity.frame_id = "map";
  entity.id = "entity_1";
  entity.lifetime.sec = 5;
  entity.frame_locked = true;
  entity.metadata = {{"source", "sim"}, {"category", "demo"}};

  CubePrimitive cube;
  cube.pose.orientation.w = 1.0;
  cube.size = {1.0, 1.0, 1.0};
  cube.color = {0.2, 0.4, 0.8, 1.0};
  entity.cubes.push_back(cube);

  SpherePrimitive sphere;
  sphere.pose.position = {2.0, 0.0, 0.0};
  sphere.pose.orientation.w = 1.0;
  sphere.size = {0.5, 0.5, 0.5};
  sphere.color = {0.9, 0.3, 0.2, 1.0};
  entity.spheres.push_back(sphere);

  ArrowPrimitive arrow;
  arrow.pose.orientation.w = 1.0;
  arrow.shaft_length = 1.0;
  arrow.shaft_diameter = 0.1;
  arrow.head_length = 0.2;
  arrow.head_diameter = 0.2;
  arrow.color = {1.0, 1.0, 0.0, 1.0};
  entity.arrows.push_back(arrow);

  SceneEntityDeletion deletion;
  deletion.timestamp.sec = 1;
  deletion.timestamp.nsec = 2;
  deletion.type = DeletionType::MATCHING_ID;
  deletion.id = "old_entity";

  SceneUpdate update;
  update.deletions.push_back(deletion);
  update.entities.push_back(entity);

  FrameTransform transform;
  transform.timestamp.sec = 123;
  transform.timestamp.nsec = 456;
  transform.parent_frame_id = "map";
  transform.child_frame_id = "base_link";
  transform.translation = {1.0, 2.0, 3.0};
  transform.rotation = {0.0, 0.0, 0.0, 1.0};

  Log log;
  log.timestamp.sec = 77;
  log.timestamp.nsec = 88;
  log.level = LogLevel::WARNING;
  log.message = "Battery low";
  log.name = "monitor";
  log.file = "monitor.cpp";
  log.line = 1234;

  CompressedImage image;
  image.timestamp.sec = 9;
  image.timestamp.nsec = 10;
  image.frame_id = "camera";
  image.format = "jpeg";
  image.data = {0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x01, 0x02};

  auto update_json = encode(update);
  auto transform_json = encode(transform);
  auto log_json = encode(log);
  auto image_json = encode(image);

  if (!update_json.has_value() || !transform_json.has_value() || !log_json.has_value() ||
      !image_json.has_value()) {
    std::printf("Encoding failed\n");
    return 1;
  }

  std::printf("SceneUpdate: %s\n", update_json.value().c_str());
  std::printf("FrameTransform: %s\n", transform_json.value().c_str());
  std::printf("Log: %s\n", log_json.value().c_str());
  std::printf("CompressedImage: %s\n", image_json.value().c_str());

  Schema scene_schema{"foxglove.SceneUpdate", "jsonschema", std::vector<uint8_t>{'{', '}'}};
  auto raw_result = RawChannel::create(
    "/scene",
    "json",
    scene_schema,
    [](uint32_t channel_id, const uint8_t* data, size_t len, uint64_t stamp) {
      std::string payload(reinterpret_cast<const char*>(data), len);
      std::printf(
        "TypedChannel log => channel=%u ts=%llu payload=%s\n",
        channel_id,
        static_cast<unsigned long long>(stamp),
        payload.c_str()
      );
    }
  );
  if (!raw_result.has_value()) {
    std::printf("RawChannel create failed\n");
    return 1;
  }

  SceneUpdateChannel typed_channel(std::move(raw_result.value()));
  auto channel_result = typed_channel.log(update, 1'700'000'000'000'000'000ULL);
  if (!channel_result.has_value()) {
    std::printf("TypedChannel log failed\n");
    return 1;
  }

  return 0;
}
