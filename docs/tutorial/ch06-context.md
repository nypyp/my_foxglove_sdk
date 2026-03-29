# Chapter 6：Context 与 Sink，把 Channel 变成可路由系统

> **对应 tag**：`v0.6-context`
> **起点**：`v0.5-serialization`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `include/foxglove/context.hpp` — 定义 `Sink` 抽象、`ChannelFilter`、`Context` 路由接口，以及 `WebSocketServerSink` 适配器
> - `src/context.cpp` — 实现 sink 注册、channel 创建、消息分发、默认单例与 WebSocket 适配层
> - `tests/test_context.cpp` — 验证单 sink、多 sink、channel filter、sink 生命周期和默认单例行为
>
> **深入阅读**：[06-Context与Sink路由.md](../06-Context与Sink路由.md)
> **预计阅读时间**：55 分钟
> **前置要求**：完成第 5 章，理解 `RawChannel::log()` 的回调模型，以及 `WebSocketServer::add_channel()` 已经能把 Channel 接到 WebSocket 广播链路上

---

## 6.0 本章地图

前五章已经能建立协议、创建 Channel、启动 WebSocket 服务器，也能把结构化消息编码成字节。但系统还缺少一个真正的“中枢”：同一条消息该怎么同时送到 WebSocket、文件写入器和别的消费者？本章实现 `Context` 与 `Sink` 路由层。读完后，你能把多个 Channel 扇出到多个消费者，并理解为什么这比“Channel 直接持有多个下游对象指针”更稳。

```text
RawChannel::log()
       |
       v
+-------------------+
| Context           |
| - channel table   |
| - sink registry   |
| - channel filter  |
+-------------------+
   |          |           \
   v          v            v
Sink A     Sink B       Sink C
(ws)       (mcap)       (logger)
```

这一章的关键问题是：**当一个 SDK 同时面向多个消费端时，谁负责路由，谁负责持有关系，谁负责线程边界？**

---

## 6.1 从需求出发

### 先看反模式，为什么不能让 Channel 直接依赖所有消费者

如果没有 `Context`，最直接的想法往往是给 `Channel` 塞几个下游指针：

```cpp
class Channel {
 public:
  void log(const uint8_t* data, size_t len, uint64_t ts) {
    if (ws_server_) {
      ws_server_->send(id_, data, len, ts);
    }
    if (mcap_writer_) {
      mcap_writer_->write(id_, data, len, ts);
    }
    if (logger_) {
      logger_->append(id_, data, len, ts);
    }
  }

 private:
  WebSocketServer* ws_server_ = nullptr;
  McapWriter* mcap_writer_ = nullptr;
  Logger* logger_ = nullptr;
};
```

这段代码看起来省事，其实把三类问题都埋进去了。

第一，**耦合爆炸**。`Channel` 本来只该表达“某个 topic 上的一串消息”，现在却知道 `WebSocketServer`、`McapWriter`、`Logger` 这些完全不同的消费端。以后新增 `PrometheusSink`、`KafkaSink`，又得继续改 `Channel`。

第二，**生命周期难管**。谁保证这些裸指针在 `Channel::log()` 时还活着？谁来决定是先关 server 还是先销毁 channel？如果每个 `Channel` 都自己记一套下游关系，整个系统会很快失去边界。

第三，**职责混乱**。`Channel` 的职责应该是保存自己的描述符、接收字节 payload、触发回调。它不该负责“哪些消费者应该收到这条消息”。一旦路由逻辑进来，`Channel` 就不再是单纯的数据入口，而成了一个小型调度器。

### Context 要解决的是什么问题

`Context` 的作用，就是把这些关系收拢到一个中间层：

1. 统一创建和管理 `RawChannel`
2. 保存已注册的所有 `Sink`
3. 把 `Channel::log()` 产生的消息分发给多个消费者
4. 用 `ChannelFilter` 控制“哪些 sink 关心哪些 channel”

于是 `Channel` 不再面对一堆具体下游，只面对一个回调入口。真正的消息路线会变成：

```text
业务代码 -> RawChannel::log() -> Context::dispatch_message()
                               -> Sink 1
                               -> Sink 2
                               -> Sink 3
```

这也是本章的设计核心：**把“数据生产”与“数据消费”解耦，再由一个路由中枢把两边连起来。**

### 为什么这一步要放在 Ch06

到了第 5 章，我们已经有了三块可复用拼图：

- `RawChannel` 负责收消息
- `WebSocketServer` 负责网络广播
- 序列化层负责把结构体变成字节

但这些模块还是并列存在，缺少一个稳定的装配点。`Context` 正是在这里出现的，它让“实时推送”和“离线录制”第一次可以放进同一个体系里。后面的 `McapWriterSink`、Ch09 的端到端示例，都会建立在这里定义的接口上。

---

## 6.2 设计接口（先写头文件）

这章的头文件是 `include/foxglove/context.hpp`。它不只是声明一个类，而是一次性给出三层抽象：`Sink`、`ChannelFilter` 和 `Context`。

### `Sink` 是统一消费端接口

先看源码里的核心定义：

```cpp
class Sink {
 public:
  virtual ~Sink() = default;

  virtual void on_channel_added(RawChannel& channel) = 0;
  virtual void on_channel_removed(uint32_t channel_id) = 0;
  virtual void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                          uint64_t log_time) = 0;
};
```

这个接口很小，但信息量很大。

- `on_channel_added()` 让 sink 在 channel 创建时做准备工作，比如注册到服务器、建立 schema 映射、初始化内部表
- `on_channel_removed()` 让 sink 在 channel 消失时清理状态
- `on_message()` 才是正常消息分发路径

也就是说，`Sink` 不只接收 payload，它还接收 channel 生命周期事件。这一点很关键，因为像 `WebSocketServer` 这样的下游，不是“等消息来了再猜这个 channel 长什么样”，而是必须先知道 topic、encoding、schema，才能向客户端 advertise。

### `ChannelFilter` 控制每个 sink 的接收范围

头文件紧接着定义：

```cpp
using ChannelFilter = std::function<bool(uint32_t channel_id)>;
```

这表示每个 sink 在注册时都可以附带一个过滤器。过滤器输入 `channel_id`，返回一个布尔值：

- `true`，这个 sink 接收该 channel 的事件和消息
- `false`，这个 sink 忽略该 channel

它的好处是把“谁订阅谁”从 `Sink` 的具体实现里抽出来了。`Sink` 本身只处理收到的事件，不需要自己再判断“我是不是应该接这个 channel”。这使 `Context` 能在路由时统一裁决。

### `Context` 公开了哪些核心接口

下面这段公开 API 是整章的主角：

```cpp
class Context final {
 public:
  static FoxgloveResult<Context> create();
  static Context& default_context();

  uint32_t add_sink(std::shared_ptr<Sink> sink, ChannelFilter channel_filter = nullptr);
  void remove_sink(uint32_t sink_id);

  FoxgloveResult<RawChannel> create_channel(const std::string& topic,
                                            const std::string& encoding,
                                            Schema schema);
  void remove_channel(uint32_t channel_id);

 private:
  struct SinkInfo {
    std::shared_ptr<Sink> sink;
    ChannelFilter filter;
  };

  void dispatch_message(uint32_t channel_id, const uint8_t* data, size_t len,
                        uint64_t log_time);
};
```

这里有四个设计点要先吃透。

**一，`Context::create()` 返回 `FoxgloveResult<Context>`。** 这和前几章风格一致。即使当前实现只是返回一个空表，接口层也保留了未来失败的空间，比如初始化共享资源失败。

**二，`default_context()` 提供全局单例。** 这让简单用法不必到处手动传 `Context`，但同时保留了 `create()`，给测试和复杂场景独立实例的能力。

**三，`add_sink()` 返回 `sink_id`。** 这说明 sink 的生命周期不是“注册后永久存在”，而是可以动态添加、动态移除。

**四，`create_channel()` 在 `Context` 上创建 `RawChannel`。** 这不是语法层面的绕路，而是为了保证每个 channel 一出生就被挂到正确的路由体系里。

### 为什么这里选运行时多态，而不是模板

看完接口后，很多人会自然冒出一个问题：`Sink` 为什么不用模板？例如做成 `Context<TSink...>`，不是能避免虚函数吗？

这章故意不用模板，有三个非常实际的原因。

第一，**sink 的种类是在运行时决定的**。教程后面会同时出现 `WebSocketServerSink`、`McapWriterSink`，用户也可能自己写一个 `ConsoleSink`。如果换成模板，`Context` 的类型会随着 sink 组合变化，调用方几乎没法把它当成一个稳定对象传来传去。

第二，**我们需要一个统一容器来保存不同类型的下游对象**。`std::shared_ptr<Sink>` 正好承担了 type erasure 的角色，把具体类型藏在抽象接口后面，让 `Context` 可以用一个 `unordered_map<uint32_t, SinkInfo>` 保存全部 sink。

第三，**模板并不能消除真正的复杂度**。就算你让 `dispatch_message()` 在编译期展开，也仍然要回答“怎么动态增删 sink”“怎么做 filter”“怎么统一管理生命周期”这些系统问题。这里的关键矛盾不是函数调用那一点开销，而是系统边界。

> 💡 **⚠️ 常见陷阱 工程旁白：观察者模式的死锁风险——回调持锁的反模式**
>
> 观察者模式最容易出事故的地方，不是通知丢了，而是“通知时还拿着锁”。如果 `Context` 在持有 `mutex_` 的情况下直接调用 `sink->on_message()`，而某个 sink 内部又反过来调用 `Context::remove_sink()`、`remove_channel()`，或者触发另一个也要拿同一把锁的路径，死锁就来了。更糟的是，这种死锁往往只会在某个特定组合条件下出现，平时很难重现。
>
> 正确做法通常是两段式：先在锁内收集一份稳定快照，例如“当前需要通知哪些 sink”，然后释放锁，再逐个调用回调。本章 `create_channel()` 和 `dispatch_message()` 都是这个策略。这样做的代价只是多一次容器拷贝，但换来的是更清晰的锁边界和更低的回调耦合风险。对于路由中枢这种组件，这是非常划算的工程取舍。
>
> 这里还要补一句现实约束：当前仓库里的 `add_sink()` 只是把 sink 注册进表，并不会自动把“已有 channel”补发给这个新 sink。所以“先取快照、后回调”的安全策略，适用于 `create_channel()` 和 `dispatch_message()` 这两条通知路径，不等于“任何时候新增 sink 都会自动补齐历史状态”。理解这一点，才能正确把握当前实现的生命周期语义。

---

## 6.3 实现核心逻辑

头文件定好以后，`src/context.cpp` 做了三件真正关键的事：给 sink 分配 ID，把 channel 的回调接到 `Context`，然后在分发时应用 filter。

### sink 注册，先解决“谁在场”

文件开头先给出一个静态原子计数器：

```cpp
std::atomic<uint32_t> Context::next_sink_id_{1};
```

然后 `add_sink()` 用它生成唯一 ID：

```cpp
uint32_t Context::add_sink(std::shared_ptr<Sink> sink, ChannelFilter channel_filter) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint32_t sink_id = next_sink_id_.fetch_add(1, std::memory_order_relaxed);
  sinks_[sink_id] = SinkInfo{std::move(sink), std::move(channel_filter)};

  return sink_id;
}
```

这里选择 `memory_order_relaxed` 是合理的，因为我们只需要“生成不重复 ID”，不需要靠这个原子变量建立复杂的跨线程 happens-before 关系。真正保护 `sinks_` 容器一致性的，是 `mutex_`。

同时要注意，当前实现里的 `add_sink()` 语义是**只注册，不回放历史 channel 事件**。也就是说，如果你在已经存在多个 channel 之后再调用 `add_sink()`，这个新 sink 不会自动收到这些旧 channel 的 `on_channel_added()`；但只要这些旧 channel 之后继续产生日志消息，它仍然会参与后续的 `dispatch_message()` 路由。这不是 bug，而是当前实现有意保留的最小语义边界：**不补发生命周期事件，但参与未来消息分发。**

### `create_channel()` 把 Channel 接到路由枢纽上

这一段是全章最重要的实现：

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
```

这段代码等于说：`RawChannel` 自己仍然保留“消息来了就调用 callback”的模型，但 callback 的落点不再是某个固定 server，而是 `Context::dispatch_message()`。

也就是说，从这一章开始，Channel 层并不知道自己后面挂的是 WebSocket、MCAP 还是别的 sink。它只知道，“我有消息了，交给 Context”。这就是解耦真正发生的地方。

### 新 channel 建好后，为什么还要通知 sink

`create_channel()` 后半段紧接着做了另一件事：把满足 filter 的 sink 拿出来，然后调用 `on_channel_added()`。

```cpp
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
```

这里有两个工程细节值得注意。

第一，`channel_descriptors_` 保存的是 descriptor，而不是整个 `RawChannel` 对象。这让 `Context` 能记住 channel 元信息，而不用承担 channel 全部所有权。

第二，filter 不只是用于消息分发，也用于“是否通知这个 sink 有新 channel”。这很合理，因为如果某个 sink 根本不关心这个 channel，那它连 `advertise`、schema 建表这些准备动作都没必要做。

### `dispatch_message()` 体现了整套路由策略

消息真正到来时，`dispatch_message()` 做的事情反而很朴素：

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

它先筛一遍 sink，再逐个调用 `on_message()`。这意味着 channel filter 的行为非常直接：**filter 只按 `channel_id` 做路由裁决，不改消息内容，也不关心 payload。**

举个具体例子：

```cpp
auto all_sink = std::make_shared<MySink>();
context.add_sink(all_sink);

auto filtered_sink = std::make_shared<MySink>();
context.add_sink(filtered_sink, [important_id](uint32_t channel_id) {
  return channel_id == important_id;
});
```

这时：

- `all_sink` 会收到所有 channel 的 `on_channel_added()` 和 `on_message()`
- `filtered_sink` 只会收到 `important_id` 对应 channel 的事件和消息

这种设计足够简单，也足够通用。后面无论是“只录某些 topic 到 MCAP”，还是“只让某些通道广播到特定服务端”，都可以沿着这个接口走。

### `WebSocketServerSink` 是适配器，不是第二套传输栈

头文件里还定义了一个适配器：

```cpp
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

实现非常短：

```cpp
WebSocketServerSink::WebSocketServerSink(WebSocketServer& server) : server_(server) {}

void WebSocketServerSink::on_channel_added(RawChannel& channel) {
  server_.add_channel(channel);
}

void WebSocketServerSink::on_channel_removed(uint32_t channel_id) {
  server_.remove_channel(channel_id);
}

void WebSocketServerSink::on_message(uint32_t /*channel_id*/, const uint8_t* /*data*/,
                                     size_t /*len*/, uint64_t /*log_time*/) {}
```

很多读者第一次看到这里会愣住，`on_message()` 怎么能是空的？答案是：**因为 WebSocket 的实际消息发送路径已经在第四章建立好了，这里不该重复做一遍。**

`WebSocketServer::add_channel(channel)` 会在 server 内部把该 channel 接到自己的广播逻辑上。换句话说，`WebSocketServerSink` 的职责只是把新 channel 和被移除的 channel 同步给 server，让 server 完成订阅、advertise 和发送链路的接线。

因此 `WebSocketServerSink::on_message()` 为空实现，不是“漏写了功能”，而是有意避免双重发送。如果它在这里再次调用某种 `server_.broadcast_message()`，反而会和已有的 Channel 回调路径重叠，造成逻辑重复甚至重复发包。

### 默认单例实现保持了简单入口

最后补一段 `default_context()`：

```cpp
Context& Context::default_context() {
  static Context instance = []() {
    auto result = create();
    return std::move(result.value());
  }();
  return instance;
}
```

这段 Meyers singleton 写法让默认上下文具备两个特征：首次调用才初始化，以及多次调用返回同一个实例。测试里也会专门验证这一点。

> 💡 **🧰 C++ 技巧 工程旁白：type erasure 与 `std::function` 的开销**
>
> 本章有两处典型的 type erasure。第一处是 `std::shared_ptr<Sink>`，它把不同具体 sink 统一擦成一个抽象基类指针。第二处是 `ChannelFilter`，它把任意可调用对象统一擦成 `std::function<bool(uint32_t)>`。这两招都极大提升了接口灵活性，但不是零成本的魔法。
>
> `std::function` 往往会带来一次间接调用，某些较大的 lambda 还可能触发堆分配。虚函数也同样需要一次动态派发。所以在极端高频路径里，这些抽象确实有成本。不过这里的工程判断是，路由层首先要解决的是“系统能否动态装配、能否保持边界清晰”，而不是把一次回调开销抠到极致。等真的出现性能瓶颈时，再用 profiling 证明问题，而不是在设计阶段为了假想开销把 API 复杂化。

---

## 6.4 测试：验证正确性

这一章最适合用单元测试。因为我们要验证的不是网络连通性，也不是磁盘格式，而是 `Context` 的路由行为本身：消息有没有发给对的人，过滤器有没有生效，移除 sink 后是不是就真的收不到了。

### `MockSink` 是测试主角

`tests/test_context.cpp` 先定义了一个 `MockSink`：

```cpp
class MockSink : public Sink {
 public:
  struct MessageCall {
    uint32_t channel_id;
    std::vector<uint8_t> data;
    uint64_t log_time;
  };

  mutable std::mutex mutex_;
  std::vector<ChannelDescriptor> channels_added_;
  std::vector<uint32_t> channels_removed_;
  std::vector<MessageCall> messages_received_;

  void on_channel_added(RawChannel& channel) override {
    std::lock_guard<std::mutex> lock(mutex_);
    channels_added_.push_back(channel.descriptor());
  }

  void on_message(uint32_t channel_id, const uint8_t* data, size_t len,
                  uint64_t log_time) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_received_.push_back({channel_id, std::vector<uint8_t>(data, data + len), log_time});
  }
};
```

这个 mock 做得很克制，没有伪造复杂行为，只是把收到的事件记下来，方便断言。这正符合本章需要锁定的目标：**验证路由结果，而不是测试某个复杂下游自己的内部实现。**

### 测试一，多 sink 广播

下面这个测试直接验证“同一条消息能否扇出到多个消费者”：

```cpp
TEST_CASE("Context - routes to multiple sinks") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink_a = std::make_shared<MockSink>();
  auto sink_b = std::make_shared<MockSink>();

  context.add_sink(sink_a);
  context.add_sink(sink_b);

  auto channel_result =
      context.create_channel("/test/topic", "json", Schema{"TestSchema", "jsonschema", {}});
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());

  std::vector<uint8_t> test_data = {0xAA, 0xBB, 0xCC};
  channel.log(test_data.data(), test_data.size(), 9876543210ULL);

  REQUIRE(sink_a->message_count() == 1);
  REQUIRE(sink_b->message_count() == 1);
}
```

这个测试说明 `Context` 并不是“把消息交给某一个当前 sink”，而是真的做广播。它保护的是整个设计最核心的承诺：一个 channel 产生的消息，可以并行供多个消费者使用。

### 测试二，channel filter

第二个关键测试验证 filter 行为：

```cpp
TEST_CASE("Context - channel filter") {
  auto ctx_result = Context::create();
  REQUIRE(ctx_result.has_value());
  auto context = std::move(ctx_result.value());

  auto sink_a = std::make_shared<MockSink>();
  auto sink_b = std::make_shared<MockSink>();

  auto result1 = context.create_channel("/topic1", "json", Schema{"Schema1", "jsonschema", {}});
  REQUIRE(result1.has_value());
  auto channel1 = std::move(result1.value());

  auto result2 = context.create_channel("/topic2", "json", Schema{"Schema2", "jsonschema", {}});
  REQUIRE(result2.has_value());
  auto channel2 = std::move(result2.value());

  uint32_t ch1_id = channel1.id();

  context.add_sink(sink_a);
  context.add_sink(sink_b, [ch1_id](uint32_t channel_id) { return channel_id != ch1_id; });

  std::vector<uint8_t> data1 = {0x01};
  channel1.log(data1.data(), data1.size(), 1ULL);
  REQUIRE(sink_a->message_count() == 1);
  REQUIRE(sink_b->message_count() == 0);

  std::vector<uint8_t> data2 = {0x02};
  channel2.log(data2.data(), data2.size(), 2ULL);
  REQUIRE(sink_a->message_count() == 2);
  REQUIRE(sink_b->message_count() == 1);
}
```

这个测试把 filter 的含义讲得非常清楚：`sink_b` 不是“永远不收消息”，而是“只跳过特定 channel”。也因此，channel filter 可以被理解成一个轻量路由规则，而不是订阅系统里那种复杂表达式语言。

### 其余测试锁定了什么风险

除了上面两项，测试文件还覆盖了这些行为：

- 单 sink 情况下，消息、log_time 和 channel descriptor 都能正确到达
- `remove_sink()` 之后，已移除的 sink 不再收到后续消息
- `default_context()` 多次调用返回同一实例
- 多个 channel 的消息不会串台，`channel_id` 会保持独立

这组测试组合起来，刚好构成 `Context` 的最小契约面。只要它们都通过，我们就知道这个路由中枢具备了后续章节要依赖的稳定行为。

---

## 6.5 与官方实现对比

这一节不是要证明教学版“更好”，而是帮助你建立一个判断框架：**教学版到底简化了什么，保留了什么，为什么这些取舍适合教程。**

### 相同点，核心架构思想是一致的

无论是官方 SDK 还是这一版教学实现，都在追求同一个目标：让消息生产者和多个消费者解耦，中间由一个统一的路由层来衔接。这意味着下面几条原则是共通的：

- Channel 不应该直接持有一堆具体消费端
- 路由层需要知道 channel 生命周期，不只是消息 payload
- 多消费者场景必须支持 fan-out，而不是单播
- 过滤规则应该由中枢统一决定，而不是散落在各个下游里

换句话说，本章不是“为了教学才发明一套假架构”，而是在保留真实工程骨架的前提下，做了有意识的减法。

### 不同点一，教学版更强调可读性和可验证性

官方实现通常会面对更复杂的兼容性、性能和平台约束，接口层可能更细分，内部状态机也可能更重。而这一章的实现刻意压缩到几个最关键的 API：`add_sink()`、`create_channel()`、`dispatch_message()`。

这样做的好处是，读者能在一个文件里看清整条路径：

1. `create_channel()` 里绑定 callback
2. `dispatch_message()` 里筛选 sink
3. 测试里验证广播和 filter

这条线一旦理顺，后续你再看更复杂的实现，就不会被细节淹没。

### 不同点二，教学版保留了 `WebSocketServer` 的既有发送路径

这一版里 `WebSocketServerSink::on_message()` 故意为空，就是一个很典型的教学取舍。官方实现里，某些系统可能会把消息分发、订阅管理和网络输出收得更紧。但在本教程里，我们保留了第四章已经建立好的 `WebSocketServer::add_channel()` 路径，让 Ch06 专注在“如何把 server 装进统一 sink 模型”这件事上。

这个取舍的价值在于，读者不会同时面对“两套新机制一起改”。你能明确看到：

- 第四章解决的是 WebSocket 自身怎么发送
- 第六章解决的是 WebSocket 作为一个消费者，怎么被挂进路由体系

这就是教学设计里很重要的一点，**每一章尽量只引入一种新的复杂度。**

### 不同点三，filter 设计保持最小可用

这里的 `ChannelFilter` 只接收 `channel_id`。真实世界里你也许会希望按 topic 名、schema、encoding，甚至按 payload 内容做过滤。但教程没有一开始就把它扩展成复杂 DSL，因为那会把重点从“路由边界”拖到“规则系统设计”。

对这一章来说，`channel_id -> bool` 已经足够证明架构成立。更复杂的过滤规则，完全可以建立在这个最小模型之上演化。

---

## 6.6 打 tag，验证完成

以下命令是本章的标准完成流程。和前几章一样，**`ctest` 全部通过才是唯一硬性完成标准**。

```bash
# 1. 构建并运行测试
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag
git add .
git commit -m "feat(ch06): add context and sink routing"
git tag my-v0.6-context

# 3. 与参考实现对比
git diff v0.6-context
```

如果你在自己的分支上完成了本章，预期看到的结果应该是：

- `tests/test_context.cpp` 全绿
- `Context` 能正确广播到多个 sink
- `ChannelFilter` 能筛掉不关心的 channel
- `default_context()` 的单例行为符合预期

`git diff v0.6-context` 只用于辅助理解。哪怕你的实现和参考版本不完全一样，只要 `ctest` 通过，就说明行为契约已经成立。

---

## 本章小结

- **本章掌握了**：
  - 为什么 `Context` 是 Channel 与多个消费者之间的路由中枢
  - `Sink` 如何统一表达 WebSocket、文件写入器和自定义下游
  - `ChannelFilter` 怎样按 `channel_id` 做最小可用的路由裁决
  - `create_channel()` 如何把 `RawChannel` 的回调接到 `dispatch_message()`
  - 为什么 `WebSocketServerSink::on_message()` 可以是空实现，而且这恰好避免了重复发送
- **工程知识点**：
  - 观察者模式的死锁风险——回调持锁的反模式
  - type erasure 与 `std::function` 的开销
- **延伸练习**：
  - 自己实现一个 `ConsoleSink`，只打印特定 topic 的 channel ID 和消息长度，体会 filter 与 sink 实现的分工
  - 尝试把 `ChannelFilter` 从按 `channel_id` 过滤，扩展到按 `ChannelDescriptor` 过滤，比较接口复杂度的变化
  - 思考 `remove_channel()` 是否也该受 filter 影响，再结合当前实现讨论哪种语义更合理
- **参考文档**：[06-Context与Sink路由.md](../06-Context与Sink路由.md)
