# 第 4 章：WebSocket 服务器——把 RawChannel 接到 Foxglove 协议上

> **对应 tag**：`v0.4-server`
> **起点**：`v0.3-channel`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `include/foxglove/server.hpp` — `WebSocketServer` 的公开接口：`create()`、`add_channel()`、`remove_channel()`、`broadcast_time()`、`shutdown()`
> - `src/server.cpp` — PIMPL 实现：libwebsockets 事件循环、订阅管理、消息排队与分发
> - `tests/test_server.cpp` — 集成测试：连接后收到 `serverInfo`、订阅后收到 `messageData`
>
> **深入阅读**：[04-WebSocket服务器.md](../04-WebSocket服务器.md)
> **预计阅读时间**：65 分钟
> **前置要求**：完成第 3 章，理解 `RawChannel`、`set_callback()`、`MessageData` 二进制编码；了解 WebSocket 协议基础

---

## 4.0 本章地图

前三章给了我们错误处理、协议编解码、以及可以发布消息的 `RawChannel`。但如果你现在启动 Foxglove Studio 并尝试连接，什么也看不到——因为还没有任何东西在监听 WebSocket 连接。

本章实现 `WebSocketServer`，它是连接 `RawChannel` 与真实客户端的桥梁：

- 管理 WebSocket 客户端连接（握手、断开）
- 在新客户端连接时发送 `serverInfo` 和所有已注册 Channel 的 `advertise`
- 处理客户端发来的 `subscribe` / `unsubscribe` 请求
- 将 `RawChannel::log()` 发出的消息路由到订阅了对应 Channel 的客户端，编码为 `messageData` 二进制帧

完成本章后，你能启动服务器、连接 Foxglove Studio，看到 `serverInfo` → `advertise` → `messageData` 的完整链路。

```text
Ch03: RawChannel              Ch04: WebSocketServer
+---------------------+       +--------------------------------+
| RawChannel          |  -->  | WebSocketServer                |
|   log(data, time)   |       |   add_channel(ch)              |
|   set_callback(fn)  |       |   dispatch_message()           |
|   id() / descriptor |       |   pending_writes queue         |
+---------------------+       |   lws_service 事件循环          |
                               +--------------------------------+
                                         |
                               Foxglove Studio (WebSocket client)
```

---

## 4.1 从需求出发

### 缺失的最后一公里

第 3 章结束时，`RawChannel` 已经能接受数据并通过回调转发给任意消费者。但「消费者」是谁？在测试里是一个 lambda；在真实场景里，它应该是一个 WebSocket 服务器，把数据推送给 Foxglove Studio。

如果没有服务器，数据的流向是这样的：

```
业务代码 --> RawChannel::log() --> 回调 --> ??? (没有人接收)
```

有了服务器之后：

```
业务代码 --> RawChannel::log() --> 回调 --> WebSocketServer::dispatch_message()
                                                 |
                                        找到订阅了此 Channel 的客户端
                                                 |
                                        构造 messageData 二进制帧
                                                 |
                                        加入该客户端的 pending_writes 队列
                                                 |
                                        lws_service 循环触发写入
```

### 服务器需要解决哪些问题？

1. **连接管理**：哪些 WebSocket 连接是活跃的？每个连接的状态（订阅关系）如何存储？
2. **Channel 注册**：服务器需要知道哪些 Channel 存在，才能在新客户端连接时发送 `advertise`。
3. **订阅管理**：客户端发来 `{"op":"subscribe", ...}` 时，需要记录 subscription_id → channel_id 的映射。
4. **消息排队**：`RawChannel::log()` 可能从任意线程调用，但 libwebsockets 的写操作必须在 lws 事件循环线程中完成——需要一个线程安全的 pending queue。
5. **事件循环**：libwebsockets 使用 `lws_service()` 轮询所有 fd，必须在独立线程中持续运行。

---

## 4.2 设计接口（先写头文件）

打开 `include/foxglove/server.hpp`，公开接口分为三部分：配置结构体、回调结构体、服务器类本身。

### 配置与回调

```cpp
// include/foxglove/server.hpp

/// @brief Callback for channel subscription events.
using SubscribeCallback = std::function<void(uint32_t channel_id, uint32_t subscription_id)>;

/// @brief Callback for channel unsubscription events.
using UnsubscribeCallback = std::function<void(uint32_t subscription_id)>;

/// @brief WebSocket server callbacks.
struct WebSocketServerCallbacks {
  SubscribeCallback on_subscribe;
  UnsubscribeCallback on_unsubscribe;
};

/// @brief WebSocket server configuration options.
struct WebSocketServerOptions {
  std::string host = "0.0.0.0";
  uint16_t port = 8765;
  std::string name;
  uint32_t capabilities = 0;
  WebSocketServerCallbacks callbacks;
};
```

`WebSocketServerOptions` 把所有启动参数集中在一个结构体里。好处是：未来新增参数（比如 TLS 证书路径）不需要改函数签名，只需给结构体加字段。`callbacks` 是可选的——不传就不会触发任何通知。

这里还有一个教学上的简化值得提前说明：本教程把 `capabilities` 保留为 `uint32_t` 数值字段，方便你在第 2 章的协议层和本章的 server 实现里直接传递与断言。更贴近真实 Foxglove 协议的实现会把能力集合建模得更语义化（例如显式能力名集合），而不是单个整数。教程此处优先选择“代码路径清晰”，不是在宣称这是唯一或最终的协议表示方式。

### 服务器类

```cpp
class WebSocketServer final {
 public:
  static FoxgloveResult<WebSocketServer> create(WebSocketServerOptions options);

  void add_channel(RawChannel& channel);
  void remove_channel(uint32_t channel_id);
  void broadcast_time(uint64_t timestamp);
  void shutdown();

  // Move-only 语义
  WebSocketServer(WebSocketServer&& other) noexcept;
  WebSocketServer& operator=(WebSocketServer&& other) noexcept;
  WebSocketServer(const WebSocketServer&) = delete;
  WebSocketServer& operator=(const WebSocketServer&) = delete;

  ~WebSocketServer();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  explicit WebSocketServer(std::unique_ptr<Impl> impl);
};
```

几个值得关注的设计决策：

**PIMPL 模式**：`server.hpp` 中没有任何 libwebsockets 类型。这意味着包含 `server.hpp` 的业务代码不需要传递 `-I/path/to/libwebsockets` 编译参数，也不会因为 lws 的内部类型变化而触发大范围重编译。libwebsockets 是实现细节，不应该泄露到公开头文件里。

**为什么 `add_channel()` 是显式调用？**

这是一个刻意的设计选择，值得展开解释。

最直观的想法是：`RawChannel` 创建时自动注册到某个全局服务器。但这会引入隐式耦合——Channel 的生命周期与服务器绑死，无法在没有服务器的场景（比如纯文件录制）中独立使用 Channel。

显式的 `add_channel(channel)` 调用带来以下好处：
- **一个 Channel，多个 Sink**：同一个 Channel 可以先注册到 `WebSocketServer`，后续再注册到 MCAP 写入器，`set_callback()` 支持重绑定。
- **生命周期透明**：调用者明确控制 Channel 何时对外可见，何时从广播中移除。
- **可测试性**：测试代码可以创建 Channel 而不启动服务器，不会有任何副作用。
- **多服务器场景**：理论上同一 Channel 的消息可以路由到两个不同的 server（通过回调链实现），显式注册不做任何假设。

`add_channel()` 内部做两件事：给 Channel 设置回调（`set_callback()`），并向所有当前连接的客户端发送 `advertise`。这两件事在同一次调用中原子完成（有 mutex 保护）。

> 💡 **🔍 对比视角 工程旁白：libwebsockets 事件循环模型 vs asio**
>
> 如果你用过 Boost.Asio 或 standalone asio，你习惯的是 `io_context::run()` + `async_read` / `async_write` + completion handler 的组合。asio 的模型是「你描述你想做什么，I/O 完成时回调」，代码结构清晰，组合性强。
>
> libwebsockets 的模型截然不同：`lws_service(context, timeout_ms)` 是一个阻塞调用，它内部 `poll()`/`epoll()` 所有文件描述符，然后触发一系列回调（`LWS_CALLBACK_RECEIVE`、`LWS_CALLBACK_SERVER_WRITEABLE` 等）。你没有办法「主动」向 lws 写数据——你只能调用 `lws_callback_on_writable(wsi)` 请求下一次可写回调，然后在回调里执行实际写入。
>
> 这个模型的优点是：lws 内部处理了 WebSocket 分帧、掩码、压缩（permessage-deflate）等复杂协议细节，你只需要关心应用层逻辑。缺点是：所有写操作必须在 lws 线程中执行，来自其他线程的消息必须先放入 pending queue，再通过 `lws_cancel_service()` 唤醒 lws 线程处理。这正是本章 `ClientSession::pending_writes` 存在的原因。
>
> 如果对性能要求更高，可以考虑用 uWebSockets（基于 µSockets，同样是事件循环模型但更轻量）或基于 asio 的 Boost.Beast（纯头文件，组合性更好）。本项目选择 lws 的主要原因是：它对 WebSocket 子协议协商的原生支持，以及 `foxglove.websocket.v1` 这种子协议字符串校验可以自然融入 callback 流程。

---

## 4.3 实现核心逻辑

### 启动：创建 lws context + service 线程

`src/server.cpp` 的 `Impl::start()` 是整个服务器真正“活起来”的地方：

```cpp
FoxgloveResult<void> start() {
  struct lws_context_creation_info info{};
  info.port = options_.port;
  info.protocols = protocols_;
  info.user = this;
  info.gid = -1;
  info.uid = -1;

  context_ = lws_create_context(&info);
  if (!context_) {
    return tl::make_unexpected(FoxgloveError::ServerStartFailed);
  }

  service_thread_ = std::thread([this]() {
    while (!interrupted_) {
      lws_service(context_, 50);
    }
  });

  return {};
}
```

这段实现定义了本章的线程模型：

- **主线程 / 业务线程**：创建 server、add_channel、调用 `channel.log()`
- **service thread**：持续执行 `lws_service()`，处理连接、接收、可写事件

`lws_context_creation_info` 里的 `info.user = this` 很重要，它把 `Impl*` 挂到了 lws context 上，后续静态 callback 才能反查到当前 server 实例。

### 静态 protocol 数组与子协议

libwebsockets 需要一个静态 C 数组，而不是 `std::vector`：

```cpp
static constexpr lws_protocols protocols_[] = {
    {"foxglove.websocket.v1", callback, 0, 4096, 0, nullptr, 0},
    {nullptr, nullptr, 0, 0, 0, nullptr, 0}
};
```

这里的协议名必须与客户端请求的子协议完全匹配，否则 Foxglove Studio 根本不会进入正确的协议路径。

### `add_channel()`：把 RawChannel 真正接上网络层

```cpp
void add_channel(RawChannel& channel) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint32_t channel_id = channel.id();
  channels_[channel_id] = &channel;

  channel.set_callback([this, channel_id](uint32_t, const uint8_t* data, size_t len,
                                          uint64_t log_time) {
    dispatch_message(channel_id, data, len, log_time);
  });

  advertise_channel_to_all(channel_id);
}
```

这就是第 3 章 callback 设计真正发挥作用的地方：server 不需要知道业务代码怎么产生消息，业务代码也不需要知道 WebSocket 细节。二者只通过 `set_callback()` 接上。

`add_channel()` 之所以必须显式调用，不是因为“写起来方便”，而是因为它同时完成两项具有副作用的操作：

1. 给 Channel 绑上 server 的分发逻辑
2. 把这个 Channel 广播给所有当前已连接客户端

如果把它做成隐式行为，调用者就无法推断“什么时候 channel 开始对外可见”。

### `dispatch_message()`：从 Channel 到 subscriber

```cpp
void dispatch_message(uint32_t channel_id, const uint8_t* data, size_t len,
                      uint64_t log_time) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& [wsi, session] : sessions_) {
    for (const auto& [sub_id, ch_id] : session->subscriptions) {
      if (ch_id == channel_id) {
        MessageData msg_data;
        msg_data.subscription_id = sub_id;
        msg_data.log_time = log_time;
        msg_data.data.assign(data, data + len);

        auto result = encode_message_data(msg_data);
        if (result.has_value()) {
          std::lock_guard<std::mutex> pending_lock(session->pending_mutex);
          session->pending_writes.push_back(std::move(result.value()));
          lws_callback_on_writable(wsi);
        }
        break;
      }
    }
  }

  if (context_) {
    lws_cancel_service(context_);
  }
}
```

这里要特别注意：server 不是把原始 payload 直接写给 client，而是必须重新包装成协议规定的 `messageData` 二进制帧。也正因为如此，本章的 server 依赖第 2 章的 `encode_message_data()`。

### `on_client_connected()`：连接建立后的第一批协议消息

第 4 章最“可见”的行为，其实不是 `dispatch_message()`，而是 client 一连上来就收到的 `serverInfo` 和 `advertise`。对应实现如下：

```cpp
void on_client_connected(struct lws* wsi) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto session = std::make_unique<ClientSession>();
  sessions_[wsi] = std::move(session);

  ServerInfo info;
  info.name = options_.name;
  info.capabilities = options_.capabilities;
  info.supported_encodings = {"json", "protobuf"};
  info.protocol_version = kProtocolVersion;

  auto result = encode_server_info(info);
  if (result.has_value()) {
    queue_text_message(wsi, result.value());
  }

  for (const auto& [channel_id, channel] : channels_) {
    advertise_channel(wsi, channel_id);
  }
}
```

它做了三件事：

1. 给新连接创建 `ClientSession`
2. 立刻发送 `serverInfo`
3. 把当前所有已注册 Channel 逐个 `advertise` 给这个 client

这也是为什么 §4.4 的集成测试里，client 连上后会先看到 `serverInfo`，然后看到 `advertise`，最后在订阅后才会收到 `messageData`。

### `on_writable()`：只有这里才能真正写 socket

```cpp
void on_writable(struct lws* wsi) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(wsi);
  if (it == sessions_.end()) return;

  auto& session = *it->second;
  std::lock_guard<std::mutex> pending_lock(session.pending_mutex);
  if (session.pending_writes.empty()) return;

  const auto& msg = session.pending_writes.front();

  size_t buf_len = LWS_PRE + msg.size();
  std::vector<uint8_t> buf(buf_len);
  std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());

  // 二进制协议帧目前只有 0x01(messageData) 和 0x02(time)，
  // 文本消息则以 '{' 开头（JSON，0x7B）。这里用 opcode 范围做轻量判定。
  bool is_binary = (msg.size() > 0 && msg[0] <= 0x02);
  lws_write_protocol protocol = is_binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;

  int written = lws_write(wsi, buf.data() + LWS_PRE, msg.size(), protocol);
  if (written >= 0) {
    session.pending_writes.erase(session.pending_writes.begin());
  }

  if (!session.pending_writes.empty()) {
    lws_callback_on_writable(wsi);
  }
}
```

这解释了本章为什么必须有 `pending_writes` 队列：业务线程只能“入队”，真正写出只能在 writable callback 中做。

> 💡 **⚡ 性能/并发 工程旁白：多线程与事件循环——哪些操作是线程安全的**
>
> 这一章最大的工程难点，不是 WebSocket 协议本身，而是线程边界。当前实现里同时存在三类活动线程：业务线程会调用 `channel.log()`，lws service 线程会跑 `lws_service()`，测试客户端也会在自己的线程里等待消息。
>
> 在真实运行时，通常只有前两类线程（业务线程 + lws 事件循环线程）；第三类“测试客户端线程”只存在于 `test_server.cpp` 的集成测试环境里，用来模拟真实外部连接。
>
> 线程安全规则非常明确：
>
> - `channels_`、`sessions_` 这类全局表由 `mutex_` 保护。
> - 每个 session 自己的待发送队列由 `pending_mutex` 保护，避免所有连接共享一个发送锁。
> - `lws_write()` 只能在 lws callback 上下文中执行，因此跨线程消息传递必须经过 `pending_writes`。
> - `lws_cancel_service()` 是关键唤醒机制：业务线程把消息入队后，要显式唤醒事件循环线程，否则对端可能要等到下一次超时轮询才能看到数据。
>
> 这套设计的意义是：锁只保护必要的共享状态，而真正的 socket 写入始终留在事件循环线程里完成。否则你要么会写出竞争条件，要么会把所有 client 的吞吐量串成一个大锁热点。

### `messageData` 二进制帧布局

server 发给 subscriber 的核心二进制帧仍然是 `messageData`：

```text
offset  size  field
0       1     opcode = 0x01
1       4     subscription_id (little-endian)
5       8     log_time (little-endian)
13      N     payload

┌──────┬──────────────────┬──────────────────────────────────┬─────────────┐
│ 0x01 │   sub_id (4 B)   │         log_time (8 B)           │ payload (N) │
│  1B  │  LE uint32       │         LE uint64                │             │
└──────┴──────────────────┴──────────────────────────────────┴─────────────┘
```

server 做的事情不是“创建一种新的帧”，而是把 `RawChannel::log()` 的原始消息根据订阅关系包装成这套协议规范要求的二进制布局。

---

## 4.4 测试：验证正确性

本章的重点是**协议交互**，所以测试策略也自然偏向集成测试，而不是 mock 一堆内部函数。

### 连接后立即收到 `serverInfo`

`tests/test_server.cpp` 中最关键的第一个测试：

```cpp
TEST_CASE("Server - sends serverInfo on connect" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18766;
  options.name = "TestServerInfo";
  options.capabilities = 0x1234;

  auto result = WebSocketServer::create(options);
  REQUIRE(result.has_value());
  auto server = std::move(result.value());

  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18766));

  auto msg_opt = client.wait_for_message();
  REQUIRE(msg_opt.has_value());
  REQUIRE(msg_opt->type == TestWsClient::ReceivedMessage::Text);

  std::string json_str(msg_opt->data.begin(), msg_opt->data.end());
  auto j = json::parse(json_str);
  REQUIRE(j["op"] == "serverInfo");
  REQUIRE(j["name"] == "TestServerInfo");
  REQUIRE(j["capabilities"] == 0x1234);
}
```

这说明：server 不只是“能监听端口”，而是已经在连接建立后立即发出了 Foxglove 协议要求的第一条控制消息。

### 订阅后收到 `messageData`

第二个关键测试覆盖完整链路：

```cpp
TEST_CASE("Server - delivers messageData to subscribers" "[integration]") {
  WebSocketServerOptions options;
  options.host = "127.0.0.1";
  options.port = 18768;
  options.name = "TestMessageDelivery";

  auto result = WebSocketServer::create(options);
  auto server = std::move(result.value());

  std::string schema_json = R"({"type":"object"})";
  std::vector<uint8_t> schema_data(schema_json.begin(), schema_json.end());
  Schema schema{"TestSchema", "jsonschema", schema_data};
  auto channel_result = RawChannel::create("/test/topic", "json", schema);
  REQUIRE(channel_result.has_value());
  auto channel = std::move(channel_result.value());
  server.add_channel(channel);

  TestWsClient client;
  REQUIRE(client.connect("127.0.0.1", 18768));

  auto msg1 = client.wait_for_message();  // serverInfo
  auto msg2 = client.wait_for_message();  // advertise
  REQUIRE(msg1.has_value());
  REQUIRE(msg2.has_value());

  std::string json_str(msg2->data.begin(), msg2->data.end());
  auto j = json::parse(json_str);
  uint32_t channel_id = j["channels"][0]["id"];

  json subscribe_msg = {
      {"op", "subscribe"},
      {"subscriptions", {{{"id", 1}, {"channelId", channel_id}}}}
  };
  client.send_text(subscribe_msg.dump());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string test_data = "{\"value\":42}";
  std::vector<uint8_t> msg_payload(test_data.begin(), test_data.end());
  channel.log(msg_payload.data(), msg_payload.size(), 1234567890ULL);

  auto msg3 = client.wait_for_message();
  REQUIRE(msg3.has_value());
  REQUIRE(msg3->type == TestWsClient::ReceivedMessage::Binary);
  REQUIRE(msg3->data.size() >= 13);
  REQUIRE(msg3->data[0] == 1);
}
```

这一条测试同时验证了：

1. `add_channel()` 会触发 `advertise`
2. 客户端 `subscribe` 会被正确记录
3. `channel.log()` 会经由 server 转成 binary `messageData`

### 为什么 pending queue 不是“多此一举”？

因为 lws 的限制不是“建议”，而是“约束”：真正 `lws_write()` 的时机必须由 writable callback 驱动。如果在 `dispatch_message()` 里跨线程直接写，测试会变成时灵时不灵的竞态灾难。

### 运行测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期输出至少包含：

```text
test_error
test_protocol
test_channel
test_server
100% tests passed
```

---

## 4.5 与官方实现对比

本教程实现的是一条“可看清”的最小主干；官方实现则更偏向完整生产能力。

| 方面 | 本教程实现 | 官方 SDK 倾向 |
|------|-----------|--------------|
| 公开接口 | `WebSocketServer` + `Options` + PIMPL | 更完整的高层抽象与运行时集成 |
| Channel 接入 | 显式 `add_channel()` | 更靠近 Context / sink 体系 |
| 写路径 | `pending_writes` + writable callback | 同样尊重事件循环约束，但封装更深 |
| 教学价值 | 高：每个阶段都可见 | 高：更真实，但理解门槛更高 |

### 为什么本教程保留显式 `add_channel()`？

因为这能把“server 是否存在”从 “channel 是否存在”里拆出去。你可以先创建 Channel，再决定它接到哪个 server；在测试场景里，你甚至可以完全不启 server，只测 callback 语义。这种组合自由度正是教程实现的优势。

### 官方实现更强的地方

官方实现往往在这些方面更成熟：

- 更完整的连接能力协商
- 更丰富的错误报告与调试入口
- 更系统化的资源管理与上层路由整合

本教程故意不追求“一次到位”，而是优先保证每一层都能单独看懂、单独测试。第 6 章引入 Context 后，你会看到这条 server 路径如何重新被纳入更大的路由体系。

---

## 4.6 打 tag，验证完成

按统一流程验证本章：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

git add .
git commit -m "feat(ch4): add websocket server and channel integration"
git tag my-v0.4-server
git diff v0.4-server HEAD -- include/foxglove/server.hpp
git diff v0.4-server HEAD -- src/server.cpp
```

这里依然遵循整套教程的统一规则：

- `ctest` 全绿是唯一硬标准
- `git diff` 只用于帮助你理解与参考实现的差异
- tag 用 `my-` 前缀，避免污染上游 tag

如果测试失败，优先排查：

1. `protocols_` 的子协议字符串是否是 `foxglove.websocket.v1`
2. `on_client_connected()` 是否真的发送了 `serverInfo`
3. `dispatch_message()` 是否把消息放进了 `pending_writes`
4. `on_writable()` 是否正确使用了 `LWS_PRE`
5. `add_channel()` 是否正确调用了 `set_callback()`

---

## 本章小结

本章第一次把“本地对象”变成了“网络服务”。

**核心交付物**：

| 文件 | 作用 |
|------|------|
| `include/foxglove/server.hpp` | 定义 `WebSocketServerOptions`、`WebSocketServer` 公开接口 |
| `src/server.cpp` | 实现 lws context、service thread、连接管理、订阅管理、pending queue |
| `tests/test_server.cpp` | 用集成测试验证连接、serverInfo、advertise、subscribe、messageData |

**本章学到的工程知识**：

- 事件循环线程和业务线程必须通过队列解耦
- `add_channel()` 的显式注册让系统组合更灵活
- lws 的 writable callback 模型决定了不能跨线程直接写 socket
- `LWS_PRE` 是 libwebsockets 写路径里必须尊重的内存约束
- PIMPL 让公开头文件不泄露 libwebsockets 依赖

**工程知识点**：

- `libwebsockets 事件循环模型 vs asio`
- `多线程与事件循环：哪些操作是线程安全的`

**延伸练习**：

1. 给 `WebSocketServerOptions` 增加 `supported_encodings` 配置项，而不是在 `on_client_connected()` 中写死 `{"json", "protobuf"}`。
2. 为 `TestWsClient` 增加对子协议协商失败场景的测试，验证错误子协议不会被错误地接入 Foxglove 路径。
3. 尝试把 `pending_writes` 从 `std::vector` 改成 `std::deque`，思考频繁 `erase(begin())` 对复杂度和缓存局部性的影响。

**参考文档**：`docs/04-WebSocket服务器.md`

**下一章**：第 5 章将实现消息序列化层。到那时，你会发现 server 虽然已经能收发协议消息，但业务层仍然在处理“字节数组”——而序列化层正是把业务对象变成这些字节的桥梁。
