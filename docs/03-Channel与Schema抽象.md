# 第三章：Channel 与 Schema 抽象

本章实现 Foxglove SDK 的核心抽象——`RawChannel`（通道）和 `Schema`（模式）。Channel 是数据发布的核心概念，代表一个具有主题、编码格式和模式定义的数据流。

## 核心概念

### Schema（模式）

`Schema` 描述消息的结构和格式，包含三个字段：

```cpp
struct Schema {
  std::string name;           // 模式名称，如 "geometry_msgs/Twist"
  std::string encoding;       // 模式编码，如 "jsonschema"、"protobuf"
  std::vector<uint8_t> data;  // 模式数据（原始字节）
};
```

Schema 的作用是告诉消费者如何解析消息。例如：
- `encoding="jsonschema"` 表示 data 包含 JSON Schema 定义
- `encoding="protobuf"` 表示 data 包含 protobuf 的 `.proto` 文件内容

### ChannelDescriptor（通道描述符）

`ChannelDescriptor` 是 Channel 的元数据快照：

```cpp
struct ChannelDescriptor {
  uint32_t id;              // 唯一通道 ID
  std::string topic;        // 主题，如 "/robot/cmd_vel"
  std::string encoding;     // 消息编码，如 "json"、"protobuf"
  Schema schema;            // 关联的模式
};
```

### RawChannel（原始通道）

`RawChannel` 是一个**回调驱动**的消息发布通道：

```cpp
// 创建通道
auto result = RawChannel::create(
    "/sensor/data",                    // topic
    "json",                            // encoding
    Schema{"SensorData", "jsonschema", schema_data},
    callback                           // 消息回调
);

// 发布消息
channel.log(data_ptr, data_len, log_time);
```

## 设计决策：为什么使用回调模型？

Foxglove 官方 SDK 将 Channel 直接与 Context 和 Sink 耦合：

```cpp
// 官方 SDK 风格
auto channel = RawChannel::create(topic, encoding, schema, context);
// channel 内部直接写入 context/sink
```

本实现采用**回调解耦**设计：

```cpp
// 我们的实现
auto channel = RawChannel::create(topic, encoding, schema, callback);
// callback 可以是任何东西：WebSocket 服务器、MCAP 写入器、测试 mock
```

**优点**：
1. **测试友好**：测试时可以注入 mock callback，无需真实网络或文件
2. **灵活组合**：同一个 Channel 可以被路由到多个 Sink，或动态切换 Sink
3. **延迟绑定**：创建 Channel 时不需要 Server 或 Context 已存在

## 关键 API：`set_callback()`

`set_callback()` 允许在 Channel 创建后重新绑定输出目标：

```cpp
// 1. 创建 Channel（可能还没有 Server）
auto channel = RawChannel::create("/topic", "json", schema);

// 2. 稍后 Server 启动，将 Channel 绑定到 Server
server.add_channel(channel);
// 内部调用：channel.set_callback(server_dispatch_fn)

// 3. 或者动态切换到文件记录
mcap_writer.add_channel(channel);
// 内部调用：channel.set_callback(mcap_write_fn)
```

这是 WebSocketServer（第四章）和 Context（第六章）集成的基础。

## 线程安全设计

Channel 实现了完整的线程安全：

| 操作 | 线程安全机制 |
|------|-------------|
| `create()` | `std::atomic<uint32_t>` 分配唯一 ID |
| `log()` | `std::mutex` 保护回调调用和 closed 状态 |
| `close()` | `std::mutex` 保护状态修改 |
| `set_callback()` | `std::mutex` 保护回调替换 |

### 原子 ID 分配

```cpp
static std::atomic<uint32_t> next_channel_id_{1};

uint32_t id = next_channel_id_.fetch_add(1, std::memory_order_relaxed);
```

使用 `fetch_add` 保证多线程创建 Channel 时 ID 唯一且连续。

### 锁策略

```cpp
void RawChannel::log(const uint8_t* data, size_t len, uint64_t log_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || !callback_) return;
  callback_(id_, data, len, log_time);
}
```

**注意**：回调在持有锁的情况下被调用。这意味着：
- 回调本身不应该尝试获取 Channel 的其他锁（避免死锁）
- 回调应该快速执行，否则会阻塞其他线程的 `log()` 调用

## 生命周期

```
创建 (create) → 发布 (log) → 关闭 (close)
     │              │             │
     │              │             └── 之后 log() 成为 no-op
     │              │                  callback 不再被调用
     │              │
     │              └── 线程安全地调用 callback
     │                  将数据传递给绑定的 Sink
     │
     └── 分配唯一 ID
         构建 ChannelDescriptor
         可选：设置初始 callback
```

## 与官方 SDK 的对比

| 特性 | 我们的实现 | 官方 SDK |
|------|-----------|---------|
| ID 分配 | `std::atomic<uint32_t>` | 内部由 Rust 代码分配 |
| 输出方式 | Callback 函数 | 直接写入 Context/Sink |
| 动态绑定 | `set_callback()` 重新绑定 | Channel 固定绑定到 Context |
| 关闭行为 | 标记 closed，后续 log 为 no-op | 向 Server 发送 Unadvertise |
| 拷贝语义 | Move-only | Move-only |
| 线程安全 | Mutex + Atomic | Rust 内部保证 |

## 测试覆盖

本章测试涵盖：

1. **基础功能**：ID 唯一性、callback 调用、数据正确性
2. **生命周期**：创建 → log → close 流程
3. **线程安全**：4 线程并发 log 100 条消息
4. **边界情况**：close 期间 log、callback 设为 nullptr
5. **动态绑定**：`set_callback()` 重新绑定测试

## 下一步

第四章将实现 `WebSocketServer`，它会：
- 调用 `channel.set_callback()` 将 Channel 消息路由到 WebSocket 客户端
- 管理 Channel 的 advertise/unadvertise 生命周期
- 处理客户端的 subscribe/unsubscribe 请求

## 示例代码

见 `examples/ch03_channel/main.cpp`：

```cpp
// 创建 Schema
Schema schema{"SensorData", "jsonschema", schema_data};

// 创建带有回调的 Channel
auto channel = RawChannel::create("/sensor/data", "json", schema, 
    [](uint32_t id, const uint8_t* data, size_t len, uint64_t time) {
        printf("Received %zu bytes\n", len);
    });

// 发布消息
channel.log(msg_data.data(), msg_data.size(), timestamp);

// 重新绑定回调
channel.set_callback(new_callback);

// 关闭通道
channel.close();
```
