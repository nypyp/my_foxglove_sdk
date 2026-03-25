# 第四章：WebSocket 服务器

本章实现 `WebSocketServer`，将第三章的 `RawChannel` 与 Foxglove WebSocket 协议连接起来。服务器管理客户端连接、处理订阅请求，并将 Channel 消息路由到已订阅的客户端。

## 核心概念

### libwebsockets 架构

我们使用 libwebsockets（lws）作为 WebSocket 底层库。理解其架构对正确使用至关重要：

```cpp
// lws 使用静态协议数组，lws 会保持指向此数组的原始指针
static constexpr lws_protocols protocols[] = {
    {
        "foxglove.websocket.v1",  // 协议名称
        callback_function,         // 回调函数
        sizeof(ClientSession),     // 每个连接的用户数据大小
        4096,                      // rx 缓冲区大小
    },
    {nullptr, nullptr, 0, 0}  // 终止符
};
```

**关键设计点**：

1. **事件循环**：`lws_service(context, timeout_ms)` 在单独线程中运行，处理所有 I/O 事件
2. **回调驱动**：lws 通过回调函数通知事件（连接、断开、可写、接收消息）
3. **协议数组**：必须使用 `static constexpr` 数组，lws 内部保持指针，不能使用 `std::vector`

### 子协议协商

Foxglove 协议定义了特定的子协议名称 `foxglove.websocket.v1`：

```cpp
// 在 LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION 中验证
static int callback_function(lws* wsi, lws_callback_reasons reason,
                             void* user, void* in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            // 验证客户端请求的 subprotocol 是否匹配
            const char* requested = (const char*)in;
            if (strcmp(requested, "foxglove.websocket.v1") != 0) {
                return -1;  // 拒绝连接
            }
            break;
        }
        // ...
    }
    return 0;
}
```

客户端必须在 WebSocket 握手时请求此子协议，服务器验证后才接受连接。

### 服务器生命周期与线程模型

```
创建 (create)
    │
    ▼
启动 Service 线程 ──► lws_service 事件循环
    │
    ├── 新客户端连接 ──► 创建 ClientSession
    │                       │
    │                       ├── 发送 serverInfo
    │                       └── 广播 advertise 所有 Channel
    │
    ├── 收到 Subscribe ──► 更新 subscription map
    │
    ├── 收到 Unsubscribe ──► 从 subscription map 移除
    │
    ├── Channel.log() ──► dispatch_message()
    │                         │
    │                         └── 构建二进制 frame ──► 加入客户端 pending_writes
    │
    └── on_writable() ──► 发送 pending 消息
                              │
                              └── lws 限制：每次回调只能写一条消息

关闭 (shutdown)
    │
    ▼
停止事件循环 ──► 清理所有 ClientSession
```

**线程安全设计**：

| 组件 | 线程 | 同步机制 |
|------|------|----------|
| `lws_service` 循环 | Service 线程 | lws 内部单线程 |
| `add_channel/remove_channel` | 用户线程 | `mutex_` |
| `broadcast_time` | 用户线程 | `mutex_` + `lws_cancel_service` |
| `dispatch_message` | Channel 回调线程 | `pending_mutex` per session |

## Channel 注册：显式绑定

与官方 SDK 的自动绑定不同，我们采用显式注册模式：

```cpp
// 创建 Channel（还没有绑定到任何服务器）
auto channel = RawChannel::create("/sensor/data", "json", schema);

// 显式注册到服务器
server.add_channel(channel);
// 内部实现：
// 1. channel.set_callback(server_dispatch_fn) 设置回调
// 2. 存储 channel 指针到 channels_ 映射表
// 3. 向所有已连接客户端发送 advertise 消息
```

**为什么要显式注册？**

1. **延迟绑定**：创建 Channel 时不需要 Server 已存在
2. **灵活路由**：同一个 Channel 可以被添加到多个 Server，或动态切换
3. **清晰控制**：注册和注销的时机由用户代码决定

```cpp
// 移除 Channel
server.remove_channel(channel_id);
// 内部实现：
// 1. channel->set_callback(nullptr) 取消回调绑定
// 2. 从 channels_ 映射表移除
// 3. 向所有客户端发送 unadvertise
```

## 订阅管理

每个客户端连接维护一个 `ClientSession`：

```cpp
struct ClientSession {
    lws* wsi;
    std::unordered_map<uint32_t, uint32_t> subscriptions;
    // subscription_id -> channel_id 映射

    std::mutex pending_mutex;
    std::vector<std::vector<uint8_t>> pending_writes;
    // 待发送的消息队列（因为 lws 每次只能写一条）

    std::set<uint32_t> subscribed_channels;
    // 已订阅的 channel_id 集合
};
```

**订阅流程**：

```cpp
// 收到客户端 Subscribe 消息
void on_client_subscribe(ClientSession* session, const Subscribe& msg) {
    for (const auto& sub : msg.subscriptions) {
        session->subscriptions[sub.subscription_id] = sub.channel_id;
        session->subscribed_channels.insert(sub.channel_id);
    }
}
```

**消息分发**：

```cpp
// 从 Channel 回调调用
void dispatch_message(uint32_t channel_id, const uint8_t* data,
                      size_t len, uint64_t log_time) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 查找订阅此 channel 的所有客户端
    for (auto& [session_id, session] : sessions_) {
        if (session->subscribed_channels.count(channel_id)) {
            // 找到该 channel 对应的 subscription_id
            uint32_t sub_id = get_subscription_id(session, channel_id);

            // 构建二进制帧并加入待发送队列
            auto frame = build_message_data_frame(sub_id, log_time, data, len);

            std::lock_guard<std::mutex> pending_lock(session->pending_mutex);
            session->pending_writes.push_back(std::move(frame));
        }
    }

    // 唤醒 lws 事件循环，触发 writable 回调
    lws_cancel_service(context_);
}
```

## 二进制帧构造

Foxglove 协议使用小端字节序的二进制格式传输消息数据：

```cpp
// messageData 帧格式：
// ┌─────────┬─────────────────┬─────────────────┬──────────┐
// │ opcode  │ subscription_id │    log_time     │  data    │
// │ 1 byte  │    4 bytes      │    8 bytes      │  N bytes │
// │  0x01   │   uint32_le     │   uint64_le     │  payload │
// └─────────┴─────────────────┴─────────────────┴──────────┘

std::vector<uint8_t> build_message_data_frame(uint32_t subscription_id,
                                               uint64_t log_time,
                                               const uint8_t* data,
                                               size_t len) {
    std::vector<uint8_t> frame;
    frame.reserve(1 + 4 + 8 + len);

    // Opcode
    frame.push_back(0x01);  // messageData

    // subscription_id (little-endian)
    frame.push_back(subscription_id & 0xFF);
    frame.push_back((subscription_id >> 8) & 0xFF);
    frame.push_back((subscription_id >> 16) & 0xFF);
    frame.push_back((subscription_id >> 24) & 0xFF);

    // log_time (little-endian)
    for (int i = 0; i < 8; ++i) {
        frame.push_back((log_time >> (i * 8)) & 0xFF);
    }

    // payload
    frame.insert(frame.end(), data, data + len);

    return frame;
}
```

**时间广播**：

```cpp
void broadcast_time(uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);

    // time 帧格式：opcode(0x02) + timestamp(8 bytes)
    std::vector<uint8_t> frame(9);
    frame[0] = 0x02;  // time opcode

    // timestamp (little-endian)
    for (int i = 0; i < 8; ++i) {
        frame[1 + i] = (timestamp >> (i * 8)) & 0xFF;
    }

    // 加入所有客户端的待发送队列
    for (auto& [session_id, session] : sessions_) {
        std::lock_guard<std::mutex> pending_lock(session->pending_mutex);
        session->pending_writes.push_back(frame);
    }

    // 唤醒事件循环
    lws_cancel_service(context_);
}
```

## libwebsockets 陷阱与解决方案

在使用 lws 过程中，我们遇到了几个关键的陷阱：

### 陷阱 1：静态协议数组

```cpp
// 错误：使用 std::vector
std::vector<lws_protocols> protocols = {...};  // ❌ lws 会保存指针，vector 重分配后指针失效

// 正确：使用 static constexpr
static constexpr lws_protocols protocols[] = {...};  // ✅ 静态存储期，指针稳定
```

### 陷阱 2：每次回调只能写一条消息

```cpp
static int callback_function(lws* wsi, lws_callback_reasons reason,
                             void* user, void* in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // lws 限制：每次 writable 回调只能调用一次 lws_write
            // 如果有多条消息，需要安排下一次 writable 回调

            auto* session = static_cast<ClientSession*>(user);

            std::lock_guard<std::mutex> lock(session->pending_mutex);
            if (!session->pending_writes.empty()) {
                auto msg = std::move(session->pending_writes.front());
                session->pending_writes.erase(session->pending_writes.begin());

                // 写入一条
                lws_write(wsi, msg.data(), msg.size(), LWS_WRITE_BINARY);

                // 如果还有更多消息，请求下一次 writable 回调
                if (!session->pending_writes.empty()) {
                    lws_callback_on_writable(wsi);
                }
            }
            break;
        }
    }
    return 0;
}
```

### 陷阱 3：跨线程唤醒事件循环

```cpp
// 问题：从非 lws 线程（如 Channel 回调线程）向客户端发送消息时，
// 需要唤醒 lws 的事件循环来处理 writable 事件

// 解决方案：使用 lws_cancel_service
void queue_message_from_channel(...) {
    // 在用户线程中将消息加入队列
    {
        std::lock_guard<std::mutex> lock(session->pending_mutex);
        session->pending_writes.push_back(frame);
    }

    // 唤醒 lws 事件循环，触发 writable 回调
    // 这个函数是线程安全的，可以从任何线程调用
    lws_cancel_service(context_);
}
```

## 与官方 SDK 的对比

| 特性 | 我们的实现 | 官方 SDK |
|------|-----------|---------|
| 架构 | 纯 C++，直接使用 libwebsockets | C++ wrapper 封装 Rust 核心（FFI） |
| 源码可见性 | 完整的 WebSocket 协议实现在代码中 | 底层在 Rust 中，通过 FFI 调用 |
| 回调机制 | C++ std::function | C 函数指针包装 C++ std::function |
| Channel ID | `uint32_t` | `uint64_t` |
| Channel 注册 | 显式 `add_channel()` | 通过 Context 自动管理 |
| 停止方法 | `shutdown()` 返回 void | `stop()` 返回 `FoxgloveError` |
| 端口查询 | 无 | `port()` getter |
| 客户端计数 | 无 | `clientCount()` getter |
| 功能支持 | 核心功能（Publish, Subscribe, Time） | 更多能力（ClientPublish, Parameters, Services, Assets, PlaybackControl, ConnectionGraph） |
| 线程模型 | 显式 Service 线程 + mutex | Rust 内部管理 |
| 教育价值 | 高，协议实现完全可见 | 封装较好，细节隐藏在 Rust 中 |

## 测试覆盖

本章测试涵盖 8 个场景：

1. **服务器创建和启动**：验证 `WebSocketServer::create()` 成功，端口监听正常
2. **连接时发送 serverInfo**：客户端连接后立即收到 JSON 格式的 serverInfo
3. **Channel 广播 advertise**：`add_channel()` 后，已连接客户端收到 advertise 消息
4. **消息投递到订阅者**：订阅 Channel 后，`channel.log()` 触发二进制 messageData 帧发送
5. **优雅关闭**：`shutdown()` 不崩溃，资源正确释放
6. **移除 Channel 取消广播**：`remove_channel()` 发送 unadvertise，客户端不再接收该 Channel 消息
7. **多客户端连接**：多个客户端可同时连接，各自独立订阅
8. **时间广播**：`broadcast_time()` 向所有客户端发送时间帧

## 下一步

第五章将实现**消息序列化**，涵盖：

- `Timestamp`、`Vector3`、`Quaternion`、`Pose` 等基础类型的 JSON 序列化
- ADL（Argument Dependent Lookup）`to_json`/`from_json` 模式
- Golden-file 测试方法
- NaN 处理策略（转换为 null）
- 字母序字段排序（Foxglove 协议要求）

## 示例代码

见 `examples/ch04_server/main.cpp`：

```cpp
#include <foxglove/server.hpp>
#include <foxglove/channel.hpp>
#include <foxglove/schema.hpp>

#include <cstdio>
#include <thread>
#include <chrono>

int main() {
    // 创建服务器
    foxglove::WebSocketServerOptions options;
    options.port = 8765;
    options.name = "My Foxglove Server";

    auto server_result = foxglove::WebSocketServer::create(options);
    if (!server_result.has_value()) {
        printf("Failed to create server\n");
        return 1;
    }
    auto& server = server_result.value();

    // 创建 Schema
    std::vector<uint8_t> schema_data = {
        '{', '"', 't', 'y', 'p', 'e', '"', ':', '"', 'o', 'b', 'j', 'e', 'c', 't', '"', '}'
    };
    foxglove::Schema schema{"Counter", "jsonschema", schema_data};

    // 创建 Channel
    auto channel_result = foxglove::RawChannel::create(
        "/counter", "json", schema);
    if (!channel_result.has_value()) {
        printf("Failed to create channel\n");
        return 1;
    }
    auto& channel = channel_result.value();

    // 注册到服务器
    server.add_channel(channel);

    printf("Server running on ws://localhost:8765\n");
    printf("Connect with Foxglove Studio to view the counter\n");

    // 发布计数器消息，每 100ms 一次，持续 30 秒
    for (int i = 0; i < 300; ++i) {
        std::string msg = "{\"value\":" + std::to_string(i) + "}";
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        channel.log(reinterpret_cast<const uint8_t*>(msg.data()),
                    msg.size(), timestamp);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    printf("Shutting down...\n");
    server.shutdown();
    return 0;
}
```
