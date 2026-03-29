# 第 2 章：Foxglove WebSocket 协议——数据结构与编解码

> **对应 tag**：`v0.2-protocol`
> **预计阅读时间**：60 分钟
> **前置要求**：完成第 1 章，理解 `FoxgloveResult<T>` 和 `FOXGLOVE_TRY` 宏；熟悉基本的网络协议概念（JSON、二进制帧）

---

## 2.0 本章地图

第 1 章打好了「错误处理的语言」，本章在此基础上实现 Foxglove WebSocket 协议的完整数据结构与编解码层。

完成本章后，你会拥有：

- 描述 Foxglove WebSocket 协议所有关键消息的 C++ 数据结构
- 将服务端消息序列化为 JSON 或二进制帧的编码器
- 将客户端消息从 JSON/二进制还原为结构体的解码器
- 覆盖正常路径和错误路径的单元测试（7 个用例）
- 打好本地 tag `my-v0.2-protocol`，为后续章节的 Channel / Server 实现奠定基础

本章核心问题：**在 C++ 里，如何把一份网络协议规范映射成类型安全、可测试的编解码器？**

```
第 1 章                第 2 章                第 3 章
+--------------+       +------------------+       +--------------+
| FoxgloveError|------>| protocol.hpp/cpp |------>| Channel &    |
| FoxgloveResult|      | ServerInfo       |       | Schema 抽象  |
| FOXGLOVE_TRY |      | ChannelAdvert.   |       |              |
+--------------+       | MessageData      |       +--------------+
                       | ClientMessage    |
                       | encode_*/decode_*|
                       +------------------+
```

---

## 2.1 从需求出发

### Foxglove WebSocket 协议是什么？

Foxglove Studio 通过 WebSocket 连接到数据源（机器人、仿真器、回放服务器）。连接建立后，双方按照 Foxglove WebSocket Protocol 规范交换消息。协议分两层：

- **文本层（JSON）**：连接握手、话题广播、订阅管理——低频控制消息
- **二进制层**：实际的传感器数据帧——高频数据消息

从服务端视角看，完整的会话流程如下：

```
客户端 (Foxglove Studio)          服务端 (我们的 SDK)
        |                                 |
        |---- WebSocket 握手 ------------>|
        |<--- serverInfo (JSON) ----------|  "我是谁，我能做什么"
        |<--- advertise (JSON) -----------|  "我有哪些话题"
        |                                 |
        |---- subscribe (JSON) ---------->|  "我要订阅话题 X"
        |<--- MessageData (binary) -------|  "话题 X 的数据帧"
        |<--- MessageData (binary) -------|
        |                                 |
        |---- unsubscribe (JSON) -------->|
        |---- WebSocket 关闭 ------------>|
```

### 本章的任务

把上图中每一种消息映射成 C++ 数据结构，并实现对应的编解码函数。需求归结为三条：

1. **类型安全**：每种消息有独立的结构体，字段名与协议规范一一对应，不用裸 `std::map<string,string>` 传递。
2. **错误可见**：编解码失败时返回 `FoxgloveResult` 错误，不抛异常，调用方能在编译期强制处理。
3. **可测试**：编码后再解码必须还原原始数据（roundtrip），边界情况（截断帧、非法 opcode）能被捕获。

### 起点：检出协议 tag

```bash
git checkout v0.2-protocol
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期输出：

```
100% tests passed, 0 tests failed out of 2
```

两个测试套件——`test_error`（继承自第 1 章）和 `test_protocol`（本章新增）全部通过。

---

## 2.2 设计接口（先写头文件）

在动手写实现之前，先把 `include/foxglove/protocol.hpp` 读完整——接口设计决定了后续一切。

### 数据结构一览

```cpp
// include/foxglove/protocol.hpp（节选）

struct ServerInfo {
  std::string name;
  uint32_t    capabilities = 0;
  std::vector<std::string>           supported_encodings;
  std::map<std::string, std::string> metadata;
  std::string session_id;
  std::string protocol_version;
};

struct ChannelAdvertisement {
  uint32_t    id;
  std::string topic;
  std::string encoding;        // "json", "protobuf" ...
  std::string schema_name;
  std::string schema_encoding;
  std::string schema_data;     // 序列化为 JSON 时键名为 "schema"，而非 "schemaData"
};

struct Subscription {
  uint32_t subscription_id;  // 客户端本地 ID
  uint32_t channel_id;       // 服务端全局 ID
};

struct Subscribe   { std::vector<Subscription> subscriptions; };
struct Unsubscribe { std::vector<uint32_t> subscription_ids; };
using  ClientMessage = std::variant<Subscribe, Unsubscribe>;

struct MessageData {
  uint32_t             subscription_id;
  uint64_t             log_time;   // nanoseconds
  std::vector<uint8_t> data;
};
```

几个设计要点值得停下来看看：

**`ClientMessage` 用 `std::variant`，而不是继承体系。** 协议消息种类有限且互斥，variant 在编译期强制你处理所有分支（`std::get_if` 或 `std::visit`），漏掉任意一种会报编译错或警告，继承加 `dynamic_cast` 做不到这一点。

**`capabilities` 用 `uint32_t` 位掩码，而不是 `bool` 字段的平铺。** 官方协议规范传输的就是整数，直接映射避免序列化时再做转换。

**`subscription_id` 与 `channel_id` 分开。** 两者含义不同：`channel_id` 由服务端分配、全局唯一；`subscription_id` 由客户端分配、在单次会话中区分同一频道的多个订阅。混用是常见 bug。

**`schema_data` 字段序列化为 JSON 键 `"schema"`，而不是 `"schemaData"`。** 这是 Foxglove 协议规范的明确要求，与其他字段的 camelCase 命名不同。若写成 `"schemaData"`，Foxglove Studio 将无法识别 schema，话题解析会静默失败。实现中请务必核对：

```cpp
// src/protocol.cpp — ChannelAdvertisement 编码（关键字段）
j["schema"] = ch.schema_data;   // 正确：协议规定键名为 "schema"
// j["schemaData"] = ch.schema_data;  // 错误：Studio 无法识别
```

### 编解码函数签名

```cpp
// 服务端输出（编码）
FoxgloveResult<std::string>          encode_server_info(const ServerInfo&);
FoxgloveResult<std::string>          encode_advertise(const std::vector<ChannelAdvertisement>&);
FoxgloveResult<std::vector<uint8_t>> encode_message_data(const MessageData&);

// 客户端输入（解码）
FoxgloveResult<ClientMessage>        decode_client_message(const std::string& json);
FoxgloveResult<MessageData>          decode_message_data_binary(const std::vector<uint8_t>&);
```

所有函数返回 `FoxgloveResult<T>`——第 1 章建立的约定在这里发挥作用：调用方必须检查错误，编译器不让你忽略。

---

## 2.3 实现核心逻辑

### JSON 编码：`encode_server_info`

```cpp
// src/protocol.cpp（节选）

FoxgloveResult<std::string> encode_server_info(const ServerInfo& info) {
  try {
    nlohmann::json json;
    json["op"]           = "serverInfo";
    json["name"]         = info.name;
    json["capabilities"] = info.capabilities;

    if (!info.protocol_version.empty())
      json["protocolVersion"] = info.protocol_version;
    if (!info.supported_encodings.empty())
      json["supportedEncodings"] = info.supported_encodings;
    if (!info.metadata.empty())
      json["metadata"] = info.metadata;
    if (!info.session_id.empty())
      json["sessionId"] = info.session_id;

    return json.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}
```

可选字段只在非空时写入，不向客户端发送无意义的空数组或空字符串——这与官方 SDK 的行为一致。

### 二进制编码：`encode_message_data`

`MessageData` 走二进制帧，帧结构如下（所有多字节字段均为**小端序**；跨平台陷阱见本节末尾的「工程旁白」）：

```
字节偏移   大小    类型       含义
────────────────────────────────────────────
0          1 B    uint8     opcode = 0x01
1-4        4 B    uint32    subscription_id（小端序）
5-12       8 B    uint64    log_time，单位纳秒（小端序）
13-N       N B    uint8[]   消息载荷

┌──────┬──────────────────┬──────────────────────────────────┬─────────────┐
│ 0x01 │   sub_id (4 B)   │         log_time (8 B)           │ payload (N) │
│  1B  │  LE uint32       │         LE uint64                │             │
└──────┴──────────────────┴──────────────────────────────────┴─────────────┘
 [0]   [1]  [2]  [3]  [4]  [5]  [6]  [7]  [8]  [9] [10] [11] [12]  [13...]
```

实现时逐字节位移，不用 `memcpy`（原因见下方工程旁白）：

```cpp
// src/protocol.cpp（节选）

constexpr uint8_t OPCODE_MESSAGE_DATA = 0x01;

FoxgloveResult<std::vector<uint8_t>> encode_message_data(const MessageData& msg) {
  try {
    size_t total_size = 1 + 4 + 8 + msg.data.size();
    std::vector<uint8_t> result;
    result.reserve(total_size);

    result.push_back(OPCODE_MESSAGE_DATA);

    // subscription_id，小端序逐字节写入
    result.push_back(static_cast<uint8_t>( msg.subscription_id        & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >>  8) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 24) & 0xFF));

    // log_time，小端序 8 字节
    for (size_t i = 0; i < 8; ++i)
      result.push_back(static_cast<uint8_t>((msg.log_time >> (i * 8)) & 0xFF));

    result.insert(result.end(), msg.data.begin(), msg.data.end());
    return result;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}
```

### JSON 解码：`decode_client_message`

客户端消息共两种，用 `op` 字段区分：

```cpp
// src/protocol.cpp（节选）

FoxgloveResult<ClientMessage> decode_client_message(const std::string& json_str) {
  try {
    auto json = nlohmann::json::parse(json_str);
    auto op   = json.at("op").get<std::string>();

    if (op == "subscribe") {
      if (!json.contains("subscriptions"))
        return tl::make_unexpected(FoxgloveError::ProtocolError);
      Subscribe sub;
      for (const auto& s : json["subscriptions"]) {
        sub.subscriptions.push_back({
          s.at("id").get<uint32_t>(),
          s.at("channelId").get<uint32_t>()
        });
      }
      return sub;
    }

    if (op == "unsubscribe") {
      if (!json.contains("subscriptionIds"))
        return tl::make_unexpected(FoxgloveError::ProtocolError);
      Unsubscribe unsub;
      for (const auto& id : json["subscriptionIds"])
        unsub.subscription_ids.push_back(id.get<uint32_t>());
      return unsub;
    }

    return tl::make_unexpected(FoxgloveError::ProtocolError);
  } catch (const nlohmann::json::exception&) {
    return tl::make_unexpected(FoxgloveError::ProtocolError);
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::ProtocolError);
  }
}
```

未知 `op` 直接返回 `ProtocolError`——宁可拒绝，不要猜测。

---

> ### 工程旁白：小端序与跨平台陷阱——为什么不能直接 memcpy
>
> 初学者常见的写法：
>
> ```cpp
> // 看起来简洁，但在大端序机器上会悄悄出错
> uint32_t sub_id = msg.subscription_id;
> std::memcpy(buf.data() + 1, &sub_id, 4);
> ```
>
> 这段代码在 x86/ARM（小端序）机器上碰巧正确，但在大端序平台（SPARC、某些 MIPS、旧版 PowerPC）上，内存中的字节顺序是反的，读出来的值完全错误，编译器不报任何警告。
>
> Foxglove 协议规范明确要求小端序。正确做法是逐位移位，与平台字节序无关：
>
> ```cpp
> // 无论在哪台机器上，写入的字节顺序都是确定的
> buf.push_back(static_cast<uint8_t>( value        & 0xFF));  // 最低字节在前
> buf.push_back(static_cast<uint8_t>((value >>  8) & 0xFF));
> buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
> buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
> ```
>
> C++20 引入了 `std::endian` 和 `std::byteswap`，可以写出更具表达力的跨平台代码。C++17 没有这些，位移法是最可靠的兼容方案。
>
> **结论**：凡是涉及二进制协议序列化，永远不要依赖平台字节序，永远显式指定每个字节的位置。

---

## 2.4 测试：验证正确性

本章测试分三类：编码正确性、解码正确性、错误路径。共 7 个用例，全在 `tests/test_protocol.cpp`。

### 编码正确性：`serverInfo`

```cpp
// tests/test_protocol.cpp（节选）

TEST_CASE("Protocol - serverInfo encodes to JSON") {
  ServerInfo info;
  info.name                = "Test Server";
  info.capabilities        = 3;  // 原始位掩码值：bit0=ClientPublish, bit1=ConnectionGraph（本章不定义枚举常量，直接用整数）
  info.supported_encodings = {"json", "protobuf"};
  info.metadata            = {{"version", "1.0.0"}, {"environment", "test"}};
  info.session_id          = "test-session-123";

  auto json_str = encode_server_info(info);
  REQUIRE(json_str.has_value());

  auto json = nlohmann::json::parse(json_str.value());
  REQUIRE(json["op"]   == "serverInfo");
  REQUIRE(json["name"] == "Test Server");
  REQUIRE(json["capabilities"] == 3);
  REQUIRE(json["supportedEncodings"].size() == 2);
  REQUIRE(json["sessionId"] == "test-session-123");
}
```

测试直接解析产出的 JSON 再验证字段，比比较原始字符串更稳健——JSON 键的顺序不保证。

### Roundtrip 测试：`MessageData`

```cpp
TEST_CASE("Protocol - messageData binary roundtrip") {
  MessageData msg;
  msg.subscription_id = 42;
  msg.log_time        = 1234567890ULL;
  msg.data            = {0x01, 0x02, 0x03, 0x04, 0x05};

  auto encoded = encode_message_data(msg);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value().size() == 1 + 4 + 8 + 5);  // opcode + sub_id + log_time + data
  REQUIRE(encoded.value()[0] == 0x01);                // opcode 校验

  auto decoded = decode_message_data_binary(encoded.value());
  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value().subscription_id == 42);
  REQUIRE(decoded.value().log_time        == 1234567890ULL);
  REQUIRE(decoded.value().data            == msg.data);
}
```

Roundtrip 是二进制协议测试的标准模式：encode 再 decode，还原必须与原始完全一致。

### 错误路径测试

```cpp
TEST_CASE("Protocol - malformed JSON returns error") {
  std::string malformed = "{not valid json";
  auto result = decode_client_message(malformed);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::ProtocolError);
}

TEST_CASE("Protocol - malformed binary returns error") {
  // 截断帧：只有 5 字节，缺少 log_time
  std::vector<uint8_t> truncated = {0x01, 0x01, 0x00, 0x00, 0x00};
  auto result = decode_message_data_binary(truncated);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::ProtocolError);

  // 非法 opcode
  std::vector<uint8_t> wrong_opcode(13, 0x00);
  wrong_opcode[0] = 0xFF;
  result = decode_message_data_binary(wrong_opcode);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::ProtocolError);
}
```

错误路径与正常路径同等重要。协议解码器在生产中会收到各种畸形数据，这些测试证明它们被干净地拒绝，而不是引发 UB 或崩溃。

### 运行测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期输出：

```
Test project /path/to/build
    Start 1: test_error
1/2 Test #1: test_error .......................   Passed    0.00 sec
    Start 2: test_protocol
2/2 Test #2: test_protocol ....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
```

---

## 2.5 与官方实现对比

我们的实现覆盖了协议核心，但刻意保持简单。与 Foxglove 官方 SDK 对比：

| 方面 | 官方 SDK（Rust） | 本章实现（C++17） |
|------|-----------------|------------------|
| JSON 库 | `serde_json` | `nlohmann/json` |
| 二进制序列化 | `bytes` crate | 手动位移 |
| 消息类型 | 完整协议支持 | 核心子集（5 种） |
| 零拷贝 | `Cow<'a, [u8]>` | `std::vector`（有复制） |
| 错误处理 | `Result<T, E>` | `FoxgloveResult<T>` |
| 可选字段 | `Option<T>` | 非空判断 |

### 官方 Rust 实现参考

官方 SDK 的 `MessageData` 用 `Cow` 避免载荷复制：

```rust
// 官方 foxglove-sdk/rust 实现（示意）
pub struct MessageData<'a> {
    pub subscription_id: u32,
    pub log_time: u64,
    pub data: Cow<'a, [u8]>,
}

impl BinaryPayload for MessageData<'_> {
    fn write_payload(&self, buf: &mut impl BufMut) {
        buf.put_u32_le(self.subscription_id);
        buf.put_u64_le(self.log_time);
        buf.put_slice(&self.data);
    }
}
```

`put_u32_le` / `put_u64_le` 在 `bytes` crate 内部同样是位移实现，只是封装得更整洁。我们的 C++ 实现做了相同的事，只是更显式。

**为什么我们不用 `Cow` / 零拷贝？** 本章目标是让协议逻辑清晰可读，`std::vector` 的拷贝开销在教学阶段不是瓶颈。第 4 章集成 libwebsockets 时，你会看到在实际数据路径上如何规避不必要的复制。

### 字段名映射规律

协议 JSON 用 camelCase，C++ 结构体用 snake_case：

| JSON 字段 | C++ 字段 |
|-----------|----------|
| `sessionId` | `session_id` |
| `schemaName` | `schema_name` |
| `channelId` | `channel_id` |
| `subscriptionIds` | `subscription_ids` |
| `supportedEncodings` | `supported_encodings` |

所有映射都在 `protocol.cpp` 的编解码函数中集中处理，调用方只看 snake_case，不用关心 JSON 键名。

---

> ### 工程旁白：二进制协议向后兼容性设计
>
> `MessageData` 帧当前是 `opcode(1) + sub_id(4) + log_time(8) + payload(N)`，共 13 字节固定头部。如果未来协议需要新增字段（比如优先级、压缩标志），怎么办？
>
> **版本 1 做法（当前）**：opcode = `0x01` 固定，帧格式固定，不可扩展。
>
> **向后兼容的常见策略**：
>
> 1. **opcode 扩展**：新消息类型用新 opcode，旧解码器收到未知 opcode 时返回错误（跳过或关闭连接）。这是 Foxglove 协议实际采用的方式。
>
> 2. **长度前缀**：在头部加一个 `header_len` 字段，新版本扩展头部时旧解码器可以按 `header_len` 跳过不认识的字段，只读自己懂的部分。Protocol Buffers 的 `oneof` 演进思路类似。
>
> 3. **能力协商**：`serverInfo` 里的 `capabilities` 位掩码就是这个思路——双方握手时互报能力，只启用双方都支持的特性。
>
> 本章实现没有扩展机制，这是有意为之的简化。真实生产代码中，协议版本管理是设计阶段就要考虑的问题，不是事后打补丁的。

---

## 2.6 打 tag，验证完成

完成所有实现并通过测试后，打本地 tag 记录进度：

```bash
# 确认测试全部通过
ctest --test-dir build --output-on-failure

# 打本地 tag（my- 前缀，不污染上游 tag）
git tag my-v0.2-protocol

# 可选：查看与参考实现的差异
git diff v0.2-protocol HEAD -- src/protocol.cpp
git diff v0.2-protocol HEAD -- include/foxglove/protocol.hpp
```

`git diff` 是辅助参考，不是门槛。只要 `ctest` 全部通过，本章就完成了。差异可能来自字段顺序、注释风格等无关紧要的地方。

如果 `ctest` 有失败，常见原因和排查方向：

| 症状 | 可能原因 |
|------|----------|
| `encode_server_info` 测试失败 | 可选字段条件判断错误，或 JSON 键名拼写错误（camelCase vs snake_case） |
| `messageData roundtrip` 失败 | 字节偏移计算错误，或 log_time 字节序反了 |
| `malformed binary` 测试失败 | 最小长度检查（13 字节）缺失，或 opcode 校验缺失 |
| 编译失败 | `tl::expected` 头文件未引入，或 `nlohmann/json` 未在 CMakeLists.txt 中链接 |

---

## 本章小结

本章在第 1 章错误处理基础上，实现了完整的 Foxglove WebSocket 协议编解码层。

**核心交付物**：

| 文件 | 作用 |
|------|------|
| `include/foxglove/protocol.hpp` | 协议数据结构（`ServerInfo`、`ChannelAdvertisement`、`MessageData`、`Subscribe`/`Unsubscribe`）与 5 个编解码函数签名 |
| `src/protocol.cpp` | JSON 编解码（nlohmann/json）+ 二进制帧编解码（手动位移） |
| `tests/test_protocol.cpp` | 7 个测试用例，覆盖正常路径和错误路径 |

**学到的工程知识**：

- `std::variant` 比继承体系更适合表达有限、互斥的消息类型集合
- 二进制协议序列化必须显式处理字节序，不能依赖 `memcpy` 的平台假设
- Roundtrip 测试是验证编解码一致性的标准模式
- 可选字段只在非空时写入，保持与官方 SDK 行为一致
- 协议向后兼容要在设计阶段考虑，`capabilities` 位掩码是协商机制的示范

**下一步**：第 3 章在本章协议层之上实现 `Channel` 和 `Schema` 抽象——管理多路数据流的生命周期和类型信息。

---
