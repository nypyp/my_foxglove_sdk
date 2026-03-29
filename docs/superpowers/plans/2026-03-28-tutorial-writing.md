# 实现计划：docs/tutorial/ 手把手教程体系

**日期**：2026-03-28  
**关联 spec**：`docs/superpowers/specs/2026-03-28-tutorial-design.md`  
**状态**：待执行

---

## 前置条件

执行前确认：

```bash
cd /home/nypyp/foxglove_ws/src/my_foxglove_sdk
git log --oneline -5          # 确认在正确的 repo
git tag | sort                 # 确认所有 tag 存在：v0.1-skeleton 到 v0.9-e2e
ls docs/                       # 确认 9 篇参考文档存在
```

预期输出：10 个 git tag（v0.1-skeleton, v0.2-protocol, v0.3-channel, v0.4-server, v0.5-serialization, v0.6-context, v0.7a-mcap-basic, v0.7b-mcap-full, v0.8-messages, v0.9-e2e）

---

## Task 0：创建 tutorial/ 目录结构

**文件**：无（仅创建目录和空文件）

**命令**：

```bash
mkdir -p docs/tutorial
touch docs/tutorial/README.md
touch docs/tutorial/ch01-skeleton.md
touch docs/tutorial/ch02-protocol.md
touch docs/tutorial/ch03-channel.md
touch docs/tutorial/ch04-server.md
touch docs/tutorial/ch05-serialization.md
touch docs/tutorial/ch06-context.md
touch docs/tutorial/ch07a-mcap-basic.md
touch docs/tutorial/ch07b-mcap-full.md
touch docs/tutorial/ch08-messages.md
touch docs/tutorial/ch09-e2e.md
```

**验收**：`ls docs/tutorial/` 输出 11 个文件（README.md + 10 章）

**提交**：`git commit -m "docs(tutorial): scaffold tutorial directory"`

---

## Task 1：写 docs/tutorial/README.md

**目标**：教程导航页，读者第一眼看到的入口文档。

**必须包含**：

1. 「如何使用本教程」——3步操作：
   ```bash
   git clone <repo-url>
   cd my_foxglove_sdk
   git checkout -b my_foxglove_sdk v0.1-skeleton
   ```
   说明 Ch01 从 `v0.1-skeleton` 骨架出发，逐步添加代码。

2. 「教程 vs 参考文档」对比表：
   | 你想要... | 看这里 |
   |----------|-------|
   | 手把手写代码 | `docs/tutorial/` |
   | 深入理解某个概念 | `docs/0N-xxx.md` |

3. 章节索引表（10行）：
   | 章节 | Tag | 主题 | 核心工程知识点 |
   每行对应一章，Ch07 拆为 Ch07a/Ch07b。

4. 「git 工作流速查」：本地 tag 用 `my-` 前缀；`ctest` 是唯一完成门槛；diff 是辅助工具。

5. 「章节间依赖」说明：严格顺序。非线性读者如何从任意章节起步：
   ```bash
   git checkout -b my_foxglove_sdk <前一章的 tag>
   ```

**语言**：中文，技术术语保留英文（如 `RawChannel`、`tl::expected`）。

**格式**：标准 Markdown，无 HTML。

**验收**：文件 > 80 行；包含章节索引表；包含 git 操作代码块。

**提交**：`git commit -m "docs(tutorial): write README navigation page"`

---

## Task 2：写 ch01-skeleton.md

**对应 tag**：`v0.1-skeleton`  
**起点**：空仓库（`git checkout -b my_foxglove_sdk v0.1-skeleton`）  
**参考文档**：`docs/01-项目骨架与代码规范.md`  
**涉及文件**（查看 tag 内容了解）：`CMakeLists.txt`, `include/foxglove/error.hpp`, `src/error.cpp`, `tests/test_error.cpp`

**章节模板各 section 要求**：

- **N.0 本章地图**（≤100字）：本章从零搭建 CMake 工程骨架，引入 `tl::expected` 错误处理。读完后能用 `cmake -B build && cmake --build build && ctest` 跑通第一个测试。

- **N.1 从需求出发**：引入问题——一个 SDK 最基础的骨架需要什么？CMake、依赖管理、错误处理约定。引出为什么从这三件事开始而不是直接写功能代码。

- **N.2 设计接口（先写头文件）**：
  - 展示 `include/foxglove/error.hpp` 完整内容
  - 逐块解释：`FoxgloveError` enum 的设计意图，`tl::expected` 作为返回类型
  - 💡 工程旁白 🏗️ 设计决策：`tl::expected` vs 异常——什么时候用哪个（150-250字）

- **N.3 实现核心逻辑**：
  - 展示 `src/error.cpp` 关键部分（`error_message()` 函数）
  - 展示 `CMakeLists.txt` 关键片段：FetchContent 引入依赖
  - 💡 工程旁白 🏗️ 设计决策：CMake FetchContent vs 系统包管理器（150-250字）

- **N.4 测试：验证正确性**：
  - 先说策略：验证 error_message() 对每种 FoxgloveError 返回正确字符串
  - 展示 `tests/test_error.cpp` 关键测试用例（2-3个）
  - 说明为什么 error.cpp 值得测试：约定即契约

- **N.5 打 tag，验证完成**：使用标准完成流程模板（见 spec 第5节）
  - my-v0.1-skeleton

- **本章小结**：
  - 掌握了：CMake FetchContent 依赖管理、`tl::expected` 用法、Catch2 测试框架基础
  - 工程知识点：2个旁白标题
  - 延伸练习：1-2个
  - 参考文档：`docs/01-项目骨架与代码规范.md`

**验收**：文件 > 150 行；包含2个工程旁白；N.0-N.5 + 小结全部存在。

**提交**：`git commit -m "docs(tutorial): write ch01 skeleton"`

---

## Task 3：写 ch02-protocol.md

**对应 tag**：`v0.2-protocol`  
**起点**：`v0.1-skeleton`  
**参考文档**：`docs/02-Foxglove协议解析.md`  
**涉及文件**：`include/foxglove/protocol.hpp`, `src/protocol.cpp`, `tests/test_protocol.cpp`

**各 section 要点**：

- **N.0**：本章实现 Foxglove WebSocket 协议的数据结构与编解码。读完后能看懂一帧 Foxglove 二进制数据的每个字节含义，并通过测试验证编解码正确。

- **N.1**：没有协议层，Channel 的消息怎么传给 Studio？引出 `OpCode`、`ServerInfo`、`MessageData` 等核心数据结构的必要性。

- **N.2**：展示 `protocol.hpp` 中 `OpCode` enum、`ServerInfo` struct、`ChannelAdvertisement` struct、`MessageData` struct。逐块解释字段意义。
  - 💡 工程旁白 ⚠️ 常见陷阱：小端序与跨平台陷阱——为什么不能直接 memcpy（200-300字）

- **N.3**：展示 `protocol.cpp` 中 JSON 编码（`encode_server_info`）和二进制编解码（`encode_message_data` / `decode_message_data`）的核心逻辑。
  - 💡 工程旁白 🏗️ 设计决策：二进制协议向后兼容性设计（150-250字）
  - 非关键的 encode/decode 函数可省略，注明「完整代码见仓库 tag」

- **N.4**：策略——验证 JSON 编码正确性（golden match）和二进制往返（roundtrip）。展示 `test_protocol.cpp` 中 JSON golden 测试和 binary roundtrip 测试各1个。

- **N.5**：标准完成流程，tag：`my-v0.2-protocol`

- **小结**：参考 `docs/02-Foxglove协议解析.md`

**验收**：文件 > 180 行；包含2个工程旁白；含 ASCII 或代码展示二进制帧结构。

**提交**：`git commit -m "docs(tutorial): write ch02 protocol"`

---

## Task 4：写 ch03-channel.md

**对应 tag**：`v0.3-channel`  
**起点**：`v0.2-protocol`  
**参考文档**：`docs/03-Channel与Schema抽象.md`  
**涉及文件**：`include/foxglove/channel.hpp`, `include/foxglove/schema.hpp`, `src/channel.cpp`, `tests/test_channel.cpp`

**各 section 要点**：

- **N.0**：本章实现 `Schema` 和 `RawChannel`——数据发布的核心抽象。读完后能创建 Channel、用回调模型发布消息、用测试 mock 验证行为。

- **N.1**：协议层能编解码了，但谁来「命名」数据流、描述数据格式？引出 Schema 和 Channel 的必要性。

- **N.2**：展示 `channel.hpp` 和 `schema.hpp` 的完整公开接口。解释回调模型设计：为什么不直接耦合 WebSocket Server？
  - 💡 工程旁白 🧰 C++ 技巧：RAII 与资源生命周期管理——Channel 的 ID 分配与回收（150-250字）

- **N.3**：展示 `channel.cpp` 中 `RawChannel::create()`、`log()`、`set_callback()` 的核心实现。
  - 💡 工程旁白 ⚡ 性能/并发：`std::string_view` vs `std::string`——零拷贝的代价（150-250字）

- **N.4**：策略——用 lambda mock 捕获回调输出，验证 `log()` 正确触发回调并传递数据。展示 `test_channel.cpp` 中「callback 被触发」和「close 后 no-op」各1个测试。

- **N.5**：标准完成流程，tag：`my-v0.3-channel`

- **小结**：参考 `docs/03-Channel与Schema抽象.md`

**验收**：文件 > 180 行；包含2个工程旁白；明确解释回调模型与线程安全约束。

**提交**：`git commit -m "docs(tutorial): write ch03 channel"`

---

## Task 5：写 ch04-server.md

**对应 tag**：`v0.4-server`  
**起点**：`v0.3-channel`  
**参考文档**：`docs/04-WebSocket服务器.md`  
**涉及文件**：`include/foxglove/server.hpp`, `src/server.cpp`, `tests/test_server.cpp`

**各 section 要点**：

- **N.0**：本章实现 `WebSocketServer`，把 `RawChannel` 真正接到 Foxglove WebSocket 协议上。读完后能启动服务器、连接客户端、看到 `serverInfo` / `advertise` / `messageData` 的完整链路。

- **N.1**：前面已经有协议层和 Channel，但为什么 Studio 还是收不到数据？引出服务器职责：连接管理、订阅管理、消息排队、事件循环。

- **N.2**：展示 `server.hpp` 的公开接口：`WebSocketServerOptions`、`WebSocketServer::create()`、`add_channel()`、`remove_channel()`、`broadcast_time()`、`shutdown()`。解释为什么采用 PIMPL，以及为什么 Channel 注册是显式的。
  - 💡 工程旁白 🔍 对比视角：libwebsockets 事件循环模型 vs asio（180-260字）

- **N.3**：展示 `src/server.cpp` 中以下核心片段：
  - `lws_context_creation_info` 与静态 protocol 数组
  - service thread 中的 `lws_service()` 循环
  - `dispatch_message()` 如何查找订阅者并构造二进制帧
  - `LWS_CALLBACK_SERVER_WRITEABLE` 中 pending queue 的发送逻辑
  - 非关键 callback 分支可省略，但注明「完整代码见仓库 tag」
  - 💡 工程旁白 ⚡ 性能/并发：多线程与事件循环——哪些操作是线程安全的（180-260字）

- **N.4**：策略——以集成测试为主，因为本章验证的是协议交互而不是纯算法。展示 `test_server.cpp` 中「连接后收到 serverInfo」和「订阅后收到 messageData」各1个测试。强调为什么需要 pending queue：lws 每次 writable 回调只能写一条消息。

- **N.5**：标准完成流程，tag：`my-v0.4-server`

- **小结**：参考 `docs/04-WebSocket服务器.md`

**验收**：文件 > 220 行；包含2个工程旁白；至少一处展示 `messageData` 二进制帧布局；明确解释显式 `add_channel()` 的原因。

**提交**：`git commit -m "docs(tutorial): write ch04 server"`

---

## Task 6：写 ch05-serialization.md

**对应 tag**：`v0.5-serialization`  
**起点**：`v0.4-server`  
**参考文档**：`docs/05-消息序列化.md`  
**涉及文件**：`include/foxglove/messages.hpp`, `src/messages.cpp`, `tests/test_messages.cpp`, `tests/golden/*.json`

**说明**：此文件已存在内容，任务不是从零写，而是**校对并补齐到符合 spec 的统一模板**。

**各 section 要点**：

- **N.0**：本章实现消息类型到 JSON 的双向转换。读完后能理解 `encode<T>()` / `decode<T>()`、golden file 测试、以及为什么教学版选择 JSON 而不是 protobuf。

- **N.1**：从 `Channel::log()` 只接受字节数组出发，引出序列化层的必要性。

- **N.2**：展示 `messages.hpp` 中 `Timestamp`、`Duration`、`Vector3`、`Quaternion`、`Pose`、`Color` 的公开接口，以及 `encode<T>()` / `decode<T>()` 模板声明。补一个符合 spec 的章首文件地图块与深入阅读链接。
  - 💡 工程旁白 🏗️ 设计决策：golden file 测试的适用边界与维护成本（180-260字）

- **N.3**：展示 `messages.cpp` 中 `to_json` / `from_json`、`encode_base64()`、NaN → `null` 的核心逻辑。明确解释字母序输出为什么对 golden file 重要。
  - 💡 工程旁白 ⚠️ 常见陷阱：JSON 序列化的性能陷阱（180-240字）

- **N.4**：展示 `test_messages.cpp` 中 roundtrip 测试和 golden file 测试各1个；说明两类测试分别锁定什么风险。

- **N.5**：标准完成流程，tag：`my-v0.5-serialization`

- **小结**：参考 `docs/05-消息序列化.md`

**验收**：保留现有主体内容，但重构为统一模板；文件 > 220 行；包含2个工程旁白；明确列出 `tests/golden/` 的作用。

**提交**：`git commit -m "docs(tutorial): refine ch05 serialization to template"`

---

## Task 7：写 ch06-context.md

**对应 tag**：`v0.6-context`  
**起点**：`v0.5-serialization`  
**参考文档**：`docs/06-Context与Sink路由.md`  
**涉及文件**：`include/foxglove/context.hpp`, `src/context.cpp`, `tests/test_context.cpp`

**各 section 要点**：

- **N.0**：本章实现 `Context` 与 `Sink` 路由中枢。读完后能把多个 Channel 扇出到多个消费者，并理解为什么这是比“Channel 直接持有 Server/Writer 指针”更稳的架构。

- **N.1**：从反模式切入：如果 Channel 直接依赖 WebSocketServer、McapWriter、Logger，会出现什么耦合问题？引出 Context 作为中间层的必要性。

- **N.2**：展示 `context.hpp` 中 `Sink` 抽象接口、`ChannelFilter`、`Context::create()`、`default_context()`、`add_sink()`、`remove_sink()`、`create_channel()` 的接口。解释为什么这里用运行时多态而不是模板。
  - 💡 工程旁白 ⚠️ 常见陷阱：观察者模式的死锁风险——回调持锁的反模式（180-260字）

- **N.3**：展示 `context.cpp` 中 `create_channel()` 如何把 Channel 回调绑定到 `dispatch_message()`，以及 `dispatch_message()` 如何筛选 sinks 并逐个派发。补充 `WebSocketServerSink` 适配器的核心实现。
  - 💡 工程旁白 🧰 C++ 技巧：type erasure 与 `std::function` 的开销（180-240字）

- **N.4**：策略——用 `MockSink` 做单元测试，验证单 sink、多 sink、filter、生存期、默认单例。展示 `test_context.cpp` 中「多 sink 广播」和「channel filter」各1个测试。

- **N.5**：标准完成流程，tag：`my-v0.6-context`

- **小结**：参考 `docs/06-Context与Sink路由.md`

**验收**：文件 > 220 行；包含2个工程旁白；明确解释 `WebSocketServerSink::on_message()` 为什么可以是空实现。

**提交**：`git commit -m "docs(tutorial): write ch06 context"`

---

## Task 8：写 ch07a-mcap-basic.md

**对应 tag**：`v0.7a-mcap-basic`  
**起点**：`v0.6-context`  
**参考文档**：`docs/07-MCAP文件写入.md`（其中仅抽取最小实现相关部分）  
**涉及文件**：`include/foxglove/mcap.hpp`, `src/mcap.cpp`, `tests/test_mcap.cpp`

**说明**：必须把 MCAP 分成教学上独立的两个阶段。ch07a 只讲**最小可工作 writer**，不要提前展开 chunking / zstd / ChunkIndex 的细节。

**各 section 要点**：

- **N.0**：本章实现最小 MCAP writer。读完后能写出一个包含 magic、Header、Schema、Channel、Message、Footer 的合法 `.mcap` 文件，并用测试验证二进制结构。

- **N.1**：为什么除了 WebSocket 实时推送，还需要文件录制？引出“在线可视化”和“离线回放”是两条独立价值链。

- **N.2**：展示 `mcap.hpp` 中最小 `McapWriter` API：`create()`、`add_schema()`、`add_channel()`、`write_message()`、`close()`。给出本章只涉及的 record 类型地图（Magic / Header / Schema / Channel / Message / DataEnd / Footer）。
  - 💡 工程旁白 🔍 对比视角：MCAP 格式设计——为什么不用 rosbag（180-260字）

- **N.3**：展示 `src/mcap.cpp` 中 little-endian 辅助函数、record 编码、magic bytes、Footer 写入的核心逻辑。强调每条 record 都是 `opcode + length + payload`。
  - 💡 工程旁白 ⚠️ 常见陷阱：文件 I/O 的错误处理——`fwrite` / `ofstream::write` 的返回值你真的检查了吗（180-240字）

- **N.4**：策略——以二进制结构测试为主。展示 `test_mcap.cpp` 中「header/footer magic 正确」和「schema/channel/message record 存在」各1个测试。不要在本章出现 `mcap info` 外部工具依赖，把验证重点放在程序内测试。

- **N.5**：标准完成流程，tag：`my-v0.7a-mcap-basic`

- **小结**：引用 `docs/07-MCAP文件写入.md`，但注明 Ch07a 只覆盖最小实现阶段。

**验收**：文件 > 220 行；包含2个工程旁白；含一个 record 布局图或 opcode 表；严格不讲 chunk/zstd 细节。

**提交**：`git commit -m "docs(tutorial): write ch07a mcap basic"`

---

## Task 9：写 ch07b-mcap-full.md

**对应 tag**：`v0.7b-mcap-full`  
**起点**：`v0.7a-mcap-basic`  
**参考文档**：`docs/07-MCAP文件写入.md`  
**涉及文件**：`include/foxglove/mcap.hpp`, `src/mcap.cpp`, `tests/test_mcap.cpp`

**说明**：此文件已存在内容，任务是**统一到章节模板**，并修正其中对外部 `mcap` CLI 的依赖表述——完成标准必须回到项目内测试，而不是要求读者安装额外工具。

**各 section 要点**：

- **N.0**：本章在 Ch07a 的最小 writer 基础上，增加 chunking、zstd 压缩、ChunkIndex 与 `McapWriterSink` 集成。读完后能理解“先能写，再优化”的工程节奏。

- **N.1**：从最小实现的局限切入：大文件 seek 慢、空间浪费、业务代码手写落盘循环。引出为什么要进入 full writer 阶段。

- **N.2**：展示 `McapWriterOptions`、`McapCompression`、`McapWriterSink` 的接口设计，解释 `use_chunks`、`chunk_size`、Context 集成入口。
  - 💡 工程旁白 🏗️ 设计决策：MCAP basic → full 的演进——增量优化的工程节奏（180-240字）

- **N.3**：展示 `flush_chunk()`、zstd 压缩分支、CRC32、ChunkIndex 记录的核心逻辑，以及 `McapWriterSink::on_channel_added()` / `on_message()` 的关键片段。
  - 💡 工程旁白 ⚡ 性能/并发：zstd 压缩级别与 chunk 大小的工程选择（180-260字）

- **N.4**：展示 `test_mcap.cpp` 中「Chunk opcode / ChunkIndex opcode 存在」和「CRC 或压缩路径验证」各1个测试。说明为什么测试应以项目内 programmatic validation 为主，而不是依赖外部 CLI。

- **N.5**：标准完成流程，tag：`my-v0.7b-mcap-full`

- **小结**：参考 `docs/07-MCAP文件写入.md`

**验收**：保留现有章节主体信息，但整理为统一模板；删除“必须用 `mcap info` 验证”的硬依赖；文件 > 260 行；包含2个工程旁白。

**提交**：`git commit -m "docs(tutorial): refine ch07b mcap full to template"`

---

## Task 10：写 ch08-messages.md

**对应 tag**：`v0.8-messages`  
**起点**：`v0.7b-mcap-full`  
**参考文档**：`docs/08-内置消息类型.md`  
**涉及文件**：`include/foxglove/messages.hpp`, `src/messages.cpp`, `tests/test_messages.cpp`, `tests/golden/*.json`

**各 section 要点**：

- **N.0**：本章把基础消息类型扩展为可直接驱动 3D 可视化的复合消息。读完后能理解 `SceneUpdate`、`FrameTransform`、`Log`、`CompressedImage` 和 typed channel 的关系。

- **N.1**：为什么光有 `Vector3` / `Pose` 不够？引出“业务语义层”的必要性：Entity、Primitive、Update、Transform。

- **N.2**：展示 `messages.hpp` 中 `CubePrimitive`、`SpherePrimitive`、`SceneEntity`、`SceneUpdate`、`FrameTransform`、`Log`、`CompressedImage`、`TypedChannel<T>`、`SceneUpdateChannel` 等关键公开接口。解释“组合优先”的建模策略。
  - 💡 工程旁白 🧰 C++ 技巧：类型安全 channel 的 template 设计（180-240字）

- **N.3**：展示 `messages.cpp` 中复杂类型的 `to_json` / `from_json`、base64 编码/解码、枚举按整数序列化、可选字段处理等核心逻辑。
  - 💡 工程旁白 🏗️ 设计决策：Protobuf schema 描述符的自描述性——为什么教学版先走 JSON path（180-260字）

- **N.4**：展示 `test_messages.cpp` 中一个 complex type roundtrip 测试（如 `SceneUpdate`）和一个 golden file 测试（如 `CompressedImage` 或 `FrameTransform`）。说明 Ch08 是在 Ch05 的测试方法上扩展“更复杂的数据结构”。

- **N.5**：标准完成流程，tag：`my-v0.8-messages`

- **小结**：参考 `docs/08-内置消息类型.md`

**验收**：文件 > 240 行；包含2个工程旁白；明确写出 TypedChannel 如何把 `encode()` 与 `RawChannel::log()` 绑在一起。

**提交**：`git commit -m "docs(tutorial): write ch08 messages"`

---

## Task 11：写 ch09-e2e.md

**对应 tag**：`v0.9-e2e`  
**起点**：`v0.8-messages`  
**参考文档**：`docs/09-端到端连接Foxglove-Studio.md`  
**涉及文件**：`examples/ch09_e2e/main.cpp`，以及跨章节串联 `context/server/messages/mcap` 的关键调用点

**说明**：此文件已存在内容，任务是**统一到章节模板**，补足章首文件地图、深入阅读链接、标准 N.5 完成流程、小结格式。

**各 section 要点**：

- **N.0**：本章把所有前置模块装配成一条真实可跑的系统链路。读完后能启动示例、连 Foxglove Studio、同时录制 `output.mcap`。

- **N.1**：从“零件已经齐了，为什么还不等于系统”切入，引出初始化顺序、关闭顺序、错误处理链三个集成挑战。

- **N.2**：展示 `main.cpp` 中 Context、WebSocketServerSink、McapWriterSink、SceneUpdateChannel 的装配关系，给出系统架构图。解释为什么双 sink 架构能同时服务实时与离线两条链路。
  - 💡 工程旁白 🏗️ 设计决策：集成测试边界——不测外部，测组合行为（180-240字）

- **N.3**：展示动画发布循环、错误 early return、关闭顺序（先 channel/context，再 sink/server）的核心代码。可以省略具体消息构造细节，注明「完整代码见仓库 tag」。
  - 💡 工程旁白 ⚠️ 常见陷阱：测试中的时间依赖——如何避免 flaky test（180-240字）

- **N.4**：展示 `test_e2e` 或等价集成验证中的「MCAP magic bytes 校验」和「双 sink 都收到消息」两个测试思路。强调项目内验证即可，不把 Foxglove Studio 或外部 CLI 作为 CI 硬依赖。

- **N.5**：标准完成流程，tag：`my-v0.9-e2e`

- **小结**：参考 `docs/09-端到端连接Foxglove-Studio.md`

**验收**：保留现有主体内容，但整理为统一模板；文件 > 240 行；包含2个工程旁白；明确写出初始化顺序与关闭顺序。

**提交**：`git commit -m "docs(tutorial): refine ch09 e2e to template"`

---

## 最终验收与收尾

完成所有 tutorial 文档后，统一检查：

1. `docs/tutorial/README.md` + 10 个章节文件全部存在。
2. 每章都符合固定模板：章首 metadata、`N.0` 到 `N.5`、本章小结。
3. 每章都有 2–4 个工程旁白，且主题与 spec 第9节规划一致或合理贴近。
4. 每章都明确：
   - 对应 upstream tag
   - 读者应打的本地 `my-` tag
   - 唯一完成标准是 `ctest` 通过
5. 已存在章节（`ch05` / `ch07b` / `ch09`）已经被整理成统一风格，而不是保留自由散文结构。
6. 所有教程文档都用中文叙述，技术术语保留英文；Markdown 纯文本，无 HTML。

**最终检查命令**：

```bash
ls docs/tutorial/
wc -l docs/tutorial/*.md
grep -n "## .*\.0 本章" docs/tutorial/ch*.md
grep -n "## .*\.5" docs/tutorial/ch*.md
grep -n "工程旁白" docs/tutorial/ch*.md
```

**完成标准**：

- tutorial 全套文档齐备；
- 每章结构统一；
- 现有章节与新增章节风格一致；
- 计划本身足够详细，执行者无需二次猜测每章要写什么。

**最后提交**：`git commit -m "docs(plan): complete tutorial writing plan"`
