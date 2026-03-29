# 第 3 章：Channel 与 Schema——把数据流变成一等公民

> **对应 tag**：`v0.3-channel`
> **起点**：`v0.2-protocol`
> **本章新增/修改文件**：
> - `include/foxglove/schema.hpp` — 定义 `Schema` 与 `ChannelDescriptor` 这两个元数据对象
> - `include/foxglove/channel.hpp` — 定义 `MessageCallback` 与 `RawChannel` 的公开接口
> - `src/channel.cpp` — 实现 Channel 的创建、发布、关闭、重绑定和 move 语义
> - `tests/test_channel.cpp` — 验证 ID 分配、callback 透传、close、并发与重绑定行为
> **深入阅读**：[03-Channel与Schema抽象.md](../03-Channel与Schema抽象.md)
> **预计阅读时间**：55 分钟
> **前置要求**：完成第 2 章，理解 `FoxgloveResult<T>`、协议层数据结构与二进制编解码；熟悉 C++17 move 语义与 `std::function`

---

## 3.0 本章地图

前两章解决的是两件底层问题：第 1 章定义错误处理契约，第 2 章定义协议消息的结构与编解码。本章开始引入一个更像“SDK”而不是“工具函数集合”的抽象：`RawChannel`。

完成本章后，你会拥有：

- `Schema`：描述消息结构的元数据对象
- `ChannelDescriptor`：描述一条数据流的只读快照
- `RawChannel`：线程安全、回调驱动的原始消息通道
- `set_callback()`：支持后绑定 / 重绑定输出目标
- 覆盖创建、发布、关闭、重绑定、并发访问的测试
- 本地 tag `my-v0.3-channel`

本章核心问题：**在不把 Channel 直接绑死到 WebSocketServer 的前提下，如何让它安全地发布消息？**

```text
Ch01: 错误处理      Ch02: 协议层              Ch03: 通道抽象
+--------------+   +------------------+     +----------------------+
| FoxgloveError|-->| protocol.hpp/cpp |---->| Schema               |
| Result<T>    |   | ServerInfo       |     | ChannelDescriptor    |
| FOXGLOVE_TRY |   | MessageData      |     | RawChannel           |
+--------------+   +------------------+     | callback-based log() |
                                             +----------------------+
```

---

## 3.1 从需求出发

### 为什么协议层之后必须引入 Channel？

第 2 章已经能把 `MessageData` 编成二进制帧了。但如果没有 Channel，业务层每次发数据都要自己维护下面这些信息：

1. 这条数据属于哪个 topic？
2. 它的消息编码是 `json` 还是 `protobuf`？
3. 它对应哪份 schema？
4. 这条数据该发给谁？WebSocket server？文件写入器？测试 mock？

如果把这些元数据散落在业务代码里，系统会迅速失控。我们需要一个稳定的“数据流对象”来承载它们，这就是 `RawChannel`。

### 为什么不直接把 Channel 绑到 WebSocketServer？

最直观的设计是：

```cpp
class Channel {
  WebSocketServer* server_;
public:
  void log(...) { server_->send(...); }
};
```

这看上去简单，但会立刻带来三个问题：

- **测试困难**：单元测试必须先起一个 server
- **扩展困难**：以后想接入 MCAP 写入器，就得改 Channel 定义
- **生命周期耦合**：Channel 必须知道 server 何时存在、何时销毁

本章采用的是**回调解耦模型**：Channel 只知道“有人订阅我的消息”，不知道对方是谁。真正的网络发送、文件落盘、路由分发，都在后续章节通过回调或适配器接进来。

### 起点：检出 `v0.3-channel`

```bash
git checkout v0.3-channel
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期输出中至少包含：

```text
test_error
test_protocol
test_channel
100% tests passed
```

这说明第 1 章和第 2 章的基础仍然成立，而本章新增的 `test_channel` 也已经覆盖通过。

---

## 3.2 设计接口（先写头文件）

### `Schema` 与 `ChannelDescriptor`

先看 `include/foxglove/schema.hpp`：

```cpp
struct Schema {
  std::string name;
  std::string encoding;
  std::vector<uint8_t> data;

  Schema(std::string name, std::string encoding, std::vector<uint8_t> data)
      : name(std::move(name)), encoding(std::move(encoding)), data(std::move(data)) {}
};

struct ChannelDescriptor {
  uint32_t id;
  std::string topic;
  std::string encoding;
  Schema schema;

  ChannelDescriptor(uint32_t id, std::string topic, std::string encoding, Schema schema)
      : id(id), topic(std::move(topic)), encoding(std::move(encoding)), schema(std::move(schema)) {}
};
```

这里有两个关键区分：

- `Schema` 只描述“消息长什么样”
- `ChannelDescriptor` 描述“这条数据流是什么”

换句话说：多个 Channel 可以复用同一类 Schema，但每个 Channel 有自己唯一的 `id` 和 `topic`。

### `RawChannel` 的公开接口

接着看 `include/foxglove/channel.hpp`：

```cpp
using MessageCallback =
    std::function<void(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time)>;

class RawChannel final {
 public:
  static FoxgloveResult<RawChannel> create(const std::string& topic, const std::string& encoding,
                                           Schema schema,
                                           MessageCallback callback = nullptr);

  [[nodiscard]] uint32_t id() const noexcept { return id_; }
  [[nodiscard]] const ChannelDescriptor& descriptor() const noexcept { return descriptor_; }

  void log(const uint8_t* data, size_t len, uint64_t log_time);
  void close();
  void set_callback(MessageCallback callback);

  RawChannel(RawChannel&& other) noexcept;
  RawChannel& operator=(RawChannel&& other) noexcept;
  RawChannel(const RawChannel&) = delete;
  RawChannel& operator=(const RawChannel&) = delete;
  ~RawChannel() = default;
};
```

接口设计有三个重要决定：

1. **工厂函数 `create()`**：保留与前两章一致的 `FoxgloveResult<T>` 风格
2. **move-only 语义**：Channel 有内部 mutex，不适合拷贝
3. **callback 可重绑定**：`set_callback()` 允许后续章节在 Channel 创建后再接上 server/context

### 回调模型：为什么这是本章最重要的设计？

`MessageCallback` 的签名是：

```cpp
void(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time)
```

它只传递四样东西：

- channel_id
- 原始消息指针
- 消息长度
- 时间戳

没有 `WebSocketServer&`、没有 `Context&`、也没有 `McapWriter&`。这正是本设计的价值：

- Channel 负责**发布**
- callback 负责**消费**
- 谁来消费，是后绑定决策，不是 Channel 类型系统的一部分

> 💡 **🧰 C++ 技巧 工程旁白：RAII 与资源生命周期管理——Channel 的 ID 分配与回收**
>
> `RawChannel` 的核心资源不是 socket，也不是文件句柄，而是“这条数据流的身份”：`id + descriptor + callback + closed 状态`。一旦把它建模成一个 move-only 对象，生命周期边界就非常清晰：创建时分配唯一 ID，销毁时自然释放内部资源，不需要额外的 `destroy_channel(id)` 之类的 API。
>
> 这里的 RAII 不是“析构时关闭文件”那种经典场景，而是“让对象拥有自己的协议身份”。`RawChannel` 一旦构造完成，就对外承诺：我有稳定的 `id()`、稳定的 `descriptor()`、以及可控的 `close()` 行为。调用方不用自己管理“topic 到 ID 的映射表”，对象本身就是那个映射。
>
> 这也是为什么接口明确禁止拷贝：两个对象如果拥有同一个 Channel 身份，会让生命周期语义变得非常模糊。move-only 让所有权保持单一，后续接入 Server 或 Context 时不会出现“双重注销”“重复广告”等问题。

---

## 3.3 实现核心逻辑

### 原子 ID 分配

`src/channel.cpp` 里第一件值得关注的事，是静态原子计数器：

```cpp
std::atomic<uint32_t> RawChannel::next_channel_id_{1};
```

对应的创建逻辑：

```cpp
FoxgloveResult<RawChannel> RawChannel::create(const std::string& topic,
                                              const std::string& encoding, Schema schema,
                                              MessageCallback callback) {
  uint32_t id = next_channel_id_.fetch_add(1, std::memory_order_relaxed);
  ChannelDescriptor descriptor{id, topic, encoding, std::move(schema)};
  return RawChannel{id, std::move(descriptor), std::move(callback)};
}
```

这里用 `memory_order_relaxed` 是合理的：我们只需要“唯一递增 ID”，不需要通过这个原子变量同步其他共享状态。要注意，**保证 ID 唯一的是 `fetch_add` 的原子性本身**；`memory_order_relaxed` 只是不额外承担跨线程同步其他共享数据的职责。

### `log()`：发布路径的最小闭环

```cpp
void RawChannel::log(const uint8_t* data, size_t len, uint64_t log_time) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (closed_ || !callback_) {
    return;
  }

  callback_(id_, data, len, log_time);
}
```

这段代码短得近乎“无聊”，但它定义了整个系统的路由基线：

- 如果 channel 已关闭，`log()` 直接 no-op
- 如果没有 callback，也 no-op
- 否则，把消息原样交给 callback

它没有做序列化，没有做网络发送，没有做 topic 过滤——这些都不是 Channel 的职责。

### `close()` 与 `set_callback()`

```cpp
void RawChannel::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  closed_ = true;
}

void RawChannel::set_callback(MessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
}
```

这两个 API 看起来简单，但正是它们让后续章节成为可能：

- 第 4 章：在当前仓库实现里，`WebSocketServer::add_channel()` 会通过 `set_callback()` 把自己的分发逻辑接进来
- 第 6 章：`Context::create_channel()` 也会通过 callback 把消息路由到 sink

如果没有 `set_callback()`，Channel 一创建就必须知道最终接收方是谁，整个架构会立刻僵化。

### move 语义实现

```cpp
RawChannel::RawChannel(RawChannel&& other) noexcept
    : id_(other.id_),
      descriptor_(std::move(other.descriptor_)),
      callback_(std::move(other.callback_)),
      mutex_(),
      closed_(other.closed_) {}

RawChannel& RawChannel::operator=(RawChannel&& other) noexcept {
  if (this != &other) {
    std::lock(mutex_, other.mutex_);
    std::lock_guard<std::mutex> lock(mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> other_lock(other.mutex_, std::adopt_lock);

    id_ = other.id_;
    descriptor_ = std::move(other.descriptor_);
    callback_ = std::move(other.callback_);
    closed_ = other.closed_;
  }
  return *this;
}
```

重点不是“怎么 move”，而是“为什么 mutex 不能 move”。标准库 mutex 不可移动，所以目标对象必须拥有一个新的默认构造 mutex。这也是 copy 被禁掉、move 被手写实现的原因。move 之后的源对象仍然**有效但状态未指定**：你可以安全析构它，但不应该继续依赖它原来的 descriptor、callback 或 closed 状态。

> 💡 **⚡ 性能/并发 工程旁白：`std::string_view` vs `std::string`——零拷贝的代价**
>
> 初看 `RawChannel::create(const std::string& topic, const std::string& encoding, ...)`，很多人会问：为什么不用 `std::string_view`？这样不是能少一次拷贝吗？
>
> 这里真正的问题不是“能不能零拷贝”，而是“谁拥有这段内存”。如果 `RawChannel` 把 `topic` 和 `encoding` 存成 `string_view`，那它们引用的底层字符串必须在 Channel 生命周期内一直有效。对调用方来说，这几乎是一个隐藏炸弹：传个临时字符串，程序就埋下悬垂引用。
>
> 用 `std::string` 虽然多一次拷贝，但换来的是明确所有权：Channel 自己拥有 topic 和 encoding。对于这种低频构造、高频读取的元数据对象，这是更稳的工程选择。真正值得做零拷贝优化的，是高频消息载荷本身，而不是构造时只拷贝一次的 topic 名称。

---

## 3.4 测试：验证正确性

本章测试的重点不是协议字节，而是对象语义：创建、发布、关闭、并发、重绑定。

### 基础行为：ID 与 callback

```cpp
TEST_CASE("Channel - assigns unique sequential IDs") {
  auto result1 = RawChannel::create("/topic1", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result1.has_value());
  auto channel1 = std::move(result1.value());
  REQUIRE(channel1.id() == 1);

  auto result2 = RawChannel::create("/topic2", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(result2.has_value());
  auto channel2 = std::move(result2.value());
  REQUIRE(channel2.id() == 2);
}
```

```cpp
TEST_CASE("Channel - log invokes callback with correct data") {
  std::vector<uint8_t> received_data;
  uint32_t received_channel_id = 0;
  uint64_t received_log_time = 0;
  bool callback_invoked = false;

  MessageCallback callback = [&](uint32_t channel_id, const uint8_t* data, size_t len,
                                 uint64_t log_time) {
    received_channel_id = channel_id;
    received_data.assign(data, data + len);
    received_log_time = log_time;
    callback_invoked = true;
  };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
  channel.log(test_data.data(), test_data.size(), 1234567890ULL);

  REQUIRE(callback_invoked);
  REQUIRE(received_channel_id == channel.id());
  REQUIRE(received_data == test_data);
}
```

这两组测试分别锁定两件事：

- ID 分配是否稳定
- `log()` 是否准确把参数透传给 callback

### 生命周期：`close()` 后 no-op

```cpp
TEST_CASE("Channel - close prevents further logging") {
  int callback_count = 0;
  MessageCallback callback = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  REQUIRE(result.has_value());
  auto channel = std::move(result.value());

  std::vector<uint8_t> test_data = {0x01, 0x02};
  channel.log(test_data.data(), test_data.size(), 1ULL);
  REQUIRE(callback_count == 1);

  channel.close();
  channel.log(test_data.data(), test_data.size(), 2ULL);
  REQUIRE(callback_count == 1);
}
```

这证明 `close()` 真正改变了对象状态，而不是只是一个“空函数占位”。

### 重绑定与并发

```cpp
TEST_CASE("Channel - set_callback rebinds output") {
  int callback_a_count = 0;
  int callback_b_count = 0;

  MessageCallback callback_a = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_a_count++; };
  MessageCallback callback_b = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_b_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback_a);
  auto channel = std::move(result.value());
  std::vector<uint8_t> test_data = {0x01, 0x02};

  channel.log(test_data.data(), test_data.size(), 1ULL);
  channel.set_callback(callback_b);
  channel.log(test_data.data(), test_data.size(), 2ULL);

  REQUIRE(callback_a_count == 1);
  REQUIRE(callback_b_count == 1);
}
```

```cpp
TEST_CASE("Channel - concurrent log from multiple threads") {
  constexpr int num_threads = 4;
  constexpr int messages_per_thread = 100;
  std::atomic<int> callback_count{0};

  MessageCallback callback = [&](uint32_t, const uint8_t*, size_t, uint64_t) { callback_count++; };

  auto result = RawChannel::create("/test_topic", "json", Schema{"TestSchema", "jsonschema", {}},
                                   callback);
  auto channel = std::move(result.value());

  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < messages_per_thread; ++i) {
        std::vector<uint8_t> data = {static_cast<uint8_t>(t), static_cast<uint8_t>(i)};
        channel.log(data.data(), data.size(), static_cast<uint64_t>(t * 1000 + i));
      }
    });
  }
  for (auto& t : threads) t.join();

  REQUIRE(callback_count == num_threads * messages_per_thread);
}
```

`set_callback()` 的测试验证了“后绑定”能力；并发测试验证了 mutex + atomic 的组合确实起作用，而不是纸面设计。

### 运行测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期至少看到：

```text
test_error
test_protocol
test_channel
100% tests passed
```

---

## 3.5 与官方实现对比

官方 Foxglove SDK 的 Channel 体系更偏向“Context 中心化”，而本教程在这里刻意走了一条更适合教学的路径。

| 方面 | 本教程实现 | 官方 SDK 倾向 |
|------|-----------|--------------|
| Channel 输出 | callback 驱动 | 更紧密地接入 Context / sink 体系 |
| 绑定时机 | 创建时可空，后续 `set_callback()` | 通常在更高层路由体系中完成 |
| 测试方式 | 直接注入 lambda/mock callback | 倾向通过上层系统集成验证 |
| 教学价值 | 高：数据流向完全可见 | 高：架构更完整，但对初学者更重 |

### 为什么本教程强调 callback 模型？

因为它把“发布”与“消费”明确切开：

- `RawChannel` 只负责维护 topic / encoding / schema / 生命周期
- WebSocketServer、Context、McapWriter 都可以作为后续绑定目标

这对教学特别重要。你可以在第 3 章只理解 Channel 本身，而不需要提前懂第 4 章 server、第 6 章 context、第 7 章 mcap。

### 官方实现更强的地方

官方实现的优势不在于“更复杂”，而在于“更完整”：

- 更成熟的路由体系
- 与完整协议栈更紧密集成
- 某些资源管理放在更高层统一协调

本教程版并不是要替代官方，而是把它拆开给你看：先让你看清 `RawChannel` 的最小职责，再在后续章节一点点把系统组装回去。

---

## 3.6 打 tag，验证完成

完成本章后，按统一流程验证：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

git add .
git commit -m "feat(ch3): add channel and schema abstraction"
git tag my-v0.3-channel
git diff v0.3-channel HEAD -- include/foxglove/channel.hpp
git diff v0.3-channel HEAD -- src/channel.cpp
```

其中：

- `ctest` 全绿是唯一硬标准
- `git diff` 只是辅助你和参考实现做对照
- `my-v0.3-channel` 用 `my-` 前缀，避免污染上游 tag

如果测试失败，优先检查：

1. ID 分配是否仍然从 `1` 开始
2. `log()` 是否在 `closed_` 或空 callback 时正确 no-op
3. `set_callback()` 是否真的替换了旧回调
4. move assignment 是否正确锁住两个 mutex

---

## 本章小结

本章把“协议消息”提升成了“数据流对象”。

**核心交付物**：

| 文件 | 作用 |
|------|------|
| `include/foxglove/schema.hpp` | 定义 `Schema` 与 `ChannelDescriptor` |
| `include/foxglove/channel.hpp` | 定义 `MessageCallback` 与 `RawChannel` 公开接口 |
| `src/channel.cpp` | 实现创建、发布、关闭、重绑定、move 语义 |
| `tests/test_channel.cpp` | 覆盖创建、回调、关闭、并发、重绑定、move 语义 |

**本章学到的工程知识**：

- callback 模型让 Channel 不必直接耦合到 WebSocketServer
- `std::atomic<uint32_t>` 足以解决 ID 唯一分配问题
- `std::mutex` 保护发布路径和生命周期状态
- move-only 语义比可拷贝对象更适合带锁资源
- `std::string_view` 不是默认更优，所有权比少一次拷贝更重要

**下一章**：第 4 章将把 `RawChannel` 真正接到 `WebSocketServer` 上。你会看到 `set_callback()` 为什么是一个关键设计点——它让 server 可以在运行时把自己的 dispatch 逻辑接进来，而不需要 Channel 预先知道网络层的存在。
