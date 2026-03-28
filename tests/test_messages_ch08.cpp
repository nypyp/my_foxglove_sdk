#include <foxglove/messages.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace foxglove;

std::string read_golden_file(const std::string& filename) {
  std::ifstream file(filename);
  REQUIRE(file.is_open());
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
    content.pop_back();
  }
  return content;
}

std::string golden_path(const std::string& filename) {
  return std::string(FOXGLOVE_TEST_GOLDEN_DIR) + "/" + filename;
}

TEST_CASE("CubePrimitive - roundtrip serialization") {
  CubePrimitive primitive;
  primitive.pose.position.x = 1.0;
  primitive.pose.position.y = 2.0;
  primitive.pose.position.z = 3.0;
  primitive.pose.orientation.w = 1.0;
  primitive.size.x = 4.0;
  primitive.size.y = 5.0;
  primitive.size.z = 6.0;
  primitive.color.r = 1.0;
  primitive.color.g = 0.5;
  primitive.color.b = 0.25;
  primitive.color.a = 1.0;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());

  auto decoded = decode<CubePrimitive>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == primitive);
}

TEST_CASE("CubePrimitive - golden file match") {
  CubePrimitive primitive;
  primitive.pose.position.x = 1.0;
  primitive.pose.position.y = 2.0;
  primitive.pose.position.z = 3.0;
  primitive.pose.orientation.w = 1.0;
  primitive.size.x = 4.0;
  primitive.size.y = 5.0;
  primitive.size.z = 6.0;
  primitive.color.r = 1.0;
  primitive.color.g = 0.5;
  primitive.color.b = 0.25;
  primitive.color.a = 1.0;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("cube_primitive.json")));
}

TEST_CASE("SpherePrimitive - roundtrip serialization") {
  SpherePrimitive primitive;
  primitive.pose.position.x = -1.0;
  primitive.pose.position.y = 0.5;
  primitive.pose.position.z = 2.5;
  primitive.pose.orientation.w = 1.0;
  primitive.size.x = 1.5;
  primitive.size.y = 1.5;
  primitive.size.z = 1.5;
  primitive.color.r = 0.0;
  primitive.color.g = 1.0;
  primitive.color.b = 0.0;
  primitive.color.a = 0.8;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());

  auto decoded = decode<SpherePrimitive>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == primitive);
}

TEST_CASE("SpherePrimitive - golden file match") {
  SpherePrimitive primitive;
  primitive.pose.position.x = -1.0;
  primitive.pose.position.y = 0.5;
  primitive.pose.position.z = 2.5;
  primitive.pose.orientation.w = 1.0;
  primitive.size.x = 1.5;
  primitive.size.y = 1.5;
  primitive.size.z = 1.5;
  primitive.color.r = 0.0;
  primitive.color.g = 1.0;
  primitive.color.b = 0.0;
  primitive.color.a = 0.8;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("sphere_primitive.json")));
}

TEST_CASE("CylinderPrimitive - roundtrip serialization") {
  CylinderPrimitive primitive;
  primitive.pose.position.x = 0.0;
  primitive.pose.position.y = 0.0;
  primitive.pose.position.z = 1.0;
  primitive.pose.orientation.w = 1.0;
  primitive.size.x = 1.0;
  primitive.size.y = 1.0;
  primitive.size.z = 2.0;
  primitive.bottom_scale = 1.0;
  primitive.top_scale = 0.5;
  primitive.color.r = 0.2;
  primitive.color.g = 0.3;
  primitive.color.b = 0.9;
  primitive.color.a = 1.0;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());

  auto decoded = decode<CylinderPrimitive>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == primitive);
}

TEST_CASE("CylinderPrimitive - golden file match") {
  CylinderPrimitive primitive;
  primitive.pose.position.x = 0.0;
  primitive.pose.position.y = 0.0;
  primitive.pose.position.z = 1.0;
  primitive.pose.orientation.w = 1.0;
  primitive.size.x = 1.0;
  primitive.size.y = 1.0;
  primitive.size.z = 2.0;
  primitive.bottom_scale = 1.0;
  primitive.top_scale = 0.5;
  primitive.color.r = 0.2;
  primitive.color.g = 0.3;
  primitive.color.b = 0.9;
  primitive.color.a = 1.0;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("cylinder_primitive.json")));
}

TEST_CASE("ArrowPrimitive - roundtrip serialization") {
  ArrowPrimitive primitive;
  primitive.pose.position.x = 3.0;
  primitive.pose.position.y = 2.0;
  primitive.pose.position.z = 1.0;
  primitive.pose.orientation.w = 1.0;
  primitive.shaft_length = 2.0;
  primitive.shaft_diameter = 0.1;
  primitive.head_length = 0.4;
  primitive.head_diameter = 0.2;
  primitive.color.r = 1.0;
  primitive.color.g = 0.0;
  primitive.color.b = 0.0;
  primitive.color.a = 1.0;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());

  auto decoded = decode<ArrowPrimitive>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == primitive);
}

TEST_CASE("ArrowPrimitive - golden file match") {
  ArrowPrimitive primitive;
  primitive.pose.position.x = 3.0;
  primitive.pose.position.y = 2.0;
  primitive.pose.position.z = 1.0;
  primitive.pose.orientation.w = 1.0;
  primitive.shaft_length = 2.0;
  primitive.shaft_diameter = 0.1;
  primitive.head_length = 0.4;
  primitive.head_diameter = 0.2;
  primitive.color.r = 1.0;
  primitive.color.g = 0.0;
  primitive.color.b = 0.0;
  primitive.color.a = 1.0;

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("arrow_primitive.json")));
}

TEST_CASE("LinePrimitive - roundtrip serialization") {
  LinePrimitive primitive;
  primitive.type = LineType::LINE_LIST;
  primitive.pose.orientation.w = 1.0;
  primitive.thickness = 0.05;
  primitive.scale_invariant = true;
  primitive.points = {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, {2.0, 1.0, 0.0}};
  primitive.color = {0.8, 0.8, 0.8, 1.0};
  primitive.colors = {{1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0}};
  primitive.indices = {0, 1, 1, 2};

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());

  auto decoded = decode<LinePrimitive>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == primitive);
}

TEST_CASE("LinePrimitive - golden file match") {
  LinePrimitive primitive;
  primitive.type = LineType::LINE_LIST;
  primitive.pose.orientation.w = 1.0;
  primitive.thickness = 0.05;
  primitive.scale_invariant = true;
  primitive.points = {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, {2.0, 1.0, 0.0}};
  primitive.color = {0.8, 0.8, 0.8, 1.0};
  primitive.colors = {{1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0}};
  primitive.indices = {0, 1, 1, 2};

  auto encoded = encode(primitive);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("line_primitive.json")));
}

TEST_CASE("SceneEntityDeletion - roundtrip serialization") {
  SceneEntityDeletion deletion;
  deletion.timestamp.sec = 10;
  deletion.timestamp.nsec = 20;
  deletion.type = DeletionType::ALL;
  deletion.id = "entity_to_delete";

  auto encoded = encode(deletion);
  REQUIRE(encoded.has_value());

  auto decoded = decode<SceneEntityDeletion>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == deletion);
}

TEST_CASE("SceneEntityDeletion - golden file match") {
  SceneEntityDeletion deletion;
  deletion.timestamp.sec = 10;
  deletion.timestamp.nsec = 20;
  deletion.type = DeletionType::ALL;
  deletion.id = "entity_to_delete";

  auto encoded = encode(deletion);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("scene_entity_deletion.json")));
}

TEST_CASE("SceneEntity - roundtrip serialization") {
  SceneEntity entity;
  entity.timestamp.sec = 100;
  entity.timestamp.nsec = 200;
  entity.frame_id = "map";
  entity.id = "entity_1";
  entity.lifetime.sec = 5;
  entity.lifetime.nsec = 0;
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

  LinePrimitive line;
  line.type = LineType::LINE_STRIP;
  line.pose.orientation.w = 1.0;
  line.thickness = 0.02;
  line.points = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
  line.color = {0.0, 1.0, 1.0, 1.0};
  entity.lines.push_back(line);

  CylinderPrimitive cylinder;
  cylinder.pose.orientation.w = 1.0;
  cylinder.size = {0.3, 0.3, 2.0};
  cylinder.bottom_scale = 1.0;
  cylinder.top_scale = 1.0;
  cylinder.color = {0.6, 0.6, 0.6, 1.0};
  entity.cylinders.push_back(cylinder);

  auto encoded = encode(entity);
  REQUIRE(encoded.has_value());

  auto decoded = decode<SceneEntity>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == entity);
}

TEST_CASE("SceneEntity - golden file match") {
  SceneEntity entity;
  entity.timestamp.sec = 100;
  entity.timestamp.nsec = 200;
  entity.frame_id = "map";
  entity.id = "entity_1";
  entity.lifetime.sec = 5;
  entity.lifetime.nsec = 0;
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

  LinePrimitive line;
  line.type = LineType::LINE_STRIP;
  line.pose.orientation.w = 1.0;
  line.thickness = 0.02;
  line.points = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
  line.color = {0.0, 1.0, 1.0, 1.0};
  entity.lines.push_back(line);

  CylinderPrimitive cylinder;
  cylinder.pose.orientation.w = 1.0;
  cylinder.size = {0.3, 0.3, 2.0};
  cylinder.bottom_scale = 1.0;
  cylinder.top_scale = 1.0;
  cylinder.color = {0.6, 0.6, 0.6, 1.0};
  entity.cylinders.push_back(cylinder);

  auto encoded = encode(entity);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("scene_entity.json")));
}

TEST_CASE("SceneUpdate - roundtrip serialization") {
  SceneUpdate update;

  SceneEntityDeletion deletion;
  deletion.timestamp.sec = 1;
  deletion.timestamp.nsec = 2;
  deletion.type = DeletionType::MATCHING_ID;
  deletion.id = "old_entity";
  update.deletions.push_back(deletion);

  SceneEntity entity;
  entity.timestamp.sec = 3;
  entity.timestamp.nsec = 4;
  entity.frame_id = "world";
  entity.id = "new_entity";
  entity.frame_locked = false;
  update.entities.push_back(entity);

  auto encoded = encode(update);
  REQUIRE(encoded.has_value());

  auto decoded = decode<SceneUpdate>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == update);
}

TEST_CASE("SceneUpdate - golden file match") {
  SceneUpdate update;

  SceneEntityDeletion deletion;
  deletion.timestamp.sec = 1;
  deletion.timestamp.nsec = 2;
  deletion.type = DeletionType::MATCHING_ID;
  deletion.id = "old_entity";
  update.deletions.push_back(deletion);

  SceneEntity entity;
  entity.timestamp.sec = 3;
  entity.timestamp.nsec = 4;
  entity.frame_id = "world";
  entity.id = "new_entity";
  entity.frame_locked = false;
  update.entities.push_back(entity);

  auto encoded = encode(update);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("scene_update.json")));
}

TEST_CASE("FrameTransform - roundtrip serialization") {
  FrameTransform transform;
  transform.timestamp.sec = 123;
  transform.timestamp.nsec = 456;
  transform.parent_frame_id = "map";
  transform.child_frame_id = "base_link";
  transform.translation = {1.0, 2.0, 3.0};
  transform.rotation = {0.0, 0.0, 0.0, 1.0};

  auto encoded = encode(transform);
  REQUIRE(encoded.has_value());

  auto decoded = decode<FrameTransform>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == transform);
}

TEST_CASE("FrameTransform - golden file match") {
  FrameTransform transform;
  transform.timestamp.sec = 123;
  transform.timestamp.nsec = 456;
  transform.parent_frame_id = "map";
  transform.child_frame_id = "base_link";
  transform.translation = {1.0, 2.0, 3.0};
  transform.rotation = {0.0, 0.0, 0.0, 1.0};

  auto encoded = encode(transform);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("frame_transform.json")));
}

TEST_CASE("Log - roundtrip serialization") {
  Log log;
  log.timestamp.sec = 77;
  log.timestamp.nsec = 88;
  log.level = LogLevel::WARNING;
  log.message = "Battery low";
  log.name = "monitor";
  log.file = "monitor.cpp";
  log.line = 1234;

  auto encoded = encode(log);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Log>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == log);
}

TEST_CASE("Log - golden file match") {
  Log log;
  log.timestamp.sec = 77;
  log.timestamp.nsec = 88;
  log.level = LogLevel::WARNING;
  log.message = "Battery low";
  log.name = "monitor";
  log.file = "monitor.cpp";
  log.line = 1234;

  auto encoded = encode(log);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("log.json")));
}

TEST_CASE("CompressedImage - roundtrip serialization") {
  CompressedImage image;
  image.timestamp.sec = 9;
  image.timestamp.nsec = 10;
  image.frame_id = "camera";
  image.format = "jpeg";
  image.data = {0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x01, 0x02};

  auto encoded = encode(image);
  REQUIRE(encoded.has_value());

  auto decoded = decode<CompressedImage>(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value() == image);
}

TEST_CASE("CompressedImage - golden file match") {
  CompressedImage image;
  image.timestamp.sec = 9;
  image.timestamp.nsec = 10;
  image.frame_id = "camera";
  image.format = "jpeg";
  image.data = {0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x01, 0x02};

  auto encoded = encode(image);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value() == read_golden_file(golden_path("compressed_image.json")));
}
