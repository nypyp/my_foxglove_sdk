#include <foxglove/context.hpp>
#include <foxglove/mcap.hpp>
#include <foxglove/messages.hpp>
#include <foxglove/schema.hpp>
#include <foxglove/server.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace foxglove;

namespace {

uint64_t now_ns() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
    duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()
  );
}

Timestamp to_timestamp(uint64_t ns_epoch) {
  Timestamp ts;
  ts.sec = static_cast<uint32_t>(ns_epoch / 1'000'000'000ULL);
  ts.nsec = static_cast<uint32_t>(ns_epoch % 1'000'000'000ULL);
  return ts;
}

Schema make_schema(const std::string& name, const nlohmann::json& schema_json) {
  const auto dumped = schema_json.dump();
  return Schema{name, "jsonschema", std::vector<uint8_t>(dumped.begin(), dumped.end())};
}

}  // namespace

int main() {
  auto ctx_result = Context::create();
  if (!ctx_result.has_value()) {
    std::printf("Failed to create Context\n");
    return 1;
  }
  auto context = std::move(ctx_result.value());

  WebSocketServerOptions ws_options;
  ws_options.port = 8765;
  ws_options.name = "my_foxglove_sdk_ch09";
  auto server_result = WebSocketServer::create(std::move(ws_options));
  if (!server_result.has_value()) {
    std::printf("Failed to create WebSocketServer\n");
    return 1;
  }
  auto server = std::move(server_result.value());

  auto ws_sink = std::make_shared<WebSocketServerSink>(server);
  auto mcap_sink_result = McapWriterSink::create("output.mcap");
  if (!mcap_sink_result.has_value()) {
    std::printf("Failed to create McapWriterSink\n");
    server.shutdown();
    return 1;
  }
  auto mcap_sink = mcap_sink_result.value();

  const uint32_t ws_sink_id = context.add_sink(ws_sink);
  const uint32_t mcap_sink_id = context.add_sink(mcap_sink);

  auto scene_raw_result = context.create_channel(
    "/scene", "json", make_schema("foxglove.SceneUpdate", SceneUpdate::json_schema())
  );
  if (!scene_raw_result.has_value()) {
    std::printf("Failed to create /scene channel\n");
    context.remove_sink(mcap_sink_id);
    context.remove_sink(ws_sink_id);
    mcap_sink->close();
    server.shutdown();
    return 1;
  }

  auto tf_raw_result = context.create_channel(
    "/tf", "json", make_schema("foxglove.FrameTransform", FrameTransform::json_schema())
  );
  if (!tf_raw_result.has_value()) {
    std::printf("Failed to create /tf channel\n");
    context.remove_sink(mcap_sink_id);
    context.remove_sink(ws_sink_id);
    mcap_sink->close();
    server.shutdown();
    return 1;
  }

  SceneUpdateChannel scene_channel(std::move(scene_raw_result.value()));
  FrameTransformChannel tf_channel(std::move(tf_raw_result.value()));

  constexpr double kPi = 3.14159265358979323846;
  const auto t0 = std::chrono::steady_clock::now();
  auto next_tick = t0;
  const std::chrono::milliseconds period(100);
  uint32_t tick = 0;

  while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
    const uint64_t stamp_ns = now_ns();
    const double t = static_cast<double>(tick) * 0.1;
    const double yaw = t;
    const double half = 0.5 * yaw;
    const double sphere_x = 1.5 + std::cos(t) * 0.8;
    const double sphere_y = std::sin(t) * 0.8;

    SceneEntity entity;
    entity.timestamp = to_timestamp(stamp_ns);
    entity.frame_id = "map";
    entity.id = "animated_entity";
    entity.frame_locked = false;

    CubePrimitive cube;
    cube.pose.position = {0.0, 0.0, 0.5};
    cube.pose.orientation = {0.0, 0.0, std::sin(half), std::cos(half)};
    cube.size = {1.0, 1.0, 1.0};
    cube.color = {0.1, 0.6, 0.9, 1.0};
    entity.cubes.push_back(cube);

    SpherePrimitive sphere;
    sphere.pose.position = {sphere_x, sphere_y, 0.6};
    sphere.pose.orientation = {0.0, 0.0, 0.0, 1.0};
    sphere.size = {0.35, 0.35, 0.35};
    sphere.color = {1.0, 0.4, 0.2, 1.0};
    entity.spheres.push_back(sphere);

    SceneUpdate scene_update;
    scene_update.entities.push_back(entity);

    FrameTransform tf;
    tf.timestamp = to_timestamp(stamp_ns);
    tf.parent_frame_id = "map";
    tf.child_frame_id = "base_link";
    tf.translation = {std::cos(t * 0.5), std::sin(t * 0.5), 0.0};
    tf.rotation = {0.0, 0.0, std::sin(kPi * 0.1 * t), std::cos(kPi * 0.1 * t)};

    if (!scene_channel.log(scene_update, stamp_ns).has_value()) {
      std::printf("Failed to log SceneUpdate\n");
      break;
    }
    if (!tf_channel.log(tf, stamp_ns).has_value()) {
      std::printf("Failed to log FrameTransform\n");
      break;
    }

    ++tick;
    next_tick += period;
    std::this_thread::sleep_until(next_tick);
  }

  const uint32_t scene_channel_id = scene_channel.raw().id();
  const uint32_t tf_channel_id = tf_channel.raw().id();
  scene_channel.raw().close();
  tf_channel.raw().close();
  context.remove_channel(scene_channel_id);
  context.remove_channel(tf_channel_id);
  context.remove_sink(mcap_sink_id);
  context.remove_sink(ws_sink_id);
  mcap_sink->close();
  server.shutdown();

  std::printf("Chapter 9 e2e example finished, wrote output.mcap\n");
  return 0;
}
