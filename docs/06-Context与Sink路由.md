# 第六章：Context 与 Sink 路由

本章实现 `Context` 作为消息路由的中心枢纽，将 `Channel` 与多个消费者（`Sink`）解耦。通过 `Sink` 抽象接口，支持 WebSocket 服务器、MCAP 文件写入器、自定义日志等多种消费端，同时保持 Channel 层的简洁性。

## 核心概念

### 为什么需要路由枢纽

在没有 Context 的架构中，每个 Channel 需要直接管理它的消费者：

```cpp
// 反模式：Channel 直接耦合所有消费者
class Channel {
    WebSocketServer* ws_server_;  // 耦合 WebSocket
    McapWriter* mcap_writer_;     // 耦合 MCAP
    CustomLogger* logger_;        // 耦合日志
    
    void log(const uint8_t* data, size_t len) {
        ws_server_->send(data, len);   // 直接调用
        mcap_writer_->write(data, len); // 直接调用
        logger_->log(data, len);        // 直接调用
    }
};
```

这种设计的弊端显而易见：

1. **紧耦合**：Channel 必须知道所有可能存在的消费者类型
2. **扩展困难**：新增消费者类型需要修改 Channel 代码
3. **生命周期混乱**：Channel 需要管理多个消费者的生命周期

Context 模式通过引入中间层解决这些问题：

```cpp
// Context 模式：Channel 只与 Context 交互
class Channel {
    void log(const uint8_t* data, size_t len) {
        context_->dispatch_message(id_, data, len);  // 单一职责
    }
};
```

### Sink 抽象

`Sink` 是消息消费者的统一接口。任何需要接收消息的对象都实现 `Sink` 接口，向 Context 注册即可：

```cpp
class Sink {
public:
    virtual void on_channel_added(RawChannel& channel) = 0;
    virtual void on_channel_removed(uint32_t channel_id) = 0;
    virtual void on_message(uint32_t channel_id, const uint8_t* data, 
                           size_t len, uint64_t log_time) = 0;
};
```

三个回调分别对应三个生命周期事件：

- **on_channel_added**：新 Channel 创建时通知 Sink，让 Sink 做好准备
- **on_channel_removed**：Channel 销毁时清理资源
- **on_message**：实际的消息投递

## Sink 接口设计

从 `include/foxglove/context.hpp` 中提取的 `Sink` 接口：

```cpp
/// @brief Sink interface for receiving channel and message events.
///
/// A Sink is any consumer of channel data: WebSocket server, MCAP writer,
/// custom logger, etc. The Context routes all channel events to registered sinks.
class Sink {
 public:
  virtual ~Sink() = default;

  /// @brief Called when a channel is added to the context.
  virtual void on_channel_added(RawChannel& channel) = 0;

  /// @brief Called when a channel is removed from the context.
  virtual void on_channel_removed(uint32_t channel_id) = 0;

  /// @brief Called when a message is logged on a channel.
  virtual void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                          uint64_t log_time) = 0;
};
```

**为什么是抽象接口而非模板？**

1. **运行时多态**：Sink 类型在运行时才确定（用户可注册任意 Sink）
2. **类型擦除**：Context 内部用统一的 `std::shared_ptr<Sink>` 存储所有消费者
3. **依赖倒置**：Channel 层不依赖具体 Sink 类型，只依赖抽象接口

## Context 类设计

Context 的核心职责：

1. **Channel 管理**：创建、销毁 Channel，维护 Channel 描述符表
2. **Sink 注册**：添加、移除 Sink，为每个 Sink 分配唯一 ID
3. **消息路由**：将消息分发给所有符合条件的 Sink
4. **线程安全**：保护内部状态，支持并发访问

### 工厂方法与单例

```cpp
// 创建独立的 Context 实例
static FoxgloveResult<Context> create();

// 获取默认全局单例
static Context& default_context();
```

默认单例采用 Meyer's Implementation，线程安全且延迟初始化：

```cpp
Context& Context::default_context() {
  static Context instance = []() {
    auto result = create();
    return std::move(result.value());
  }();
  return instance;
}
```

### Channel 创建与回调绑定

从 `src/context.cpp` 提取的 `create_channel` 实现：

```cpp
FoxgloveResult<RawChannel> Context::create_channel(const std::string& topic,
                                                   const std::string& encoding, Schema schema) {
  auto channel_result = RawChannel::create(topic, encoding, schema, nullptr);
  if (!channel_result.has_value()) {
    return channel_result;
  }

  auto channel = std::move(channel_result.value());
  uint32_t channel_id = channel.id();

  MessageCallback callback = [this, channel_id](uint32_t, const uint8_t* data, size_t len,
                                                uint64_t log_time) {
    this->dispatch_message(channel_id, data, len, log_time);
  };
  channel.set_callback(std::move(callback));

  std::vector<std::shared_ptr<Sink>> notifiable_sinks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_descriptors_.insert_or_assign(channel_id, channel.descriptor());

    for (auto& [sink_id, info] : sinks_) {
      if (!info.filter || info.filter(channel_id)) {
        notifiable_sinks.push_back(info.sink);
      }
    }
  }

  for (auto& sink : notifiable_sinks) {
    sink->on_channel_added(channel);
  }

  return channel;
}
```

**关键点**：

- Channel 的回调是一个 lambda，捕获 `this` 和 `channel_id`
- 当 Channel 调用 `log()` 时，实际调用的是 `Context::dispatch_message()`
- 使用 `insert_or_assign` 而非 `operator[]`，因为 `ChannelDescriptor` 没有默认构造函数

### 消息分发

从 `src/context.cpp` 提取的 `dispatch_message` 实现：

```cpp
void Context::dispatch_message(uint32_t channel_id, const uint8_t* data, size_t len,
                               uint64_t log_time) {
  std::vector<std::shared_ptr<Sink>> active_sinks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [sink_id, info] : sinks_) {
      if (info.filter && !info.filter(channel_id)) {
        continue;
      }
      active_sinks.push_back(info.sink);
    }
  }

  for (auto& sink : active_sinks) {
    sink->on_message(channel_id, data, len, log_time);
  }
}
```

**Channel 过滤机制**：

```cpp
using ChannelFilter = std::function<bool(uint32_t channel_id)>;

// Sink A 接收所有 Channel
context.add_sink(sink_a);

// Sink B 只接收特定 Channel
context.add_sink(sink_b, [](uint32_t id) { return id == specific_channel_id; });
```

## WebSocketServerSink 适配器

`WebSocketServerSink` 是适配器模式的典型应用。它将现有的 `WebSocketServer` 包装为 `Sink` 接口：

```cpp
/// @brief Sink adapter that wraps a WebSocketServer.
class WebSocketServerSink : public Sink {
 public:
  explicit WebSocketServerSink(WebSocketServer& server);

  void on_channel_added(RawChannel& channel) override;
  void on_channel_removed(uint32_t channel_id) override;
  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override;

 private:
  WebSocketServer& server_;
};
```

实现细节（来自 `src/context.cpp`）：

```cpp
void WebSocketServerSink::on_channel_added(RawChannel& channel) {
  server_.add_channel(channel);
}

void WebSocketServerSink::on_channel_removed(uint32_t channel_id) {
  server_.remove_channel(channel_id);
}

void WebSocketServerSink::on_message(uint32_t, const uint8_t*, size_t, uint64_t) {
  // WebSocket 消息通过 Channel 回调直接发送，不需要 Sink 分发
}
```

**注意**：`on_message` 故意为空实现。WebSocketServer 的消息投递走 Channel 回调路径（Channel 创建时注册的回调直接调用 `server_.broadcast_message()`），而非 Sink 分发路径。这种设计保持了与第四章 WebSocket 实现的兼容性。

## 实现细节

### Sink ID 分配

使用 `std::atomic<uint32_t>` 保证线程安全的 ID 生成：

```cpp
static std::atomic<uint32_t> next_sink_id_{1};  // 从 1 开始，0 可表示无效 ID

uint32_t Context::add_sink(std::shared_ptr<Sink> sink, ChannelFilter channel_filter) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t sink_id = next_sink_id_.fetch_add(1, std::memory_order_relaxed);
  sinks_[sink_id] = SinkInfo{std::move(sink), std::move(channel_filter)};
  return sink_id;
}
```

### Move-Only 语义

Context 禁止拷贝，支持移动：

```cpp
Context(Context&& other) noexcept;
Context& operator=(Context&& other) noexcept;
Context(const Context&) = delete;
Context& operator=(const Context&) = delete;
```

这保证了 Context 的唯一所有权语义，避免多个对象引用同一组 Channel 和 Sink。

### 线程安全设计

- 所有公共方法使用 `std::mutex` 保护
- 内部状态（Channel 表、Sink 表）在锁内访问
- Sink 的回调在持有锁时调用，因此 Sink 实现不应调用 Context 方法（避免死锁）

## 与官方 SDK 对比

| 特性 | 我们的实现 | 官方 SDK |
|------|-----------|---------|
| 实现语言 | 纯 C++ | Rust 核心 + C++ FFI 包装 |
| Context 复杂度 | 实现完整路由逻辑 | 仅持有 `shared_ptr<const foxglove_context>` |
| Sink 接口 | C++ 虚函数接口 | Rust trait + C FFI 桥接 |
| 线程安全 | `std::mutex` 显式保护 | Rust 的所有权系统保证 |
| 可调试性 | 单步调试可见路由过程 | 调用进入 Rust 运行时 |
| 教学价值 | 设计模式清晰可见 | 生产优化，隐藏实现细节 |

**设计意图的差异**：

- 官方 SDK 的 Context 是一个薄包装，真正的路由逻辑在 Rust 核心中实现
- 我们的教育版本将完整的路由逻辑用纯 C++ 实现，便于理解设计模式
- 两者在接口层面保持一致（`create()`、`default_context()`、`add_sink()` 等）

## 测试策略

`tests/test_context.cpp` 包含 7 个测试用例：

1. **单 Sink 路由**：验证消息正确路由到单个 Sink
2. **多 Sink 广播**：验证多个 Sink 都能收到同一条消息
3. **Channel 过滤**：验证过滤器能正确筛选 Channel
4. **生命周期管理**：验证添加/移除 Sink 后消息不再投递
5. **默认单例**：验证 `default_context()` 返回同一实例
6. **Channel 验证**：验证创建的 Channel 属性正确
7. **多 Channel 独立路由**：验证不同 Channel 的消息互不干扰

**MockSink 测试模式**：

```cpp
class MockSink : public Sink {
  mutable std::mutex mutex_;
  std::vector<ChannelDescriptor> channels_added_;
  std::vector<MessageCall> messages_received_;

  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_received_.push_back({channel_id, std::vector<uint8_t>(data, data + len), log_time});
  }
};
```

MockSink 记录所有回调调用，测试通过验证记录内容断言正确行为。

## 示例程序

见 `examples/ch06_context/main.cpp`：

```cpp
class PrintSink : public Sink {
  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override {
    printf("[PrintSink] Message on channel %u: %zu bytes\n", channel_id, len);
  }
};

class CounterSink : public Sink {
  std::mutex mutex_;
  int message_count_ = 0;
  
  void on_message(uint32_t, const uint8_t*, size_t, uint64_t) override {
    std::lock_guard<std::mutex> lock(mutex_);
    message_count_++;
  }
};

int main() {
  auto context = Context::create().value();
  
  auto print_sink = std::make_shared<PrintSink>();
  auto counter_sink = std::make_shared<CounterSink>();
  
  context.add_sink(print_sink);
  context.add_sink(counter_sink);
  
  auto channel = context.create_channel("/test", "json", schema).value();
  channel.log(data.data(), data.size(), timestamp);
  
  // counter_sink->message_count_ == 1
}
```

这个例子展示了 Sink 接口的灵活性：`PrintSink` 用于调试输出，`CounterSink` 用于统计计数，两者互不影响。

## 下一步

第七章将实现 **MCAP 文件写入**——这是 Sink 接口的第二个重要实现，它将消息持久化到 MCAP 格式文件中，供 Foxglove Studio 离线回放。
