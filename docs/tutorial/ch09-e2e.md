# Chapter 9：端到端装配，把前八章的零件接成一条真正可跑的系统链路

> **对应 tag**：`v0.9-e2e`
> **起点**：`v0.8-messages`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `examples/ch09_e2e/main.cpp` — 把 `Context`、`WebSocketServer`、双 sink、typed channel 和发布循环装配成完整示例程序
> - `tests/test_e2e.cpp` — 用 MCAP magic bytes 与 sink 写文件测试锁定端到端链路的最小正确性
> - `docs/tutorial/ch09-e2e.md` — 把参考文档整理成统一教程模板，并明确集成验证边界
>
> **深入阅读**：[09-端到端连接Foxglove-Studio.md](../09-端到端连接Foxglove-Studio.md)
> **预计阅读时间**：55 分钟
> **前置要求**：完成 Ch04、Ch06、Ch07b 和 Ch08，已经理解 `Context` 的 sink 扇出、`McapWriterSink` 的文件写入链路，以及 `TypedChannel<T>::log()` 如何把结构化消息变成 `RawChannel` 可发送的字节流

---

## 9.0 本章地图

前八章已经把协议、channel、server、serialization、context、mcap、内置消息类型这些模块一块块搭起来了，但“零件都在”并不等于“系统能跑”。本章做的不是再发明新抽象，而是把已有模块按正确顺序装起来，形成一条真实的端到端链路：同一份 `SceneUpdate` / `FrameTransform` 消息，一路送到 `WebSocketServerSink` 做实时可视化，另一路送到 `McapWriterSink` 落盘成 `output.mcap`。

读完后，你应该能掌握三件事：

1. 如何把 `Context`、server、sink 和 typed channel 装成一个完整系统
2. 为什么双 sink 架构能同时满足实时观察和离线复盘
3. 为什么项目把本地 `ctest`，而不是外部 Foxglove Studio 或 CLI，当成唯一硬性完成标准

```text
SceneUpdate / FrameTransform
        |
        v
TypedChannel<T>::log()
        |
        v
RawChannel
        |
        v
Context
   +----+-------------------+
   |                        |
   v                        v
WebSocketServerSink     McapWriterSink
   |                        |
   v                        v
Foxglove Studio         output.mcap
```

如果说 Ch08 解决的是“该发什么消息”，那 Ch09 解决的就是“这些消息怎样在系统边界之间流动，并且既能在线消费，也能离线保存”。这一步做完，教程版 SDK 才真正从“模块集合”变成“完整工具链”。

---

## 9.1 从需求出发

### 零件已经齐了，为什么还不等于系统？

到 Ch08 为止，你手里其实已经有了很多看起来“单独可用”的能力。

- Ch04 提供了 `WebSocketServer`
- Ch06 提供了 `Context` 和 sink 扇出
- Ch07b 提供了 `McapWriterSink`
- Ch08 提供了 `SceneUpdateChannel` 和 `FrameTransformChannel`

如果只从 API 名字看，很容易产生一个错觉：把这些对象各自创建出来，系统自然就工作了。真实工程里通常不是这样。端到端链路最难的部分，往往不是某个底层算法，而是**装配顺序、失败路径和退出路径**。

这一章要面对的，就是三个典型的集成挑战。

### 挑战一：初始化顺序

例如，你当然可以先创建 channel，再晚一点注册 sink；代码照样能编译。但如果你的系统语义是“channel 一创建出来就应该有消费者可以接到它的消息”，那这种顺序就会让最早一批消息没有下游。

本项目的 Chapter 9 示例故意采用一条很明确的顺序：

1. 创建 `Context`
2. 创建 `WebSocketServer`
3. 创建 `WebSocketServerSink` 和 `McapWriterSink`
4. 先把 sink 注册到 `Context`
5. 再创建 `/scene` 和 `/tf` 两条 typed channel
6. 最后进入发布循环

这条顺序背后的原则不是“这样写比较好看”，而是**先把路由网络铺好，再让消息开始流动**。

### 挑战二：关闭顺序

系统运行时大家都在“连着”，退出时才会暴露对象之间真正的依赖方向。比如 `WebSocketServer` 如果还在运行，而某些 channel、sink 或底层资源已经先析构了，就可能出现悬空引用、尾部数据没 flush 完、footer 没写出去之类的问题。

Chapter 9 的示例因此明确展示了关闭顺序：

1. 关闭 `scene_channel` 和 `tf_channel` 对应的 `RawChannel`
2. 从 `Context` 中移除 channel
3. 从 `Context` 中移除两个 sink
4. 显式 `mcap_sink->close()`
5. 最后 `server.shutdown()`

这套顺序的核心是：**先停止消息源，再拆路由，再关闭落盘和网络出口。** 这样每一层都在自己仍然有效的时候完成收尾。

### 挑战三：错误传播与 early return

集成代码最容易被低估的一点，是失败路径不是“补几个 `if` 就行”。Chapter 9 示例里，`Context::create()`、`WebSocketServer::create()`、`McapWriterSink::create()`、`context.create_channel()` 都可能失败。只要任何一步失败，系统就不应该假装自己还能继续跑。

所以示例代码采用了非常直接的策略：

- 创建阶段失败：立即打印错误并 `return 1`
- 某些资源已经创建成功：按当前拥有的资源范围做最小清理
- 运行阶段 `log()` 失败：打印错误、跳出循环、进入统一收尾路径

这种写法一点都不花哨，但它非常适合教学和工程起步阶段。因为端到端示例最重要的不是炫技，而是把“系统何时开始工作、何时停止工作、失败后谁负责清理”写得一清二楚。

---

## 9.2 设计接口（先看装配关系）

Chapter 9 没有像前几章那样新增一整组头文件接口；它更像是一章“系统装配课”。你要掌握的重点，不是新 API 名字，而是已有 API 之间怎样搭配。

### 先看系统装配骨架

`examples/ch09_e2e/main.cpp` 的主线，可以先抽象成下面这张图：

```text
Context
  |
  +-- add_sink(WebSocketServerSink)
  |
  +-- add_sink(McapWriterSink)
  |
  +-- create_channel("/scene", "json", SceneUpdate schema)
  |
  +-- create_channel("/tf", "json", FrameTransform schema)
          |
          v
  SceneUpdateChannel / FrameTransformChannel
          |
          v
      publish loop
```

这里的几个角色，各自职责非常清楚。

- `Context`：统一路由器，只负责持有 channel / sink 关系并做扇出
- `WebSocketServer`：真正负责 WebSocket 生命周期和连接管理
- `WebSocketServerSink`：把路由出来的消息送给在线客户端
- `McapWriterSink`：把路由出来的消息写进 MCAP 文件
- `SceneUpdateChannel` / `FrameTransformChannel`：让业务层以结构化消息而不是裸字节发消息

从依赖方向上看，业务代码并不直接“调用 WebSocketServer 发消息”或“调用 McapWriter 写记录”。它只是在 typed channel 上调用 `log()`。随后由 `Context` 把同一份消息扇出到两个不同 sink。这正是 Ch06 那种“生产者不关心下游有几个消费者”的设计，在本章第一次完整落地。

### 为什么是双 sink，而不是二选一？

Chapter 9 的核心不是“能连上 Foxglove Studio”，而是**同一份数据同时服务两条消费链路**。

第一条链路是实时链路：

- `WebSocketServerSink`
- 在线客户端连接后立即能看到 `/scene` 和 `/tf`
- 适合调试、观察动画是否如预期变化

第二条链路是离线链路：

- `McapWriterSink`
- 相同消息被落盘成 `output.mcap`
- 适合回放、归档、复现问题、后续分析

这不是“为了 demo 更炫”才硬塞进去的设计。在真实系统里，在线可视化和离线记录几乎总是一起出现，因为你既想看现在发生了什么，也想保留之后复盘的证据。

用 `Context` 做扇出还有一个很直接的好处：业务层只构造一次消息。你不会为了实时链路拼一份 JSON，再为了文件链路再拼一份 JSON。单一消息源能显著减少在线与离线结果不一致的风险。

> 💡 **🏗️ 工程旁白：集成测试边界——不测外部，测组合行为**
>
> 集成测试最容易走偏的地方，是把“系统对外部世界是否可达”误认为“系统内部组合是否正确”。对 Chapter 9 而言，Foxglove Studio 能不能在你本机成功连上，当然重要，但它受端口占用、图形环境、网络策略、客户端版本等太多外部条件影响。如果把这些都纳入 CI 的硬门槛，测试会非常脆弱。本项目更稳妥的边界是：在仓库内部验证 **消息能不能从 typed channel 经过 `Context` 到达 sink，并最终形成合法 MCAP 文件**。这叫测组合行为，不叫逃避真实环境；真实环境可以留给手工演示，而自动化测试要优先锁定仓库自己能完全控制的那一段。

### schema 是怎样接进来的？

Chapter 9 还有一个容易被忽略、但非常关键的装配点：channel 不是凭 topic 名字就能创建的，它还需要 schema。

示例没有手写大段 schema 字符串，而是复用 Ch08 中每个消息类型自带的 `json_schema()`：

```cpp
auto scene_raw_result = context.create_channel(
  "/scene", "json", make_schema("foxglove.SceneUpdate", SceneUpdate::json_schema())
);

auto tf_raw_result = context.create_channel(
  "/tf", "json", make_schema("foxglove.FrameTransform", FrameTransform::json_schema())
);
```

`make_schema()` 做的事情并不复杂：把 `nlohmann::json` dump 成字符串，再转成 `std::vector<uint8_t>` 填进 `Schema`。但这个小转换把“消息结构定义”和“channel 建立时需要的 schema 数据”顺滑地接了起来。于是本章没有引入新的 schema 管理器，却依然维持了 schema 与消息类型的一致性。

---

## 9.3 实现核心逻辑

理解装配关系之后，再看 `main.cpp` 就会清楚很多。这个文件真正展示的，是系统从启动到退出的完整生命周期。

### 第一步：创建 `Context` 和 `WebSocketServer`

程序开头先拿到路由核心和网络出口：

```cpp
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
```

这里的写法非常朴素，但它体现了一个端到端程序必须有的态度：**启动失败就明确失败，不要把一个半初始化状态的系统继续往下推。** `Context` 和 `WebSocketServer` 是整个链路的基础，如果这两步都没成功，后面所有 channel、sink 和发布循环都不成立。

### 第二步：创建双 sink，并先注册再建 channel

接下来是整个 Chapter 9 的关键装配段：

```cpp
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
```

这段代码把两个下游出口先都挂进 `Context`。顺序上看，它比“先建 channel，后加 sink”更稳，因为一旦后面 channel 开始 `log()`，系统已经具备完整的扇出路径。

注意这里两个 sink 的所有权形式不同：

- `WebSocketServerSink` 用 `std::shared_ptr`
- `McapWriterSink::create()` 返回结果类型包装的 sink 对象

这并不表示它们在架构上的地位不同，只是各自实现细节和创建方式不同。对 `Context` 来说，它只认“这是一个可接收消息的 sink”。这正是接口抽象带来的好处。

### 第三步：用消息类型自带的 schema 建 channel

Chapter 9 没有退回到裸 `RawChannel` API，而是直接利用上一章已经准备好的消息类型和 schema：

```cpp
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
```

这里最值得你注意的，不只是 `SceneUpdate::json_schema()` 和 `FrameTransform::json_schema()` 被复用了，更是失败路径里那一长串清理动作。示例代码没有追求花哨的 RAII 封装，而是把“失败时需要回收哪些资源”直接写出来。这对教学非常有价值，因为它把资源依赖关系暴露得很清楚。

随后，`RawChannel` 会被包进上一章定义的 typed channel：

```cpp
SceneUpdateChannel scene_channel(std::move(scene_raw_result.value()));
FrameTransformChannel tf_channel(std::move(tf_raw_result.value()));
```

从这一刻开始，业务层终于不必再自己手动 `encode()` 再喂字节，而可以直接发结构化消息。

### 第四步：发布循环要稳定，不要让时间漂移越滚越大

示例程序不是简单跑一轮就退出，而是以大约 10Hz 的节奏持续 10 秒：

```cpp
const auto t0 = std::chrono::steady_clock::now();
auto next_tick = t0;
const std::chrono::milliseconds period(100);

while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
  // 构造 SceneUpdate 和 FrameTransform
  if (!scene_channel.log(scene_update, stamp_ns).has_value()) {
    std::printf("Failed to log SceneUpdate\n");
    break;
  }
  if (!tf_channel.log(tf, stamp_ns).has_value()) {
    std::printf("Failed to log FrameTransform\n");
    break;
  }

  next_tick += period;
  std::this_thread::sleep_until(next_tick);
}
```

这段代码里，最关键的不是消息构造细节，而是节拍控制方式。它没有每轮都 `sleep_for(100ms)`，而是维护一个递增的 `next_tick`，再 `sleep_until(next_tick)`。这样即使循环体内部有少量抖动，也不会把误差不断累积下去。

消息构造部分则刻意选了两种不同语义：

- `SceneUpdate`：一边旋转立方体，一边让球体做平面圆周运动
- `FrameTransform`：让 `base_link` 相对 `map` 发生缓慢位姿变化

它们共同构成一个足够“有动态感”的最小 demo，但代码量又没有膨胀到掩盖装配主线。如果你想看全部字段是怎样填充的，完整代码直接看仓库里的 `examples/ch09_e2e/main.cpp` 即可。

> 💡 **⚠️ 工程旁白：测试中的时间依赖——如何避免 flaky test**
>
> 和时间有关的程序最容易写出“本机偶尔过、CI 偶尔挂”的 flaky test。根源通常有两个：一是把 wall-clock 结果当成精确断言对象，二是让测试依赖真实睡眠时长。Chapter 9 的自动化测试刻意没有去断言“10 秒内一定发出 100 条消息”或“每 100ms 恰好命中一次”，因为这类断言太容易受到调度抖动影响。更稳妥的做法，是把时间敏感逻辑留在示例程序里演示，把测试关注点收缩到可重复验证的产物上，比如 MCAP 文件是否存在、magic bytes 是否正确、sink 链路是否至少写出了一条合法消息。换句话说：**测试时间相关代码时，优先验证结果契约，而不是苛求运行时钟像实验室仪器一样精确。**

### 第五步：退出时明确做“先停源，再拆路，再关出口”

Chapter 9 的收尾代码值得你逐行看，因为它把关闭顺序写得非常具体：

```cpp
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
```

这里有两个重要信号。

第一，`channel.raw().close()` 和 `context.remove_channel()` 是分开的。也就是说，逻辑上先告诉 channel 停止发送，再从路由表里把它移除。这样做能把“停止产出消息”和“从系统注册表摘除”这两个动作明确分开。

第二，`mcap_sink->close()` 是显式调用的，而不是期待析构时悄悄完成。对文件类资源来说，这种显式性很重要，因为你往往希望 footer、magic、flush 时机都在受控的程序路径里发生，而不是让对象寿命尾声替你做黑箱决定。

最终程序打印：

```cpp
std::printf("Chapter 9 e2e example finished, wrote output.mcap\n");
```

这句话不是“测试通过”的证明，但它是对示例运行结束状态的一个清晰交代：程序约 10 秒后退出，并在当前目录留下 `output.mcap`。

---

## 9.4 测试：验证正确性

Chapter 9 的测试重点不是“把整个 Foxglove 生态都拉进 CI”，而是验证仓库内部可控的端到端组合行为。`tests/test_e2e.cpp` 目前有两组测试，它们各自回答一个很明确的问题。

### 测试一：`McapWriter` 写出的文件，是否至少具有合法边界？

第一组测试直接绕过 `Context` 和 channel，使用最小 `McapWriter` 路径写一个文件：

```cpp
auto writer_result = McapWriter::open(kPath);
REQUIRE(writer_result.has_value());

auto schema_id = writer.add_schema("foxglove.SceneUpdate", "jsonschema", schema_data);
REQUIRE(schema_id.has_value());

auto channel_id = writer.add_channel(schema_id.value(), "/scene", "json");
REQUIRE(channel_id.has_value());

REQUIRE(writer.write_message(msg).has_value());
REQUIRE(writer.close().has_value());
```

然后测试把整个文件读回内存，检查三件事：

1. 文件大小大于 0
2. 文件前 8 字节等于 `kMcapMagic`
3. 文件后 8 字节等于 `kMcapMagic`

对应的 magic 常量是：

```cpp
constexpr std::array<uint8_t, 8> kMcapMagic = {0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};
```

这组测试不试图证明“整个 MCAP 语义都完全正确”。它锁定的是**最小文件边界是否成立**。如果连 header/footer magic 都不对，那文件格式的基本完整性就已经出了问题，根本不必谈后续消费链路。

### 测试二：`Context + McapWriterSink + SceneUpdateChannel` 是否真的把消息写进文件？

第二组测试更接近本章的主题。它创建 `Context`，创建 `McapWriterSink`，把 sink 注册进去，再通过 `SceneUpdateChannel` 发一条真实的 `SceneUpdate`：

```cpp
auto ctx_result = Context::create();
REQUIRE(ctx_result.has_value());
auto context = std::move(ctx_result.value());

auto sink_result = McapWriterSink::create(kPath);
REQUIRE(sink_result.has_value());
auto sink = sink_result.value();

context.add_sink(sink);

auto scene_raw_result = context.create_channel(
  "/scene", "json", Schema{"foxglove.SceneUpdate", "jsonschema", {'{', '}'}}
);
REQUIRE(scene_raw_result.has_value());

SceneUpdateChannel scene_channel(std::move(scene_raw_result.value()));
REQUIRE(scene_channel.log(update, now_ns()).has_value());
REQUIRE(sink->close().has_value());
```

它验证的结果也很务实：

- 文件存在
- 文件大小大于 0
- 文件前后 magic 正确

换句话说，这组测试证明了：**一条从 typed channel 发出的消息，确实能穿过 `Context`，到达 `McapWriterSink`，并形成一个最小合法的 MCAP 文件。**

### 这里要特别注意：当前测试并没有覆盖 WebSocket sink 收到消息

这一点必须说清楚，因为 Chapter 9 的运行架构里确实有两个 sink，但 `tests/test_e2e.cpp` 当前只自动验证了文件路径。

它**没有**做这些事情：

- 没有启动真实客户端并断言收到了 WebSocket 消息
- 没有模拟 Foxglove Studio 连接 `/scene` 和 `/tf`
- 没有断言“双 sink 都收到同一条消息”

所以本章可以合理地说“系统架构支持双 sink”，也可以说“示例程序会同时挂两个 sink”，但**不能过度声称现有自动化测试已经证明了 WebSocket 投递成功**。这类验证更适合手工演示、专用网络测试，或者后续单独补更重的 integration test。

### 为什么不把 Foxglove Studio 或外部 CLI 设成 CI 硬依赖？

理由有三个。

第一，它们不属于仓库内部最小可控闭环。只要把 CI 绑到外部 GUI 或 CLI，测试稳定性就会受环境波动明显影响。

第二，本章真正要锁定的是“SDK 内部组合是否正确”，而不是“某个外部程序在当前环境能否启动”。这两个问题有联系，但不是同一层问题。

第三，项目已经有一个足够明确的完成标准：`ctest` 全部通过。对教程来说，这个标准越单一越好。读者只要能在本地跑通测试，就说明仓库内部的端到端最小链路已经成立；想进一步观察 Studio 的实时效果，再手工运行示例即可。

---

## 9.5 打 tag，验证完成

这一章完成后，你已经把前八章的模块装成了一条真实可跑的系统链路。标准完成流程如下：

```bash
# 1. 构建并运行测试（这是唯一的正确性标准）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag（my- 前缀避免与仓库参考 tag 冲突）
git add .
git commit -m "feat(ch09): add end-to-end example"
git tag my-v0.9-e2e

# 3. 与参考实现对比（辅助理解，非强制门槛）
git diff v0.9-e2e
```

**完成标准**：`ctest` 全部通过仍然是唯一硬性门槛。`git diff v0.9-e2e` 很可能存在实现风格、变量命名、清理路径组织方式上的差异，这都不奇怪。只要测试通过，说明端到端装配已经成立。diff 的价值在于帮你观察参考实现在示例组织和失败路径处理上还有哪些额外工程取舍。

对这一章，建议你另外做两项有针对性的人工检查。

第一，手工运行 `examples/ch09_e2e/main.cpp`，确认程序大约 10 秒后退出，并在工作目录留下 `output.mcap`。这一步不是自动化硬门槛，但它能帮助你把“测试里验证的最小契约”和“真实演示时的完整体验”接起来。

第二，顺着 `SceneUpdateChannel::log()` 和 `FrameTransformChannel::log()` 往下看一遍，确认你真的理解消息是如何先经过 typed channel、再进入 `RawChannel`、再由 `Context` 扇出到两个 sink 的。这个认识比单纯看到画面更重要，因为它决定了你后续能否继续扩展更多 topic 和更多 sink。

---

## 本章小结

- **本章掌握了**：
  - 为什么“已有 server、context、mcap、messages”还不等于一个真正可跑的系统，装配顺序和失败路径同样重要
  - `Context`、`WebSocketServerSink`、`McapWriterSink`、`SceneUpdateChannel`、`FrameTransformChannel` 之间的职责边界与协作方式
  - 双 sink 架构为什么能同时满足实时可视化与离线复盘，而且业务层只需构造一次消息
  - `examples/ch09_e2e/main.cpp` 如何通过 schema 装配、稳定节拍循环和显式关闭顺序把系统完整跑起来
  - `tests/test_e2e.cpp` 当前真正锁定了哪些端到端契约，以及它刻意没有覆盖哪些外部依赖

- **工程知识点**：
  - 集成测试边界——不测外部，测组合行为
  - 测试中的时间依赖——如何避免 flaky test

- **延伸练习**：
  - 给 Chapter 9 再加一条自定义 topic，比如 `/log`，练习如何把第三条 typed channel 接进现有双 sink 装配路径
  - 补一组更深入的 MCAP 断言，例如解析记录类型并验证至少出现 Header、Channel、Message、Footer，体会“最小契约”和“更强契约”的差别
  - 单独设计一组 WebSocket integration test，思考如何在不引入过多环境不稳定性的前提下，补上在线链路的自动验证

- **参考文档**：[`docs/09-端到端连接Foxglove-Studio.md`](../09-端到端连接Foxglove-Studio.md)
