# 第二章：Foxglove WebSocket 协议解析

本章实现 Foxglove WebSocket Protocol v1 的核心消息编解码，包括 serverInfo、advertise、subscribe/unsubscribe 和 messageData。

## 2.1 Foxglove WebSocket 协议概述

### 协议分层

Foxglove WebSocket 协议在标准 WebSocket 之上定义了应用层消息格式：

```
┌─────────────────────────────────────────┐
│  Application Layer (Foxglove Messages)  │
│  - serverInfo (JSON)                    │
│  - advertise (JSON)                     │
│  - subscribe/unsubscribe (JSON)         │
│  - messageData (Binary)                 │
├─────────────────────────────────────────┤
│  WebSocket Frame (RFC 6455)             │
│  - Text frames for JSON messages        │
│  - Binary frames for messageData        │
├─────────────────────────────────────────┤
│  TCP/SSL                                │
└─────────────────────────────────────────┘
```

### 消息方向

| 方向 | 消息类型 | 格式 |
|------|----------|------|
| Server → Client | serverInfo, advertise, status | JSON |
| Server → Client | messageData, time | Binary |
| Client → Server | subscribe, unsubscribe | JSON |
| Client → Server | clientMessageData | Binary |

## 2.2 JSON 消息格式

### ServerInfo 消息

服务器在客户端连接后立即发送，告知服务器能力和配置：

```json
{
  "op": "serverInfo",
  "name": "example server",
  "capabilities": ["clientPublish", "time"],
  "supportedEncodings": ["json"],
  "metadata": {"key": "value"},
  "sessionId": "1675789422160"
}
```

能力位掩码定义（简化版）：

```cpp
enum class Capability : uint32_t {
  None = 0,
  ClientPublish = 1 << 0,   // 允许客户端发布消息
  ConnectionGraph = 1 << 1, // 支持连接图更新
  Parameters = 1 << 2,      // 支持参数读写
  Time = 1 << 3,            // 支持时间消息
  Services = 1 << 4,        // 支持服务调用
  Assets = 1 << 5,          // 支持资源获取
};
```

### Advertise 消息

服务器广播可用频道（channels），每个频道描述一个数据流：

```json
{
  "op": "advertise",
  "channels": [
    {
      "id": 1,
      "topic": "/robot/pose",
      "encoding": "json",
      "schemaName": "geometry_msgs/Pose",
      "schema": "base64_encoded_schema_data"
    }
  ]
}
```

### Subscribe/Unsubscribe 消息

客户端订阅/取消订阅频道：

```json
// Subscribe
{
  "op": "subscribe",
  "subscriptions": [
    {"id": 0, "channelId": 1}
  ]
}

// Unsubscribe
{
  "op": "unsubscribe",
  "subscriptionIds": [0]
}
```

注意：`subscription.id` 是客户端分配的本地 ID，`channelId` 是服务器分配的全局频道 ID。

## 2.3 二进制消息格式

### MessageData 帧结构

| 字节 | 类型 | 描述 |
|------|------|------|
| 0 | uint8 | Opcode = 0x01 |
| 1-4 | uint32 | subscription_id (小端序) |
| 5-12 | uint64 | log_time (纳秒，小端序) |
| 13+ | uint8[] | 消息载荷 |

```
┌─────┬─────────────┬────────────────────┬──────────┐
│0x01 │  sub_id     │     log_time       │  payload │
│ 1B  │    4B       │       8B           │   N B    │
└─────┴─────────────┴────────────────────┴──────────┘
```

### 为什么使用小端序 (Little-Endian)

1. **Foxglove 官方规范要求**: 协议明确指定小端序
2. **主流架构兼容性**: x86-64 和 ARM 默认小端序，无需转换
3. **网络协议惯例**: WebSocket 等现代协议多采用小端序

```cpp
// 编码示例：小端序写入 uint32_t
void put_u32_le(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

// 解码示例：小端序读取 uint32_t
uint32_t get_u32_le(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}
```

## 2.4 代码实现

### 数据结构定义

```cpp
// include/foxglove/protocol.hpp
struct ServerInfo {
  std::string name;
  uint32_t capabilities = 0;
  std::vector<std::string> supported_encodings;
  std::map<std::string, std::string> metadata;
  std::string session_id;
};

struct ChannelAdvertisement {
  uint32_t id;
  std::string topic;
  std::string encoding;
  std::string schema_name;
  std::string schema_encoding;
  std::string schema_data;
};

struct Subscription {
  uint32_t subscription_id;
  uint32_t channel_id;
};

struct Subscribe { std::vector<Subscription> subscriptions; };
struct Unsubscribe { std::vector<uint32_t> subscription_ids; };

using ClientMessage = std::variant<Subscribe, Unsubscribe>;

struct MessageData {
  uint32_t subscription_id;
  uint64_t log_time;
  std::vector<uint8_t> data;
};
```

### JSON 编码实现

使用 nlohmann/json 进行 JSON 序列化：

```cpp
// src/protocol.cpp
FoxgloveResult<std::string> encode_server_info(const ServerInfo& info) {
  try {
    nlohmann::json json;
    json["op"] = "serverInfo";
    json["name"] = info.name;
    json["capabilities"] = info.capabilities;
    if (!info.supported_encodings.empty()) {
      json["supportedEncodings"] = info.supported_encodings;
    }
    if (!info.session_id.empty()) {
      json["sessionId"] = info.session_id;
    }
    return json.dump();
  } catch (...) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}
```

### 二进制编码实现

```cpp
FoxgloveResult<std::vector<uint8_t>> encode_message_data(const MessageData& msg) {
  try {
    std::vector<uint8_t> result;
    result.reserve(1 + 4 + 8 + msg.data.size());

    // Opcode
    result.push_back(0x01);

    // Subscription ID (little-endian)
    result.push_back(static_cast<uint8_t>(msg.subscription_id & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((msg.subscription_id >> 24) & 0xFF));

    // Log time (little-endian, 8 bytes)
    for (size_t i = 0; i < 8; ++i) {
      result.push_back(static_cast<uint8_t>((msg.log_time >> (i * 8)) & 0xFF));
    }

    // Data payload
    result.insert(result.end(), msg.data.begin(), msg.data.end());

    return result;
  } catch (...) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}
```

## 2.5 与官方实现对比

| 方面 | 官方 SDK (Rust) | 我们的实现 |
|------|-----------------|------------|
| JSON 库 | serde_json | nlohmann/json |
| 二进制序列化 | bytes crate | 手动实现 |
| 消息类型 | 完整协议支持 | 核心子集 |
| 生命周期 | Cow (零拷贝) | std::string/vector |
| 错误处理 | Result<T, E> | FoxgloveResult<T> |

### 官方 Rust 实现参考

```rust
// third-party/foxglove-sdk/rust/foxglove/src/protocol/v1/server/message_data.rs
pub struct MessageData {
    pub subscription_id: u32,
    pub log_time: u64,
    pub data: Cow<'a, [u8]>,
}

impl BinaryPayload for MessageData {
    fn write_payload(&self, buf: &mut impl BufMut) {
        buf.put_u32_le(self.subscription_id);
        buf.put_u64_le(self.log_time);
        buf.put_slice(&self.data);
    }
}
```

我们的 C++ 实现保持了相同的二进制格式，但使用标准容器简化内存管理。

## 2.6 测试验证

### 单元测试结构

```cpp
TEST_CASE("Protocol - serverInfo encodes to JSON") {
  ServerInfo info;
  info.name = "Test Server";
  info.capabilities = 3;
  info.supported_encodings = {"json", "protobuf"};

  auto json_str = encode_server_info(info);
  REQUIRE(json_str.has_value());

  auto json = nlohmann::json::parse(json_str.value());
  REQUIRE(json["op"] == "serverInfo");
  REQUIRE(json["name"] == "Test Server");
}

TEST_CASE("Protocol - messageData binary roundtrip") {
  MessageData msg;
  msg.subscription_id = 42;
  msg.log_time = 1234567890ULL;
  msg.data = {0x01, 0x02, 0x03};

  auto encoded = encode_message_data(msg);
  auto decoded = decode_message_data_binary(encoded.value());

  REQUIRE(decoded.has_value());
  REQUIRE(decoded.value().subscription_id == 42);
  REQUIRE(decoded.value().data == msg.data);
}
```

### 运行测试

```bash
$ cmake -B build && cmake --build build && ctest --test-dir build
Test project /home/nypyp/foxglove_ws/src/my_foxglove_sdk/build
    Start 1: test_error
1/2 Test #1: test_error .......................   Passed    0.00 sec
    Start 2: test_protocol
2/2 Test #2: test_protocol ....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2
```

## 2.7 示例程序

```bash
$ ./build/examples/ch02_protocol/ch02_protocol
=== Example 1: ServerInfo JSON Encoding ===
Encoded ServerInfo JSON:
{"capabilities":3,"name":"My Foxglove Server","op":"serverInfo",...}

=== Example 3: MessageData Binary Roundtrip ===
Encoded binary frame:
Hex dump (23 bytes):
01 2a 00 00 00 cb 04 fb 71 1f 01 00 00 7b 22 78
22 3a 20 31 2e 30 7d
```

## 2.8 本章总结

本章实现的核心组件：

| 组件 | 文件 | 作用 |
|------|------|------|
| 协议数据结构 | `include/foxglove/protocol.hpp` | ServerInfo, ChannelAdvertisement, MessageData 等 |
| JSON 编解码 | `src/protocol.cpp` | 使用 nlohmann/json 序列化 |
| 二进制编解码 | `src/protocol.cpp` | MessageData 小端序处理 |
| 单元测试 | `tests/test_protocol.cpp` | 7 个测试用例覆盖编解码和错误处理 |
| 示例程序 | `examples/ch02_protocol/` | 演示 API 用法 |

下一章将基于这些协议基础，实现 Channel 和 Schema 抽象。

---

**练习**

1. 修改 MessageData 编码，添加 CRC32 校验字段
2. 实现 unadvertise 消息的编解码
3. 比较：小端序 vs 大端序在网络协议中的优缺点
4. 思考：为什么 subscribe 需要两个 ID（subscription_id 和 channel_id）？

---

*"理解协议是网络编程的第一步。"*
