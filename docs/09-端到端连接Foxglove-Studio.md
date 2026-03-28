# 第九章：端到端连接 Foxglove Studio

这一章把前 8 章拆开的能力重新组装成一个完整系统：

- 上游：业务代码生成结构化消息（`SceneUpdate` / `FrameTransform`）
- 中间：`Context` 统一路由
- 下游 A：`WebSocketServerSink` 实时发送给 Foxglove Studio
- 下游 B：`McapWriterSink` 同步落盘为 `output.mcap`

目标不是“再写一个 demo”，而是让你看到教学版 SDK 的完整数据闭环：

1. 数据如何从类型系统进入传输层
2. 数据如何同时被“在线消费”和“离线回放”
3. 为什么这种双通路设计在真实系统里几乎是默认选型

---

## 1. 章节目标与交付物

本章新增三个主要交付物：

1. `examples/ch09_e2e/main.cpp`
2. `tests/test_e2e.cpp`
3. `docs/09-端到端连接Foxglove-Studio.md`

其中示例程序负责“跑起来”，测试负责“可验证”，文档负责“可理解”。

你应该能在本章结束后获得两个能力：

- 能把前面章节的 API 组合成一个完整数据系统
- 能在不依赖外部 CLI 工具的情况下验证 MCAP 产物正确性

---

## 2. 端到端架构复盘：所有层如何组合

从代码分层看，Chapter 9 的路径如下：

```text
Messages (SceneUpdate / FrameTransform)
  -> encode<T>()
  -> TypedChannel<T>
  -> RawChannel
  -> Context
  -> Sink fan-out
      -> WebSocketServerSink -> WebSocket clients (Foxglove Studio)
      -> McapWriterSink      -> output.mcap
```

这里最关键的是 `Context`：它不是传输实现本身，而是“统一路由器”。
上游不关心下游有几个消费者；下游也不关心上游是谁产出的消息。

这种解耦意味着：

1. 你可以新增 sink 而不改业务发布代码
2. 你可以下线某个 sink 而不影响另一个 sink
3. 测试可以在不启网络的情况下仅验证文件 sink 行为

---

## 3. 线程模型与生命周期

本章示例涉及两个时序面：

- 主线程：创建上下文、创建 channel、周期发布消息、收尾关闭
- WebSocket 服务线程：由 `WebSocketServer::create()` 内部启动，执行 lws event loop

推荐生命周期顺序：

1. 创建 `Context`
2. 创建 `WebSocketServer`
3. 构造两个 sink 并注册到 `Context`
4. 创建 typed channels
5. 发布循环
6. 关闭 channels / remove sinks / close mcap / shutdown server

这一顺序能保证：

- channel 注册发生在 sink 已就位之后
- 退出阶段不会出现“server 还在跑但资源已被析构”的悬挂状态

---

## 4. 双 Sink 模式：实时可视化 + 离线录制

`WebSocketServerSink` 和 `McapWriterSink` 同时挂到 `Context`，是本章的核心模式。

它的价值在工程上非常直接：

1. 调试时看实时数据（Studio）
2. 复盘时看历史数据（MCAP）
3. 二者来自同一份原始消息，避免“在线与离线不一致”

注册方式很简单：

```cpp
auto ws_sink = std::make_shared<WebSocketServerSink>(server);
auto mcap_sink = McapWriterSink::create("output.mcap").value();

context.add_sink(ws_sink);
context.add_sink(mcap_sink);
```

这行代码背后体现的是“扇出（fan-out）”设计：
一条消息写一次，消费多次。

---

## 5. 示例程序设计：10 秒、约 10Hz、两类消息

Chapter 9 示例程序创建两条 typed channel：

1. `SceneUpdateChannel`（topic `/scene`）
2. `FrameTransformChannel`（topic `/tf`）

并在循环里发布动画数据：

- 立方体绕 Z 轴旋转（角度递增）
- 球体在平面内作圆周运动
- TF 里 `base_link` 相对 `map` 做缓慢位姿变化

节拍控制使用 `sleep_until(next_tick)`，每 100ms 触发一次，持续约 10 秒：

```cpp
const std::chrono::milliseconds period(100);
while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
  // publish scene + tf
  next_tick += period;
  std::this_thread::sleep_until(next_tick);
}
```

这种写法比 `sleep_for(100ms)` 更稳，因为它降低了循环体执行耗时带来的累计漂移。

---

## 6. Channel 与 Schema：为什么是这两种组合

本章按 Foxglove 常见实践选择：

- topic `/scene` 对应 schema `foxglove.SceneUpdate`
- topic `/tf` 对应 schema `foxglove.FrameTransform`
- message encoding 统一为 `json`

在教学版 SDK 中，`Context::create_channel()` 需要 `Schema` 对象：

```cpp
auto scene_raw = context.create_channel(
  "/scene", "json", Schema{"foxglove.SceneUpdate", "jsonschema", schema_bytes}
);
```

示例里直接用 `SceneUpdate::json_schema()` 与 `FrameTransform::json_schema()` 生成 schema data，
这样能避免硬编码 schema 字符串，并与消息定义保持同步。

---

## 7. 集成测试策略：不依赖外部 mcap CLI

本章新增 `tests/test_e2e.cpp`，明确约束是：

- 不调用外部 `mcap` 命令
- 不依赖网络可用性

测试拆成两部分：

### 7.1 验证 `McapWriter` 写出的文件边界

直接用 `McapWriter` 写一个极小文件，然后检查：

1. 文件大小 `> 0`
2. 前 8 字节是 magic
3. 后 8 字节是 magic

magic 定义为：

```cpp
constexpr std::array<uint8_t, 8> kMcapMagic = {0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};
```

这个测试不是在验证“全部格式语义”，而是在验证“最小完整文件边界”是否成立。

### 7.2 验证 `McapWriterSink + Context` 的链路

测试通过 `Context` 创建 `/scene` channel，发布一条 `SceneUpdate`，然后关闭 sink，
再读取文件检查：

- 文件存在
- 大小 `> 0`
- 首尾 magic 均正确

这保证了 sink 路径并非“可创建但不可写”。

---

## 8. 性能特征与容量预估

示例运行参数是 10 秒 * 10Hz * 2 topics，理论消息量约 200 条。

在教学项目里，这个规模有两个优点：

1. 足够展示实时动画连续性
2. 输出文件足够小，便于频繁实验与回归测试

性能关注点主要在三处：

1. JSON 编码开销（`encode<T>`）
2. Sink 扇出引入的多路写入开销
3. 文件 IO flush 时机（由 writer/sink 内部管理）

如果后续要扩展到高频传感器场景，可优先考虑：

- 压缩开启（chunk + zstd）
- topic 分层过滤（`ChannelFilter`）
- 减少对象重建（预分配/对象复用）

---

## 9. 与官方 quickstart 的对照

官方 quickstart 通常强调“快速看到消息出现在 Studio”。
本章在教学版里更强调“系统内聚与可验证性”。

两者差异可以概括为：

1. quickstart 偏演示路径，本章偏工程路径
2. quickstart 关注连通，本章同时关注连通 + 录制 + 测试
3. quickstart 常依赖外部环境，本章测试尽量本地闭环

这不是替代关系，而是层次关系：

- quickstart 适合“第一次跑通”
- 本章适合“形成可维护实践”

---

## 10. 设计回顾：这一版实现的取舍

Chapter 9 的取舍是“教育可读性优先”：

1. 明确写出创建、注册、发布、关闭的完整序列
2. 用 typed channel 代替裸字节拼接
3. 用小而完整的 integration test 保证回归稳定

暂未在示例里加入的内容（可作为后续练习）：

- 复杂多实体场景增删策略
- 网络断连重连监控
- 发布线程与业务线程隔离队列

这些不是不重要，而是被刻意留到后续演化阶段，避免第九章过载。

---

## 11. 运行与验证流程

建议按以下顺序验证：

1. 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

2. 运行全部测试

```bash
ctest --test-dir build --output-on-failure
```

3. 启动 Chapter 9 示例

```bash
./build/examples/ch09_e2e/ch09_e2e
```

正常情况下，程序约 10 秒后自动退出，并在工程根目录生成 `output.mcap`。

---

## 12. 常见问题排查

### 12.1 `output.mcap` 没有生成

优先检查：

1. `McapWriterSink::create("output.mcap")` 是否成功
2. 退出前是否调用了 `mcap_sink->close()`
3. 进程是否被异常中断导致来不及写 footer/magic

### 12.2 Studio 看不到 topic

检查：

1. 端口是否为 `8765`
2. server 是否成功创建
3. channel 是否在 sink 注册后创建

### 12.3 测试在某些环境偶发失败

如果失败点是文件读取，通常是：

1. 临时路径冲突
2. 关闭顺序不完整
3. 上一次异常退出留下旧文件

测试里使用固定临时文件名时，记得先 `remove` 再写。

---

## 13. 延伸练习（建议）

可以在本章基础上做以下练习：

1. 把 `SceneEntity` 扩展成多个实体并实现周期性 deletion
2. 为 `Context::add_sink` 添加 topic 级过滤，验证只录制 `/scene`
3. 把 10Hz 参数化为命令行参数（如 `--hz 30 --duration 20`）
4. 新增测试：解析记录 opcode，验证至少存在 Header/Channel/Message/Footer
5. 对比 `use_chunks=false` 与 `use_chunks=true` 的文件体积差异

这些练习能把“会用 API”升级为“能做工程优化”。

---

## 14. 本章小结

Chapter 9 完成了教学版 SDK 的第一条真正端到端链路：

- 同一份消息，同时驱动实时可视化与离线录制
- 集成测试可在本地、无外部 CLI、无网络依赖下验证关键正确性
- 示例程序可控（10 秒自动退出），便于教学与 CI 场景复用

如果说前 8 章是在造“零件”，第 9 章就是把零件装成一台可运行机器。
从这里开始，你已经具备向“更真实数据、更复杂系统、更强鲁棒性”继续迭代的基础。
