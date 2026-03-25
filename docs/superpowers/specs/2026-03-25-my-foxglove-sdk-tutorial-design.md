# Design Spec: my_foxglove_sdk — A From-Scratch C++ Foxglove SDK Tutorial

**Date**: 2026-03-25
**Status**: Reviewed (Oracle review incorporated)
**Approach**: Protocol-first, bottom-up (方案 A)

---

## 1. Purpose

Build an educational C++ project that teaches how to construct a Foxglove SDK from scratch, following the incremental methodology of 陈硕's muduo book (《Linux多线程服务端编程》). The project serves as both a working SDK implementation and a progressive tutorial.

### Success Criteria

1. A C++ middle-level developer can follow the tutorial from Chapter 0 to Chapter 9 and build a working Foxglove SDK
2. Each chapter (git tag) compiles, runs, and passes its own tests
3. The final product connects to Foxglove Studio and displays live 3D visualization data
4. Code strictly follows conventions extracted from the official foxglove-sdk source code
5. Each chapter's public API surface and major architecture choices are compared against the official implementation in `third-party/foxglove-sdk/`

### Non-Goals

- Full feature parity with official SDK (we implement core subset only)
- Rust core or C FFI layer (we use pure C++)
- Services, Parameters, Connection Graph, Asset fetching (out of scope)
- Python/TypeScript bindings
- Production-grade performance optimization

---

## 2. Target Audience

**C++ middle-level developers** who:
- Know C++17, CMake, and basic WebSocket concepts
- Want to understand how SDK-level software is designed
- Want to learn Foxglove protocol internals
- Appreciate the "build it to understand it" teaching philosophy

---

## 3. Repository Structure

```
my_foxglove_sdk/
├── CMakeLists.txt                    # Top-level build configuration
├── cmake/
│   └── dependencies.cmake            # FetchContent dependency management
├── .clang-format                     # Code style (extracted from official SDK)
├── third-party/
│   └── foxglove-sdk/                 # git submodule: official source (read-only reference)
│
├── include/foxglove/                 # Public headers
│   ├── foxglove.hpp                  # Aggregate header
│   ├── error.hpp                     # FoxgloveResult<T> error handling
│   ├── protocol.hpp                  # Foxglove WebSocket Protocol v1 definitions
│   ├── schema.hpp                    # Schema definition
│   ├── channel.hpp                   # Channel abstraction
│   ├── server.hpp                    # WebSocketServer
│   ├── context.hpp                   # Context + Sink routing
│   ├── mcap.hpp                      # MCAP writer
│   └── messages.hpp                  # Built-in message types
│
├── src/                              # Implementation (1:1 with headers)
│   ├── error.cpp
│   ├── protocol.cpp
│   ├── channel.cpp
│   ├── server.cpp
│   ├── context.cpp
│   ├── mcap.cpp
│   └── messages.cpp
│
├── tests/                            # Unit tests (Catch2)
│   ├── CMakeLists.txt
│   ├── test_error.cpp
│   ├── test_protocol.cpp
│   ├── test_channel.cpp
│   ├── test_server.cpp
│   ├── test_context.cpp
│   ├── test_mcap.cpp
│   └── test_messages.cpp
│
├── examples/                         # Per-chapter runnable examples
│   ├── ch01_skeleton/
│   ├── ch02_protocol/
│   ├── ch03_channel/
│   ├── ch04_server/
│   ├── ch05_serialization/
│   ├── ch06_context/
│   ├── ch07_mcap/
│   ├── ch08_messages/
│   └── ch09_e2e/
│
└── docs/                             # Tutorial documents
    ├── 00-前言与项目概览.md
    ├── 01-项目骨架与代码规范.md
    ├── 02-Foxglove协议解析.md
    ├── 03-Channel与Schema抽象.md
    ├── 04-WebSocket服务器.md
    ├── 05-消息序列化.md
    ├── 06-Context与Sink路由.md
    ├── 07-MCAP文件写入.md
    ├── 08-内置消息类型.md
    └── 09-端到端连接Foxglove-Studio.md
```

---

## 4. Code Style & Standards (Extracted from Official SDK)

All conventions are derived from `third-party/foxglove-sdk/cpp/` source code. The tutorial enforces these from Chapter 1.

### 4.1 Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes / Structs | `PascalCase` | `WebSocketServer`, `RawChannel` |
| Functions / Methods | `snake_case` | `create()`, `broadcast_time()` |
| Variables | `snake_case` | `channel_id`, `log_time` |
| Constants / Enums | `UPPER_SNAKE_CASE` | `MAX_CHANNELS` |
| Enum values (scoped) | `PascalCase` | `Capability::ClientPublish` |
| Private members | `snake_case_` (trailing underscore) | `server_`, `channels_` |
| Namespaces | `snake_case` | `foxglove`, `foxglove::messages` |
| File names | `snake_case.hpp` / `snake_case.cpp` | `server.hpp`, `channel.cpp` |

### 4.2 Header File Conventions

- Use `#pragma once` (no `#ifndef` guards)
- Include order: project headers → third-party → standard library, each group alphabetically sorted
- Forward-declare when possible to minimize includes in headers

### 4.3 Class Design Patterns

- **Factory methods**: `static FoxgloveResult<T> create(Options)` — never expose constructors for complex types
- **PIMPL idiom**: Use `std::unique_ptr<Impl>` for ABI stability on public-facing classes
- **Result types**: `FoxgloveResult<T>` (based on `tl::expected`) — no exceptions
- **Thread safety**: Document thread-safety guarantees per class; use `std::mutex` + `std::atomic` where needed
- **Move semantics**: Classes that manage resources are move-only (deleted copy ctor/assignment)

### 4.4 Comment & Documentation Style

```cpp
/// @brief Brief one-line description.
///
/// Extended description if needed. Explain the "why", not the "what".
///
/// @param options Server configuration options.
/// @return The created server, or an error if creation failed.
///
/// @note Thread-safety: This function is not thread-safe.
///
/// @code
/// auto result = WebSocketServer::create(options);
/// if (!result) { /* handle error */ }
/// @endcode
static FoxgloveResult<WebSocketServer> create(WebSocketServerOptions options);
```

- Every public API must have a `/// @brief` doc comment
- Use `@param`, `@return`, `@note`, `@code` Doxygen tags
- Inline comments (`//`) for implementation details only
- Each file starts with a brief module-level `///` comment

### 4.5 Testing Standards

- Framework: Catch2 v3
- One test file per module: `test_<module>.cpp`
- Structure: `TEST_CASE("ModuleName - behavior description") { SECTION("specific scenario") { ... } }`
- Every public API must have at least one test
- Test categories:
  - **Unit**: Isolated, no I/O, fast
  - **Integration**: May use network (tagged `[integration]`)
  - **Roundtrip**: Encode → decode → compare (for serialization)
- Naming: Tests describe behavior, not implementation (`"Channel - assigns unique IDs"`, not `"test_channel_id_counter"`)

### 4.6 Build Configuration

- C++ Standard: C++17
- CMake minimum: 3.20
- Dependencies via FetchContent:
  - `libwebsockets` — WebSocket server
  - `nlohmann_json` — JSON handling
  - `Catch2` v3 — Testing
  - `tl-expected` — C++23 `std::expected` backport
  - `zstd` — Compression (for MCAP)

---

## 5. Chapter Breakdown (Progressive Build)

### Chapter 0: 前言与项目概览
**Tag**: None (documentation only)
**Deliverables**: Understanding of Foxglove ecosystem

- Foxglove ecosystem: Studio (visualization), MCAP (file format), WebSocket protocol (live streaming)
- What we are building: a pure C++ SDK that can stream data to Foxglove Studio and record to MCAP files
- Relationship to official SDK: our implementation vs Rust-core + C FFI + C++ wrapper
- Development environment: compiler, CMake, Foxglove Studio installation
- How to use `third-party/foxglove-sdk/` as a reference throughout the tutorial

---

### Chapter 1: 项目骨架与代码规范
**Tag**: `v0.1-skeleton`
**New files**: `CMakeLists.txt`, `cmake/dependencies.cmake`, `.clang-format`, `error.hpp/cpp`, `test_error.cpp`
**Dependencies introduced**: tl-expected, Catch2

**Content**:
1. Initialize CMake project with FetchContent for dependency management
2. Add `third-party/foxglove-sdk` as git submodule
3. Extract `.clang-format` from official SDK style
4. Implement `FoxgloveResult<T>`:
   - `tl::expected<T, FoxgloveError>` wrapper
   - `FoxgloveError` enum with error codes matching official SDK
   - Helper macros: `FOXGLOVE_TRY(expr)` for early return on error
5. First test: `test_error.cpp` — verify Result construction, error propagation, TRY macro

**Official comparison**: `cpp/foxglove/include/foxglove/error.hpp`, `cpp/foxglove/include/foxglove/expected.hpp`

**Milestone**: `cmake --build build && ctest --test-dir build` passes.

---

### Chapter 2: Foxglove WebSocket 协议解析
**Tag**: `v0.2-protocol`
**New files**: `protocol.hpp/cpp`, `test_protocol.cpp`, `examples/ch02_protocol/`
**Dependencies introduced**: nlohmann_json

**Content**:
1. Foxglove WebSocket Protocol v1 specification overview
   - Subprotocol: `foxglove.websocket.v1`
   - Server → Client ops: `serverInfo`, `advertise`, `unadvertise`, `messageData`, `time`, `status`
   - Client → Server ops: `subscribe`, `unsubscribe`
2. Data structures:
   - `ServerInfo`: name, capabilities, supported encodings, metadata, session ID
   - `ChannelAdvertisement`: id, topic, encoding, schema_name, schema_encoding, schema_data
   - `Subscribe`/`Unsubscribe`: subscription ID ↔ channel ID mapping
   - `MessageData`: binary frame — `uint8 opcode | uint32 subscriptionId | uint64 timestamp | payload`
3. JSON encoding/decoding for text-based messages
4. Binary encoding/decoding for `messageData` frames
5. Tests: roundtrip encode→decode for every message type, malformed input handling

**Official comparison**: `rust/foxglove/src/websocket/protocol.rs` (Rust core), cross-reference with [ws-protocol spec](https://github.com/foxglove/ws-protocol/blob/main/docs/spec.md)

**Milestone**: All protocol message types encode/decode correctly: `encode(msg) → bytes → decode(bytes) → msg2`, where `msg == msg2` for all fields. Malformed input tests verify graceful error returns (not crashes).

---

### Chapter 3: Channel 与 Schema 抽象
**Tag**: `v0.3-channel`
**New files**: `schema.hpp`, `channel.hpp/cpp`, `test_channel.cpp`, `examples/ch03_channel/`

**Architecture note**: In this chapter, `RawChannel` is a standalone data holder with a callback-based output. It does NOT know about servers or sinks. The `MessageCallback` function type is the sole output mechanism. This avoids circular dependencies — the server (Ch4) and context (Ch6) will each provide their own callback implementations.

**Content**:
1. `Schema` structure:
   - `name`: schema identifier (e.g., `"foxglove.SceneUpdate"`)
   - `encoding`: `"jsonschema"` (this tutorial uses JSON serialization throughout; see Serialization Decision below)
   - `data`: raw schema bytes (JSON Schema document)
2. `ChannelDescriptor`:
   - `topic`: unique topic string
   - `schema`: Schema reference
   - `encoding`: message encoding format (always `"json"` in this tutorial)
3. `MessageCallback`: `std::function<void(channel_id, const uint8_t* data, size_t len, uint64_t log_time)>`
4. `RawChannel` lifecycle:
   - `static FoxgloveResult<RawChannel> create(topic, encoding, schema, MessageCallback)` — factory
   - `void log(const uint8_t* data, size_t len, uint64_t log_time)` — invokes callback
   - `void close()` — marks channel closed, rejects further `log()` calls
   - Atomic `channel_id` allocation (global counter)
   - Thread-safe `log()` via internal mutex
5. Concurrency contract:
   - `log()` may be called from any thread; internally serialized via mutex
   - `close()` blocks until in-flight `log()` calls complete; subsequent `log()` calls return immediately (no-op)
   - Message ordering: messages from the same thread are delivered in call order; cross-thread ordering is not guaranteed
6. Channel ID uniqueness guarantee (atomic global counter)
7. Tests: create/close lifecycle, concurrent log from multiple threads, ID uniqueness, close-during-log race

**Official comparison**: `cpp/foxglove/include/foxglove/channel.hpp`

**Milestone**: Channels can be created, log data via callback, and close safely from multiple threads.

---

### Chapter 4: WebSocket Server
**Tag**: `v0.4-server`
**New files**: `server.hpp/cpp`, `test_server.cpp`, `examples/ch04_server/`
**Dependencies introduced**: libwebsockets

**Architecture note**: The server explicitly manages channel registration. Users call `server.add_channel(channel)` to advertise and `server.remove_channel(channel_id)` to unadvertise. There is no auto-wiring — the server provides a `MessageCallback` that channels can use to push data.

**Content**:
1. libwebsockets integration:
   - Event loop setup (`lws_context`, `lws_protocols`)
   - Connection lifecycle callbacks
   - Subprotocol negotiation: `foxglove.websocket.v1`
2. `WebSocketServerOptions`:
   - `host`, `port` (default: `127.0.0.1:8765`)
   - `name` (server name for `serverInfo`)
   - `capabilities` bitmask (in-scope values listed in Appendix A)
3. `WebSocketServer` class:
   - `static FoxgloveResult<WebSocketServer> create(WebSocketServerOptions)`
   - Internal event loop thread (runs libwebsockets service loop)
   - `add_channel(RawChannel&)` — advertise to connected clients, wire channel's callback to server dispatch
   - `remove_channel(channel_id)` — unadvertise
   - Subscription tracking: map `subscriptionId → channelId → client`
   - Message dispatch: channel callback → find subscribers → binary frame → send
4. `WebSocketServerCallbacks`:
   - `on_subscribe(channel_id, client_id)`
   - `on_unsubscribe(channel_id, client_id)`
5. Concurrency contract:
   - `add_channel()` / `remove_channel()`: callable from any thread; internally serialized
   - Message sending is async (queued to event loop thread)
   - Callbacks (`on_subscribe`, etc.) run on the event loop thread
   - `shutdown()` blocks until event loop exits and all pending sends are flushed or dropped
6. Connection management: client connect/disconnect handling, graceful shutdown
7. Tests:
   - Mock WebSocket client connects, receives `serverInfo` with correct fields
   - `add_channel()` → client sees `advertise` JSON message
   - Subscribe → send messageData → client receives correct binary frame (verify opcode, channelId, timestamp, payload)
   - Multiple clients with different subscriptions
   - Graceful shutdown with connected clients

**Official comparison**: `cpp/foxglove/include/foxglove/server.hpp`, `cpp/foxglove/src/server.cpp`

**Milestone**: Run example → open Foxglove Studio → connect to `ws://localhost:8765` → Studio shows connected state and receives `serverInfo`. Raw data frames are delivered for subscribed channels. (Note: meaningful visualization requires typed messages from Ch5/Ch8; this chapter proves protocol-level connectivity.)

---

### Chapter 5: 消息序列化
**Tag**: `v0.5-serialization`
**New files**: `messages.hpp/cpp` (partial), `test_messages.cpp`, `examples/ch05_serialization/`

**Serialization Decision**: This tutorial uses **JSON + JSON Schema** exclusively. All message encodings use `"json"`, and all schema encodings use `"jsonschema"`. This intentionally diverges from the official SDK which uses Protobuf. Foxglove Studio supports both; JSON is chosen for educational clarity. Protobuf support is listed as a reader exercise in Chapter 9.

**Content**:
1. Base types with JSON serialization:
   - `Timestamp { sec, nsec }` → `{ "sec": N, "nsec": N }`
   - `Duration { sec, nsec }`
   - `Vector3 { x, y, z }`
   - `Quaternion { x, y, z, w }`
   - `Pose { position: Vector3, orientation: Quaternion }`
   - `Color { r, g, b, a }` (all fields are `double` in range [0, 1])
2. JSON Schema generation:
   - Each type has a static `json_schema()` method returning its JSON Schema definition
   - Used for channel advertisement `schema_data` field
   - Schema names must match Foxglove's canonical names (e.g., `"foxglove.SceneUpdate"`) for Studio compatibility
3. Serialization interface:
   - `encode(const T& msg) → std::vector<uint8_t>` (JSON UTF-8 bytes)
   - `decode(const uint8_t* data, size_t len) → FoxgloveResult<T>`
4. nlohmann_json integration: `to_json`/`from_json` ADL customization points
5. JSON edge case policy:
   - `NaN` and `Infinity`: serialized as `null` (JSON has no NaN/Inf representation)
   - Field ordering: alphabetical (for deterministic output and golden-file testing)
   - Floating-point: full precision via `nlohmann::json::number_float_t` (no truncation)
6. Tests:
   - Roundtrip for every type: `T → encode → decode → T`, assert equality
   - Edge cases: NaN fields → null → decoded as 0.0 (with documented lossy behavior)
   - Golden-file: encode known values → compare against checked-in JSON files
   - Schema validation: generated JSON Schema is valid JSON Schema Draft-07

**Official comparison**: `cpp/foxglove/include/foxglove/messages.hpp` (auto-generated from proto), `schemas/` proto definitions

**Milestone**: All base types serialize/deserialize correctly; golden-file tests pass; JSON Schemas match Foxglove canonical schema names.

---

### Chapter 6: Context 与 Sink 路由
**Tag**: `v0.6-context`
**New files**: `context.hpp/cpp`, `test_context.cpp`, `examples/ch06_context/`

**Architecture note**: This chapter introduces the `Sink` abstraction and `Context` as a multiplexer. This is a refactoring chapter — `RawChannel`'s `MessageCallback` is now provided by `Context` instead of directly by `WebSocketServer`. The `WebSocketServer` becomes one `Sink` among potentially many.

**Content**:
1. `Sink` abstract interface:
   - `void on_channel_advertise(ChannelDescriptor)`
   - `void on_channel_unadvertise(channel_id)`
   - `void on_message(channel_id, data, len, log_time)`
2. `WebSocketServerSink`: adapter wrapping `WebSocketServer` as a `Sink`
3. `Context` class:
   - Binds channels to sinks — provides a `MessageCallback` that fans out to all registered sinks
   - Default global context (`Context::default_context()`)
   - `add_sink(unique_ptr<Sink>)` → returns `sink_id` / `remove_sink(sink_id)`
   - `create_channel(topic, encoding, schema)` → creates `RawChannel` wired through this Context
   - Optional: `SinkChannelFilterFn` — `std::function<bool(sink_id, channel_id)>` to selectively route channels to specific sinks
4. Concurrency contract:
   - `add_sink()` / `remove_sink()`: callable from any thread; internally serialized via mutex
   - `create_channel()`: callable from any thread
   - Sink callbacks (`on_message`, etc.) are called synchronously from the thread that calls `RawChannel::log()` — sinks must be prepared for concurrent calls
5. Migration: refactor Ch4's example to use `Context` + `WebSocketServerSink` instead of direct server registration
6. Tests: multi-sink routing (two mock sinks receive same message), channel filter (sink A gets channel 1 but not 2), default context behavior, add/remove sink lifecycle

**Official comparison**: `cpp/foxglove/include/foxglove/context.hpp`

**Milestone**: Same channel data simultaneously goes to WebSocket server AND a mock sink. Automated test verifies both sinks receive identical messages.

---

### Chapter 7: MCAP 文件写入
**Tag**: `v0.7-mcap`
**New files**: `mcap.hpp/cpp`, `test_mcap.cpp`, `examples/ch07_mcap/`
**Dependencies introduced**: zstd
**External tools required**: `mcap` CLI (Go binary, `go install github.com/foxglove/mcap/go/cli/mcap@latest`) — used for verification only

**Architecture note**: This chapter is split into two phases to maintain incremental rhythm. Phase 1 produces valid but uncompressed MCAP files. Phase 2 adds chunking, compression, and indexing.

**Content**:

**Phase 1 — Minimal MCAP writer** (tag: `v0.7a-mcap-basic`):
1. MCAP binary format specification (subset):
   - File structure: `Magic → Header → Schema → Channel → Message* → Footer → Magic`
   - Record types: Header (0x01), Schema (0x03), Channel (0x04), Message (0x05), Footer (0x02)
   - CRC32 field left as 0 (no checksum yet)
2. `McapWriterOptions`:
   - File path (required)
3. `McapWriter` class (minimal):
   - `static FoxgloveResult<McapWriter> create(McapWriterOptions)`
   - `write_schema(Schema)` / `write_channel(ChannelDescriptor)` / `write_message(channel_id, data, len, log_time)`
   - `close()`: write Footer, close file
4. Tests:
   - Write header + schema + channel + messages + footer → verify with `mcap info` CLI
   - Golden-file comparison: write known data → compare binary output byte-by-byte against golden artifact
   - Empty file (header + footer only)

**Phase 2 — Chunking, compression, indexing** (tag: `v0.7b-mcap-full`):
5. Chunking: messages accumulate in a Chunk record, flush when chunk size exceeded
6. Compression: zstd compress chunk data before writing
7. Indexing: ChunkIndex (0x08) records for random access, Statistics record
8. CRC32 checksums on Header and Footer
9. `McapWriterSink`: adapter wrapping `McapWriter` as a `Sink` for Context integration
10. Tests:
    - Write compressed MCAP → verify readable by `mcap info` and `mcap cat`
    - Verify chunk boundaries at configured size
    - Round-trip: write → read back statistics via `mcap info --json` → assert message counts match

**Official comparison**: `cpp/foxglove/include/foxglove/mcap.hpp`, [MCAP spec](https://mcap.dev/spec)

**Milestone**: Generate `.mcap` files that Foxglove Studio can open and play back. Golden-file tests pass.

---

### Chapter 8: 内置消息类型
**Tag**: `v0.8-messages`
**New files**: extended `messages.hpp/cpp`, `test_messages.cpp` (extended), `examples/ch08_messages/`

**Content**:
1. Complex message types (building on Chapter 5 base types):
   - `SceneEntity { timestamp, frame_id, id, lifetime, cubes[], spheres[], lines[], ... }`
   - `SceneUpdate { entities[], deletions[] }`
   - `FrameTransform { timestamp, parent_frame_id, child_frame_id, translation, rotation }`
   - `Log { timestamp, level, message, name, file, line }`
   - `CompressedImage { timestamp, frame_id, data, format }`
2. Typed channel helpers:
   - `SceneUpdateChannel`: `RawChannel` pre-configured with `foxglove.SceneUpdate` schema
   - `FrameTransformChannel`, `LogChannel`
3. 3D primitive types:
   - `CubePrimitive { pose, size, color }`
   - `SpherePrimitive { pose, size, color }`
   - `CylinderPrimitive { pose, size, color, bottom_scale, top_scale }`
   - `ArrowPrimitive { pose, shaft_length, shaft_diameter, head_length, head_diameter, color }`
   - `LinePrimitive { type, pose, thickness, points[], colors[] }`
4. Discussion: how official SDK auto-generates `messages.hpp` from `schemas/*.proto`
5. Tests:
   - Roundtrip serialization for each complex type (encode → decode → compare)
   - Golden-file JSON comparison for each message type
   - Integration: publish SceneUpdate via WebSocketServer → scripted WebSocket client receives and validates JSON payload structure
   - Manual smoke test (not automated): open Foxglove Studio, view 3D panel (documented as a manual step in example README)

**Official comparison**: `cpp/foxglove/include/foxglove/messages.hpp`, `schemas/schemas/foxglove/`

**Milestone**: All complex message types pass roundtrip + golden-file tests. Scripted WebSocket client validates published message structure matches Foxglove schema.

---

### Chapter 9: 端到端集成
**Tag**: `v0.9-e2e`
**New files**: `examples/ch09_e2e/`, extended integration tests

**Content**:
1. Complete demo application:
   - Create `WebSocketServer` (live streaming) + `McapWriter` (recording) via `Context`
   - Publish animated 3D scene (rotating cubes, moving spheres)
   - Publish coordinate transforms (world → base → sensor)
   - Publish log messages at different levels
2. Architecture review:
   - How all modules connect: Channel → Context → Sink(Server + McapWriter)
   - Thread model: server event loop thread, user publishing thread(s), writer flush thread
   - Memory management: who owns what, lifetime guarantees
3. Performance discussion:
   - Zero-copy opportunities (message data views)
   - Lock contention analysis
   - Comparison with official SDK's Rust-core performance characteristics
4. Design retrospective:
   - What we gained by going pure C++ (no Rust dependency, full control)
   - What we lost (Rust's safety guarantees, performance of Rust core)
   - Design decisions that diverged from official SDK and why
5. Suggested exercises for the reader:
   - Add Service support (request/response RPC)
   - Add Parameter server
   - Add LZ4 compression option for MCAP
   - Implement a custom message type

**Milestone**: Full working demo — live 3D visualization in Foxglove Studio + recorded MCAP file playable offline.

---

## 6. Third-Party Reference Protocol

The `third-party/foxglove-sdk/` directory serves as the authoritative reference throughout the tutorial.

### Usage Rules

1. **Read-only**: Never modify files in `third-party/`. It is a git submodule pinned to a specific commit.
2. **API & architecture comparison**: Each chapter ends with a "vs Official Implementation" section comparing our public API surface and major architecture choices to the corresponding official source files. Implementation internals (especially Rust async patterns) are noted but not required to match.
3. **Style extraction**: `.clang-format`, naming conventions, comment style, and test patterns are all derived from the official source.
4. **Source of truth for protocol**: When implementing Foxglove WebSocket Protocol or MCAP format, cross-reference against official Rust/C implementations, not just documentation.

### Pinned Version

Pin to a specific commit on main branch (to be determined at project setup). Record the exact commit SHA in `third-party/foxglove-sdk` submodule. Do not use floating tags.

---

## 7. Dependencies

| Library | Version | Purpose | Introduced |
|---------|---------|---------|-----------|
| tl-expected | latest | `FoxgloveResult<T>` (C++23 `std::expected` backport) | Chapter 1 |
| Catch2 | v3.x | Unit testing framework | Chapter 1 |
| nlohmann_json | v3.x | JSON serialization | Chapter 2 |
| libwebsockets | v4.x | WebSocket server | Chapter 4 |
| zstd | v1.x | MCAP chunk compression | Chapter 7 |

All managed via CMake FetchContent. No system-level C++ package installation required.

**External tools** (not C++ dependencies, used for verification only):
- `mcap` CLI (Go binary) — required from Chapter 7 onward for MCAP file verification
- Foxglove Studio — recommended for manual smoke tests from Chapter 4 onward

---

## 8. Git Tag Strategy

Each chapter corresponds to a git tag marking a compilable, testable milestone:

```
v0.1-skeleton       → Chapter 1: Project skeleton + error handling + first test
v0.2-protocol       → Chapter 2: Foxglove protocol encode/decode
v0.3-channel        → Chapter 3: Channel + Schema abstraction
v0.4-server         → Chapter 4: WebSocket server (first connectivity milestone!)
v0.5-serialization  → Chapter 5: Message serialization (JSON)
v0.6-context        → Chapter 6: Context + Sink routing
v0.7a-mcap-basic    → Chapter 7 Phase 1: Minimal MCAP writer (uncompressed)
v0.7b-mcap-full     → Chapter 7 Phase 2: Chunking + compression + indexing
v0.8-messages       → Chapter 8: Built-in Foxglove message types
v0.9-e2e            → Chapter 9: End-to-end integration
```

**Invariant**: At every tag, `cmake --build build && ctest --test-dir build` passes with zero failures.

---

## 9. Resolved Decisions

1. **Language of tutorial docs**: Chinese text with English code/comments (matching muduo book style).
2. **MCAP read support**: Out of scope. Use official `mcap` CLI tool for verification + golden-file binary comparisons.
3. **Serialization format**: JSON + JSON Schema only. Protobuf is a reader exercise (Chapter 9). This is an intentional divergence from the official SDK for educational clarity.
4. **Official comparison scope**: Compare public API surface and major architecture choices, not Rust internals.
5. **Upstream pin**: Exact commit SHA, not floating tag.

---

## 10. Glossary

| Term | Definition |
|------|-----------|
| **Middle-level C++ developer** | Comfortable with C++17 features (structured bindings, `std::optional`, `if constexpr`), CMake, and basic networking concepts. Has built at least one non-trivial C++ project. |
| **Roundtrip test** | `T original → encode(original) → bytes → decode(bytes) → T decoded`, assert `original == decoded` for all fields. |
| **Golden-file test** | Encode known input → compare output bytes against a checked-in reference file. Detect unintentional serialization changes. |
| **Sink** | An output destination that receives channel advertisements and message data. Two implementations: WebSocket server (live) and MCAP writer (file). |
| **Context** | A multiplexer that routes channel data to one or more sinks, with optional per-channel filtering. |
| **SinkChannelFilterFn** | `std::function<bool(uint32_t sink_id, uint32_t channel_id)>` — returns true if the given sink should receive data from the given channel. |

---

## Appendix A: In-Scope Enums and Constants

### Error Codes (`FoxgloveError`)

| Code | Name | Description |
|------|------|-------------|
| 0 | `None` | No error |
| 1 | `InvalidArgument` | Invalid function argument |
| 2 | `ChannelClosed` | Attempted to log on a closed channel |
| 3 | `ServerStartFailed` | WebSocket server failed to start |
| 4 | `IoError` | File I/O failure (MCAP) |
| 5 | `SerializationError` | JSON encode/decode failure |
| 6 | `ProtocolError` | Malformed protocol message |

### Server Capabilities (bitmask)

| Bit | Name | Description |
|-----|------|-------------|
| 0x01 | `ClientPublish` | Allow clients to advertise and publish (out of scope — listed for completeness) |
| 0x02 | `Time` | Server publishes time messages |

(Services, Parameters, ConnectionGraph, Assets, PlaybackControl are out of scope.)

### Supported Encodings

| Encoding | Schema Encoding | Description |
|----------|----------------|-------------|
| `"json"` | `"jsonschema"` | JSON messages with JSON Schema — the only encoding in this tutorial |

### Protocol Opcodes (Binary messages)

| Opcode | Direction | Name |
|--------|-----------|------|
| 0x01 | S→C | `messageData` |
| 0x02 | S→C | `time` |

(Client-to-server binary opcodes are out of scope since `ClientPublish` is not implemented.)

---

## Appendix B: Intentional Divergences from Official SDK

| Area | Official SDK | This Tutorial | Why |
|------|-------------|---------------|-----|
| Core language | Rust + C FFI + C++ wrapper | Pure C++ | Educational: full control, no Rust toolchain required |
| Serialization | Protobuf (generated from .proto) | JSON + JSON Schema | Educational: human-readable, no protoc dependency |
| WebSocket library | Tokio + Tungstenite (Rust) | libwebsockets (C) | Closest C/C++ equivalent; matches official C++ SDK's dependency |
| Async model | Tokio async runtime | Threaded event loop | C++ standard library has no built-in async runtime |
| Code generation | Proc macros + build.rs (Rust) | Hand-written message types | Educational: understand what code generators produce |
| Feature scope | Full (Services, Params, Assets, etc.) | Core subset only | Keeps tutorial focused and achievable |
