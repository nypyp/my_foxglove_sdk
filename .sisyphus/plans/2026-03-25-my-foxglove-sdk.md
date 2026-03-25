# my_foxglove_sdk Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an educational C++ Foxglove SDK from scratch with progressive tutorial documentation, following the design spec at `docs/superpowers/specs/2026-03-25-my-foxglove-sdk-tutorial-design.md`.

**Architecture:** Protocol-first bottom-up approach. Each task corresponds to one chapter (git tag). Chapters build sequentially: error handling → protocol → channel → server → serialization → context → MCAP → messages → e2e. All code uses JSON+JSON Schema serialization, C++17, CMake FetchContent for dependencies, Catch2 for tests.

**Tech Stack:** C++17, CMake 3.20+, libwebsockets, nlohmann_json, Catch2 v3, tl-expected, zstd

**Spec:** `docs/superpowers/specs/2026-03-25-my-foxglove-sdk-tutorial-design.md`

**Reference source (read-only):** `third-party/foxglove-sdk/` — created by Task 0. **Task 0 is a hard prerequisite for all subsequent tasks.** All `third-party/foxglove-sdk/` file references below are valid only after Task 0 completes.

---

## Pre-Implementation Setup

### Task 0: Repository Bootstrap + Reference Source

**Prerequisite:** This task assumes the working directory is a Git repository. If not, run `git init && git commit --allow-empty -m "chore: initialize repository"` first.

**Files:**
- Create: `.gitmodules`
- Create: `.gitignore`

- [ ] **Step 0: Ensure Git repository exists**

```bash
git rev-parse --is-inside-work-tree || (git init && git commit --allow-empty -m "chore: initialize repository")
```

- [ ] **Step 1: Add foxglove-sdk as git submodule**

```bash
git submodule add https://github.com/foxglove/foxglove-sdk.git third-party/foxglove-sdk
```

- [ ] **Step 2: Pin to a specific commit**

```bash
cd third-party/foxglove-sdk && git checkout main && git log -1 --format="%H" && cd ../..
```

Record the commit SHA. This becomes the fixed reference for the entire tutorial.

- [ ] **Step 3: Create .gitignore**

```gitignore
build/
.cache/
compile_commands.json
*.mcap
```

- [ ] **Step 4: QA — verify submodule and .gitignore**

Run: `ls third-party/foxglove-sdk/README.md`
Expected: File exists (submodule cloned successfully).
Run: `git submodule status`
Expected: Shows pinned commit SHA for `third-party/foxglove-sdk`.
Run: `cat .gitignore`
Expected: Contains `build/`, `.cache/`, `compile_commands.json`, `*.mcap`.

- [ ] **Step 5: Commit**

```bash
git add .gitmodules .gitignore third-party/
git commit -m "chore: add foxglove-sdk as reference submodule, add .gitignore"
```

---

## Part 1: Foundation (Chapters 0–3)

### Task 1: Chapter 0 — Tutorial Documentation (前言与项目概览)

**Files:**
- Create: `docs/00-前言与项目概览.md`

- [ ] **Step 1: Write Chapter 0 documentation**

Write `docs/00-前言与项目概览.md` covering:
- Foxglove ecosystem introduction (Studio, MCAP, WebSocket protocol)
- What this tutorial builds (pure C++ SDK core subset)
- Relationship to official SDK (our pure C++ vs their Rust+C FFI+C++ wrapper)
- Development environment requirements (C++17 compiler, CMake 3.20+, Foxglove Studio)
- How to use `third-party/foxglove-sdk/` as reference
- Tutorial progression overview (Ch1–Ch9)

**Tone**: Chinese text, English code/comments. Educational, like 陈硕's muduo book.

**Reference**: Read `third-party/foxglove-sdk/README.md` and `third-party/foxglove-sdk/cpp/README.md` for official project description.

- [ ] **Step 2: QA — verify doc completeness**

Run: `grep -c "##" docs/00-前言与项目概览.md`
Expected: At least 6 sections (one per bullet in Step 1).
Run: `wc -l docs/00-前言与项目概览.md`
Expected: At least 100 lines of substantive content.
Run: `grep -l "third-party/foxglove-sdk" docs/00-前言与项目概览.md`
Expected: File matches (doc references the submodule).

- [ ] **Step 3: Commit**

```bash
git add docs/00-前言与项目概览.md
git commit -m "docs: add Chapter 0 — tutorial overview and Foxglove ecosystem introduction"
```

---

### Task 2: Chapter 1 — Project Skeleton + Error Handling

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/dependencies.cmake`
- Create: `.clang-format`
- Create: `include/foxglove/error.hpp`
- Create: `src/error.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_error.cpp`
- Create: `examples/ch01_skeleton/CMakeLists.txt`
- Create: `examples/ch01_skeleton/main.cpp`
- Create: `docs/01-项目骨架与代码规范.md`

**Reference files to read first:**
- `third-party/foxglove-sdk/cpp/CMakeLists.txt` — build structure
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/error.hpp` — error type design
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/expected.hpp` — expected wrapper

- [ ] **Step 1: Extract .clang-format from official SDK**

Read `third-party/foxglove-sdk/cpp/` for any `.clang-format` or style configuration. If none exists, create one matching the naming conventions in the spec (Section 4.1). Must enforce:
- `ColumnLimit: 100`
- `IndentWidth: 2`
- `NamespaceIndentation: None`
- `BreakBeforeBraces: Attach`

Create `.clang-format` at project root.

- [ ] **Step 2: Write cmake/dependencies.cmake**

```cmake
include(FetchContent)

FetchContent_Declare(
  tl-expected
  GIT_REPOSITORY https://github.com/TartanLlama/expected.git
  GIT_TAG        v1.1.0
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)

FetchContent_MakeAvailable(tl-expected Catch2)
```

- [ ] **Step 3: Write top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_foxglove_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

# Library
add_library(foxglove
  src/error.cpp
)
target_include_directories(foxglove PUBLIC include)
target_link_libraries(foxglove PUBLIC tl::expected)

# Tests
enable_testing()
add_subdirectory(tests)

# Examples
add_subdirectory(examples/ch01_skeleton)
```

- [ ] **Step 4: Write the failing test for FoxgloveResult**

Create `tests/CMakeLists.txt`:
```cmake
add_executable(test_error test_error.cpp)
target_link_libraries(test_error PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_error COMMAND test_error)
```

Create `tests/test_error.cpp`:
```cpp
/// @brief Unit tests for FoxgloveResult<T> error handling.

#include <foxglove/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("FoxgloveError - enum values match spec") {
  SECTION("None is zero") {
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::None) == 0);
  }
  SECTION("all codes defined") {
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::InvalidArgument) == 1);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::ChannelClosed) == 2);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::ServerStartFailed) == 3);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::IoError) == 4);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::SerializationError) == 5);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::ProtocolError) == 6);
  }
}

TEST_CASE("FoxgloveResult - success construction") {
  foxglove::FoxgloveResult<int> result(42);
  REQUIRE(result.has_value());
  REQUIRE(result.value() == 42);
}

TEST_CASE("FoxgloveResult - error construction") {
  foxglove::FoxgloveResult<int> result(
      tl::make_unexpected(foxglove::FoxgloveError::InvalidArgument));
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == foxglove::FoxgloveError::InvalidArgument);
}

TEST_CASE("FoxgloveResult - string type") {
  foxglove::FoxgloveResult<std::string> result(std::string("hello"));
  REQUIRE(result.has_value());
  REQUIRE(result.value() == "hello");
}

TEST_CASE("FOXGLOVE_TRY - propagates error") {
  auto failing_fn = []() -> foxglove::FoxgloveResult<int> {
    return tl::make_unexpected(foxglove::FoxgloveError::IoError);
  };

  auto caller = [&]() -> foxglove::FoxgloveResult<std::string> {
    FOXGLOVE_TRY(val, failing_fn());
    (void)val;
    return std::string("should not reach");
  };

  auto result = caller();
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == foxglove::FoxgloveError::IoError);
}

TEST_CASE("FOXGLOVE_TRY - passes through on success") {
  auto success_fn = []() -> foxglove::FoxgloveResult<int> { return 42; };

  auto caller = [&]() -> foxglove::FoxgloveResult<int> {
    FOXGLOVE_TRY(val, success_fn());
    return val * 2;
  };

  auto result = caller();
  REQUIRE(result.has_value());
  REQUIRE(result.value() == 84);
}

TEST_CASE("foxglove_error_string - returns description") {
  REQUIRE(foxglove::foxglove_error_string(foxglove::FoxgloveError::None) ==
          "no error");
  REQUIRE(foxglove::foxglove_error_string(
              foxglove::FoxgloveError::InvalidArgument) ==
          "invalid argument");
}
```

- [ ] **Step 5: Run test to verify it fails**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build 2>&1
```

Expected: Compilation fails — `foxglove/error.hpp` does not exist yet.

- [ ] **Step 6: Implement error.hpp**

Create `include/foxglove/error.hpp`:
```cpp
/// @brief Error handling for the foxglove SDK.
///
/// Provides FoxgloveError enum, FoxgloveResult<T> type alias,
/// and FOXGLOVE_TRY macro for ergonomic error propagation.

#pragma once

#include <tl/expected.hpp>

#include <string>

namespace foxglove {

/// @brief Error codes for foxglove SDK operations.
///
/// Values match the error code table in the design spec (Appendix A).
enum class FoxgloveError {
  None = 0,
  InvalidArgument = 1,
  ChannelClosed = 2,
  ServerStartFailed = 3,
  IoError = 4,
  SerializationError = 5,
  ProtocolError = 6,
};

/// @brief Result type for foxglove SDK operations.
///
/// Uses tl::expected as a C++17 backport of std::expected.
/// On success, holds a value of type T. On failure, holds a FoxgloveError.
template <typename T>
using FoxgloveResult = tl::expected<T, FoxgloveError>;

/// @brief Convert a FoxgloveError to a human-readable string.
///
/// @param error The error code to describe.
/// @return A string describing the error.
std::string foxglove_error_string(FoxgloveError error);

}  // namespace foxglove

/// @brief Early-return macro for error propagation.
///
/// Evaluates `expr` (which must return a FoxgloveResult). If it holds an error,
/// immediately returns that error from the enclosing function. Otherwise, binds
/// the success value to `var`.
///
/// @code
/// auto result = some_operation();
/// FOXGLOVE_TRY(value, result);
/// // use value...
/// @endcode
#define FOXGLOVE_TRY(var, expr)                                      \
  auto _foxglove_try_##var = (expr);                                 \
  if (!_foxglove_try_##var.has_value()) {                            \
    return tl::make_unexpected(_foxglove_try_##var.error());         \
  }                                                                  \
  auto var = std::move(_foxglove_try_##var.value())
```

- [ ] **Step 7: Implement error.cpp**

Create `src/error.cpp`:
```cpp
/// @brief Implementation of error handling utilities.

#include <foxglove/error.hpp>

namespace foxglove {

std::string foxglove_error_string(FoxgloveError error) {
  switch (error) {
    case FoxgloveError::None:
      return "no error";
    case FoxgloveError::InvalidArgument:
      return "invalid argument";
    case FoxgloveError::ChannelClosed:
      return "channel closed";
    case FoxgloveError::ServerStartFailed:
      return "server start failed";
    case FoxgloveError::IoError:
      return "I/O error";
    case FoxgloveError::SerializationError:
      return "serialization error";
    case FoxgloveError::ProtocolError:
      return "protocol error";
  }
  return "unknown error";
}

}  // namespace foxglove
```

- [ ] **Step 8: Create ch01_skeleton example**

Create `examples/ch01_skeleton/CMakeLists.txt`:
```cmake
add_executable(ch01_skeleton main.cpp)
target_link_libraries(ch01_skeleton PRIVATE foxglove)
```

Create `examples/ch01_skeleton/main.cpp`:
```cpp
/// @brief Chapter 1 example: demonstrate FoxgloveResult usage.

#include <foxglove/error.hpp>

#include <cstdio>

foxglove::FoxgloveResult<int> divide(int a, int b) {
  if (b == 0) {
    return tl::make_unexpected(foxglove::FoxgloveError::InvalidArgument);
  }
  return a / b;
}

foxglove::FoxgloveResult<int> chain_example() {
  FOXGLOVE_TRY(result, divide(10, 2));
  FOXGLOVE_TRY(result2, divide(result, 0));  // This will fail
  return result2;
}

int main() {
  auto r1 = divide(10, 2);
  if (r1.has_value()) {
    std::printf("10 / 2 = %d\n", r1.value());
  }

  auto r2 = chain_example();
  if (!r2.has_value()) {
    std::printf("Chain failed: %s\n",
                foxglove::foxglove_error_string(r2.error()).c_str());
  }

  return 0;
}
```

- [ ] **Step 9: Build and run tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All tests pass. Example runs and prints output.

- [ ] **Step 10: Write Chapter 1 tutorial document**

Create `docs/01-项目骨架与代码规范.md` covering:
- CMake project setup with FetchContent
- Why tl::expected over exceptions (reference official SDK's design)
- FoxgloveError enum design (reference Appendix A)
- FoxgloveResult<T> type alias pattern
- FOXGLOVE_TRY macro — why and how
- .clang-format and code style rules (reference Section 4 of spec)
- Testing with Catch2 — structure, naming conventions
- **vs Official Implementation**: compare `error.hpp` with `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/error.hpp`

Chinese text, English code/comments.

- [ ] **Step 11: Tag and commit**

```bash
git add -A
git commit -m "feat(ch01): project skeleton with error handling, tests, and tutorial doc"
git tag v0.1-skeleton
```

---

### Task 3: Chapter 2 — Foxglove WebSocket Protocol

**Files:**
- Create: `include/foxglove/protocol.hpp`
- Create: `src/protocol.cpp`
- Create: `tests/test_protocol.cpp`
- Create: `examples/ch02_protocol/CMakeLists.txt`
- Create: `examples/ch02_protocol/main.cpp`
- Create: `docs/02-Foxglove协议解析.md`
- Modify: `CMakeLists.txt` — add nlohmann_json dep, protocol source
- Modify: `cmake/dependencies.cmake` — add nlohmann_json
- Modify: `tests/CMakeLists.txt` — add test_protocol

**Reference files to read first:**
- `third-party/foxglove-sdk/rust/foxglove/src/websocket/` — Rust protocol implementation
- https://github.com/foxglove/ws-protocol/blob/main/docs/spec.md — protocol spec
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/server.hpp` — for C++ data structures

- [ ] **Step 1: Add nlohmann_json to dependencies.cmake**

Append to `cmake/dependencies.cmake`:
```cmake
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(tl-expected Catch2 nlohmann_json)
```

- [ ] **Step 2: Write failing tests for protocol encode/decode**

Create `tests/test_protocol.cpp` with TEST_CASEs for:
- `"Protocol - serverInfo encodes to JSON"`: construct ServerInfo → encode → verify JSON fields
- `"Protocol - advertise encodes to JSON"`: construct ChannelAdvertisement list → encode → verify JSON
- `"Protocol - subscribe decodes from JSON"`: parse Subscribe JSON → verify fields
- `"Protocol - unsubscribe decodes from JSON"`: parse Unsubscribe JSON → verify fields
- `"Protocol - messageData binary roundtrip"`: construct MessageData → encode binary → decode → compare
- `"Protocol - malformed JSON returns error"`: invalid JSON → decode returns ProtocolError
- `"Protocol - malformed binary returns error"`: truncated binary → decode returns ProtocolError

Each test must use concrete values, not placeholders.

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake -B build && cmake --build build 2>&1
```

Expected: Fails — `foxglove/protocol.hpp` doesn't exist.

- [ ] **Step 4: Implement protocol.hpp**

Create `include/foxglove/protocol.hpp` with:
- `ServerInfo` struct: `name`, `capabilities` (uint32), `supported_encodings` (vector<string>), `metadata` (map<string,string>), `session_id` (string)
- `ChannelAdvertisement` struct: `id` (uint32), `topic`, `encoding`, `schema_name`, `schema_encoding`, `schema_data`
- `Subscription` struct: `subscription_id` (uint32), `channel_id` (uint32)
- `ClientMessage` variant: `Subscribe { subscriptions: vector<Subscription> }`, `Unsubscribe { subscription_ids: vector<uint32> }`
- Binary frame `MessageData`: `subscription_id` (uint32), `log_time` (uint64), `data` (vector<uint8_t>)
- Encode functions: `encode_server_info(...)`, `encode_advertise(...)`, `encode_message_data(...)`
- Decode functions: `decode_client_message(...)`, `decode_message_data_binary(...)`
- All return `FoxgloveResult<T>` on decode

Must follow spec naming conventions and doc comment style.

- [ ] **Step 5: Implement protocol.cpp**

Implement JSON encode/decode using nlohmann_json. Binary encode/decode for messageData using little-endian byte layout:
- Binary messageData frame: `uint8_t opcode(0x01) | uint32_t subscriptionId | uint64_t logTime | payload_bytes`

- [ ] **Step 6: Update CMakeLists.txt**

Add `src/protocol.cpp` to library sources. Add `nlohmann_json::nlohmann_json` to link libraries.

- [ ] **Step 7: Update tests/CMakeLists.txt**

Add `test_protocol` executable and test.

- [ ] **Step 8: Build and run all tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All protocol tests pass + all previous tests still pass.

- [ ] **Step 9: Create ch02_protocol example**

Create example that:
1. Constructs a `ServerInfo` and prints its JSON encoding
2. Constructs `ChannelAdvertisement` list and prints JSON
3. Encodes/decodes a `MessageData` binary frame and prints hex dump

- [ ] **Step 10: Write Chapter 2 tutorial document**

Create `docs/02-Foxglove协议解析.md` covering:
- Foxglove WebSocket Protocol v1 overview (reference ws-protocol spec)
- Server→Client vs Client→Server message types
- JSON message format for text frames
- Binary message format for messageData
- Why little-endian byte order
- **vs Official Implementation**: compare with `third-party/foxglove-sdk/rust/foxglove/src/websocket/`

- [ ] **Step 11: Tag and commit**

```bash
git add -A
git commit -m "feat(ch02): Foxglove WebSocket protocol encode/decode with tests"
git tag v0.2-protocol
```

---

### Task 4: Chapter 3 — Channel & Schema Abstraction

**Files:**
- Create: `include/foxglove/schema.hpp`
- Create: `include/foxglove/channel.hpp`
- Create: `src/channel.cpp`
- Create: `tests/test_channel.cpp`
- Create: `examples/ch03_channel/CMakeLists.txt`
- Create: `examples/ch03_channel/main.cpp`
- Create: `docs/03-Channel与Schema抽象.md`
- Modify: `CMakeLists.txt` — add channel source
- Modify: `tests/CMakeLists.txt` — add test_channel

**Reference files to read first:**
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/channel.hpp`
- `third-party/foxglove-sdk/cpp/foxglove/src/channel.cpp`

- [ ] **Step 1: Write failing tests for Channel**

Create `tests/test_channel.cpp` with TEST_CASEs for:
- `"Channel - assigns unique sequential IDs"`: create 3 channels → IDs are 1, 2, 3
- `"Channel - log invokes callback with correct data"`: create channel with mock callback → log → callback receives data
- `"Channel - close prevents further logging"`: create → close → log returns no-op (no callback)
- `"Channel - concurrent log from multiple threads"`: 4 threads each log 100 messages → all messages delivered, no crashes
- `"Channel - close during log is safe"`: one thread logs in a loop, another closes → no crash, no UB
- `"Channel - set_callback rebinds output"`: create channel with callback A → set_callback(B) → log → B receives, not A
- `"Channel - set_callback to nullptr silences output"`: create → set_callback(nullptr) → log → no crash, no delivery
- `"Schema - construction"`: create Schema with name, encoding, data → fields accessible

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake -B build && cmake --build build 2>&1
```

- [ ] **Step 3: Implement schema.hpp**

Create `include/foxglove/schema.hpp`:
- `Schema` struct with `name` (string), `encoding` (string), `data` (vector<uint8_t>)
- `ChannelDescriptor` struct with `id` (uint32), `topic` (string), `encoding` (string), `schema` (Schema)

- [ ] **Step 4: Implement channel.hpp and channel.cpp**

`include/foxglove/channel.hpp`:
- `MessageCallback` = `std::function<void(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time)>`
- `RawChannel` class:
  - Private: `uint32_t id_`, `ChannelDescriptor descriptor_`, `MessageCallback callback_`, `std::mutex mutex_`, `bool closed_`
  - Public static: `FoxgloveResult<RawChannel> create(string topic, string encoding, Schema schema, MessageCallback callback = nullptr)`
  - Public: `uint32_t id() const`, `const ChannelDescriptor& descriptor() const`, `void log(const uint8_t* data, size_t len, uint64_t log_time)`, `void close()`
  - Public: `void set_callback(MessageCallback callback)` — **rebinding API**: allows the server/context to wire its dispatch callback after channel construction. Thread-safe (takes mutex). This is the integration point used by `WebSocketServer::add_channel()` and `Context::create_channel()`.
  - Move-only (delete copy)

`src/channel.cpp`:
- Static `std::atomic<uint32_t> next_channel_id_{1}` for unique IDs
- `create()`: allocate ID atomically, construct channel
- `log()`: lock mutex, if not closed → invoke callback
- `close()`: lock mutex, set closed=true

- [ ] **Step 5: Update CMakeLists.txt and tests/CMakeLists.txt**

Add `src/channel.cpp` to library. Add `test_channel` test.

- [ ] **Step 6: Build and run all tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 7: Create ch03_channel example**

Example that creates a channel with a printf callback, logs some messages, then closes.

- [ ] **Step 8: Write Chapter 3 tutorial document**

Create `docs/03-Channel与Schema抽象.md` covering:
- Schema as data descriptor
- Channel lifecycle: create → log → close
- Callback-based output (why not direct server coupling)
- Thread safety design (mutex, atomic ID)
- Concurrency contracts
- **vs Official**: compare with `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/channel.hpp`

- [ ] **Step 9: Tag and commit**

```bash
git add -A
git commit -m "feat(ch03): Channel and Schema abstraction with thread-safe logging"
git tag v0.3-channel
```

---

## Part 2: Core Features (Chapters 4–6)

### Task 5: Chapter 4 — WebSocket Server

**Files:**
- Create: `include/foxglove/server.hpp`
- Create: `src/server.cpp`
- Create: `tests/test_server.cpp`
- Create: `examples/ch04_server/CMakeLists.txt`
- Create: `examples/ch04_server/main.cpp`
- Create: `docs/04-WebSocket服务器.md`
- Modify: `CMakeLists.txt` — add libwebsockets dep, server source
- Modify: `cmake/dependencies.cmake` — add libwebsockets
- Modify: `tests/CMakeLists.txt` — add test_server

**Reference files to read first:**
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/server.hpp`
- `third-party/foxglove-sdk/cpp/foxglove/src/server.cpp`
- `third-party/foxglove-sdk/cpp/examples/ws-server/` — example usage

- [ ] **Step 1: Add libwebsockets to dependencies**

Add to `cmake/dependencies.cmake`. Note: libwebsockets via FetchContent can be complex. May need `find_package(libwebsockets)` as fallback or use a simpler approach. Research the actual build integration from the official SDK's CMakeLists.txt.

- [ ] **Step 2: Write failing tests for WebSocket server**

Create `tests/test_server.cpp` with:
- `"Server - creates and starts" [integration]`: create server → verify it's listening on port
- `"Server - sends serverInfo on connect" [integration]`: connect with a raw WebSocket client → receive JSON → parse → verify fields
- `"Server - advertises channels" [integration]`: add_channel → connect client → verify advertise message
- `"Server - delivers messageData to subscribers" [integration]`: connect → subscribe → channel.log() → verify binary frame
- `"Server - graceful shutdown" [integration]`: create → connect client → shutdown → no crash

Use a test helper that creates a temporary libwebsockets client for assertions. Tag all with `[integration]`.

- [ ] **Step 3: Implement server.hpp**

Create `include/foxglove/server.hpp`:
- `WebSocketServerOptions`: `host`, `port`, `name`, `capabilities`
- `WebSocketServerCallbacks`: `on_subscribe`, `on_unsubscribe` functors
- `WebSocketServer` class (PIMPL):
  - `static FoxgloveResult<WebSocketServer> create(WebSocketServerOptions)`
  - `void add_channel(RawChannel&)` — stores a pointer to the channel, calls `channel.set_callback(...)` to wire channel output to server dispatch, then advertises channel to connected clients. The server maintains a `std::unordered_map<uint32_t, RawChannel*>` mapping channel IDs to channel pointers.
  - `void remove_channel(uint32_t channel_id)` — looks up channel pointer in the map, calls `channel->set_callback(nullptr)` to unwire, removes from map, then unadvertises to clients
  - `void broadcast_time(uint64_t timestamp)` — send time message
  - `void shutdown()` — graceful stop
  - Move-only

- [ ] **Step 4: Implement server.cpp**

Key implementation details:
- Create `lws_context` with foxglove protocol
- Run `lws_service()` in a dedicated `std::thread`
- On client connect: send `serverInfo` JSON
- On client text message: parse as `Subscribe`/`Unsubscribe`, update subscription map
- `add_channel()`: store channel info, broadcast `advertise` to all clients
- MessageCallback wired to channels: on `log()` → find subscribers → build binary `messageData` frame → `lws_write()`
- Thread-safe: mutex for subscription map and channel list

- [ ] **Step 5: Update build files and run tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Create ch04_server example**

Example: start server, create a channel, publish incrementing counter messages every 100ms. Print "Server running on ws://localhost:8765". User can connect with Foxglove Studio.

- [ ] **Step 7: Write Chapter 4 tutorial document**

Create `docs/04-WebSocket服务器.md` covering:
- libwebsockets architecture (event loop, protocols, callbacks)
- Subprotocol negotiation
- Server lifecycle and threading model
- Channel registration (explicit, not auto-wired)
- Subscription management
- Binary frame construction
- **vs Official**: compare with official server.hpp/cpp

- [ ] **Step 8: Tag and commit**

```bash
git add -A
git commit -m "feat(ch04): WebSocket server with channel management and subscription tracking"
git tag v0.4-server
```

---

### Task 6: Chapter 5 — Message Serialization

**Files:**
- Create: `include/foxglove/messages.hpp`
- Create: `src/messages.cpp`
- Create: `tests/test_messages.cpp`
- Create: `tests/golden/` (golden JSON files for each type)
- Create: `examples/ch05_serialization/CMakeLists.txt`
- Create: `examples/ch05_serialization/main.cpp`
- Create: `docs/05-消息序列化.md`
- Modify: `CMakeLists.txt` — add messages source
- Modify: `tests/CMakeLists.txt` — add test_messages

**Reference files to read first:**
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/messages.hpp`
- `third-party/foxglove-sdk/schemas/schemas/foxglove/` — canonical Foxglove schemas

- [ ] **Step 1: Write failing tests for base message types**

Create `tests/test_messages.cpp` with roundtrip + golden-file tests for: Timestamp, Duration, Vector3, Quaternion, Pose, Color.

Create golden files in `tests/golden/`:
- `timestamp.json`: `{"sec":1234567890,"nsec":123456789}`
- `vector3.json`: `{"x":1.0,"y":2.0,"z":3.0}`
- `quaternion.json`: `{"w":1.0,"x":0.0,"y":0.0,"z":0.0}`
- `pose.json`: full Pose with known values
- `color.json`: `{"a":1.0,"b":0.0,"g":1.0,"r":1.0}` (alphabetical)

- [ ] **Step 2: Implement messages.hpp and messages.cpp**

Implement base types with:
- Struct definitions
- `to_json`/`from_json` ADL functions
- `static nlohmann::json json_schema()` method per type
- `encode<T>()` / `decode<T>()` free functions
- NaN → null policy, alphabetical field ordering

- [ ] **Step 3: Build and run tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All message serialization tests pass (roundtrip + golden-file comparison). All previous tests (error, protocol, channel) still pass. Zero test failures.

- [ ] **Step 4: QA — verify golden files and example**

Run: `ls tests/golden/*.json | wc -l`
Expected: At least 5 golden JSON files (timestamp, vector3, quaternion, pose, color).
Run: `./build/examples/ch05_serialization/ch05_serialization`
Expected: Prints JSON representations of each type. No crashes.
Run: `ctest --test-dir build -R test_messages --output-on-failure`
Expected: All message tests pass independently.

- [ ] **Step 5: Create ch05_serialization example**

Example: construct various message types, encode to JSON, print, decode back, verify equality.

- [ ] **Step 6: Write Chapter 5 tutorial document**

Create `docs/05-消息序列化.md` covering: JSON serialization strategy, ADL to_json/from_json pattern, golden-file testing methodology, NaN→null policy, alphabetical field ordering, comparison with official messages.hpp.

- [ ] **Step 7: Tag and commit**

```bash
git add -A
git commit -m "feat(ch05): JSON message serialization with base types and golden-file tests"
git tag v0.5-serialization
```

---

### Task 7: Chapter 6 — Context & Sink Routing

**Files:**
- Create: `include/foxglove/context.hpp`
- Create: `src/context.cpp`
- Create: `tests/test_context.cpp`
- Create: `examples/ch06_context/CMakeLists.txt`
- Create: `examples/ch06_context/main.cpp`
- Create: `docs/06-Context与Sink路由.md`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Reference files:**
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/context.hpp`

- [ ] **Step 1: Write failing tests for Context and Sink**

Tests for:
- `"Context - routes message to single sink"`: add mock sink → create channel → log → mock receives
- `"Context - routes to multiple sinks"`: two mock sinks → both receive same data
- `"Context - channel filter"`: filter fn blocks sink B from channel 1 → only sink A receives
- `"Context - add/remove sink lifecycle"`: add → remove → log → removed sink does not receive
- `"Context - default context singleton"`: `default_context()` returns same instance

- [ ] **Step 2: Implement context.hpp — Sink interface + Context class**

- [ ] **Step 3: Implement context.cpp**

- [ ] **Step 4: Create WebSocketServerSink adapter**

Wrap WebSocketServer as a Sink (in server.hpp/cpp or a new file).

- [ ] **Step 5: Refactor ch04 example to use Context**

- [ ] **Step 6: Build and run all tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All context/sink tests pass. All previous tests (error, protocol, channel, server, messages) still pass. Zero failures.

- [ ] **Step 7: QA — verify Context routing and Sink integration**

Run: `ctest --test-dir build -R test_context --output-on-failure`
Expected: All context tests pass independently.
Run: `./build/examples/ch06_context/ch06_context`
Expected: Prints messages routed through context to mock sinks. No crashes.
Verify: `grep -r "class.*Sink" include/foxglove/` shows Sink interface defined.
Verify: `grep -r "WebSocketServerSink" include/ src/` shows adapter exists.

- [ ] **Step 8: Create ch06_context example + Write tutorial doc**

Create `docs/06-Context与Sink路由.md` covering: Sink interface design (why abstract), Context as multiplexer, WebSocketServerSink adapter pattern, channel filter mechanism, default_context() singleton, comparison with official context.hpp.

- [ ] **Step 9: Tag and commit**

```bash
git add -A
git commit -m "feat(ch06): Context and Sink routing with WebSocketServerSink adapter"
git tag v0.6-context
```

---

## Part 3: Advanced Features (Chapters 7–9)

### Task 8: Chapter 7 Phase 1 — Minimal MCAP Writer

**Files:**
- Create: `include/foxglove/mcap.hpp`
- Create: `src/mcap.cpp`
- Create: `tests/test_mcap.cpp`
- Create: `tests/golden/minimal.mcap` (golden binary file)
- Create: `examples/ch07_mcap/CMakeLists.txt`
- Create: `examples/ch07_mcap/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Reference files:**
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/mcap.hpp`
- https://mcap.dev/spec — MCAP binary format specification

- [ ] **Step 1: Write failing tests for minimal MCAP**

Tests for:
- `"McapWriter - writes valid header and footer"`: create → close → verify magic bytes at start/end
- `"McapWriter - writes schema and channel records"`: write schema + channel → verify record opcodes in output
- `"McapWriter - writes messages"`: write messages → golden-file comparison
- `"McapWriter - empty file is valid"`: create → close → file starts with magic, ends with magic

- [ ] **Step 2: Implement mcap.hpp — McapWriter minimal API**

- [ ] **Step 3: Implement mcap.cpp — binary MCAP writing**

Implement record writing: Magic (8 bytes: `0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'`), Header, Schema, Channel, Message, Footer records. Each record: `uint8_t opcode | uint64_t length | payload`.

- [ ] **Step 4: Build, run tests, verify MCAP output**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All MCAP tests pass. All previous tests still pass. Zero failures.

QA — verify MCAP output:
Run: `ctest --test-dir build -R test_mcap --output-on-failure`
Expected: All MCAP tests pass independently.
Run: `./build/examples/ch07_mcap/ch07_mcap` (creates a test .mcap file)
Expected: File created, no crashes.
Run: `xxd build/test_output.mcap | head -1` (or wherever the test writes)
Expected: First bytes are `89 4d 43 41 50 30 0d 0a` (MCAP magic).
Note: `mcap` CLI is NOT a project dependency. All MCAP validation is done programmatically in tests.

- [ ] **Step 5: Tag**

```bash
git add -A
git commit -m "feat(ch07a): minimal MCAP writer — uncompressed, no chunking"
git tag v0.7a-mcap-basic
```

---

### Task 9: Chapter 7 Phase 2 — MCAP Chunking + Compression

**Files:**
- Modify: `include/foxglove/mcap.hpp` — add options, chunking, compression
- Modify: `src/mcap.cpp` — chunk accumulation, zstd compression, indexing
- Modify: `tests/test_mcap.cpp` — additional tests
- Create: `docs/07-MCAP文件写入.md`
- Modify: `cmake/dependencies.cmake` — add zstd

- [ ] **Step 1: Add zstd to dependencies**

- [ ] **Step 2: Write tests for chunking and compression**

Tests for:
- `"McapWriter - chunked output contains Chunk records"`: enable chunking → write messages → verify Chunk opcode present
- `"McapWriter - zstd compression reduces size"`: write same data compressed vs uncompressed → compressed is smaller
- `"McapWriter - chunk index records present"`: enable chunking → verify ChunkIndex records in summary section
- `"McapWriter - CRC32 validation"`: compute CRC32 of chunk data → matches stored CRC

- [ ] **Step 3: Implement chunking, compression, indexing, CRC32**

- [ ] **Step 4: Implement McapWriterSink for Context integration**

- [ ] **Step 5: Build and run all tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All MCAP tests pass (both minimal and chunked/compressed). All previous tests still pass. Zero failures.

QA — verify compression and Context integration:
Run: `ctest --test-dir build -R test_mcap --output-on-failure`
Expected: All MCAP tests pass including new chunking/compression tests.
Run: `./build/examples/ch07_mcap/ch07_mcap`
Expected: Produces .mcap file. File size with compression < file size without (verified in tests).
Note: `mcap` CLI is NOT a project dependency. All MCAP validation is done programmatically in tests.

- [ ] **Step 6: Create example + Write Chapter 7 tutorial document**

Create `docs/07-MCAP文件写入.md` covering: MCAP binary format overview, record types, minimal writer (Phase 1 recap), chunking strategy, zstd compression integration, CRC32 for data integrity, index records for seekability, McapWriterSink as Context adapter, comparison with official mcap.hpp.

Cover both phases: minimal writer → full writer with chunking.

- [ ] **Step 7: Tag**

```bash
git add -A
git commit -m "feat(ch07b): MCAP chunking, zstd compression, indexing, and Context integration"
git tag v0.7b-mcap-full
```

---

### Task 10: Chapter 8 — Built-in Message Types

**Files:**
- Modify: `include/foxglove/messages.hpp` — add complex types
- Modify: `src/messages.cpp` — add serialization
- Modify: `tests/test_messages.cpp` — add complex type tests
- Create: `tests/golden/scene_update.json`, `tests/golden/frame_transform.json`, etc.
- Create: `examples/ch08_messages/CMakeLists.txt`
- Create: `examples/ch08_messages/main.cpp`
- Create: `docs/08-内置消息类型.md`

**Reference files:**
- `third-party/foxglove-sdk/cpp/foxglove/include/foxglove/messages.hpp`
- `third-party/foxglove-sdk/schemas/schemas/foxglove/SceneUpdate.proto`
- `third-party/foxglove-sdk/schemas/schemas/foxglove/FrameTransform.proto`

- [ ] **Step 1: Write tests for complex message types**

Roundtrip + golden-file for: SceneEntity, SceneUpdate, FrameTransform, Log, CompressedImage, CubePrimitive, SpherePrimitive, CylinderPrimitive, ArrowPrimitive, LinePrimitive.

- [ ] **Step 2: Implement complex types in messages.hpp/cpp**

- [ ] **Step 3: Implement typed channel helpers (SceneUpdateChannel, etc.)**

- [ ] **Step 4: Integration test: publish via WebSocket, scripted client validates**

Write a test tagged `[integration]` that: starts server with a SceneUpdateChannel → connects a scripted WebSocket client → subscribes → publishes a SceneUpdate → client receives binary frame → decodes and verifies SceneUpdate JSON.

- [ ] **Step 5: Build and run all tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: All tests pass including new complex type tests and integration test. Zero failures.

QA — verify golden files and typed channels:
Run: `ls tests/golden/scene_update.json tests/golden/frame_transform.json`
Expected: Both golden files exist.
Run: `ctest --test-dir build -R test_messages --output-on-failure`
Expected: All message tests pass (base types + complex types).
Run: `./build/examples/ch08_messages/ch08_messages`
Expected: Prints JSON for SceneUpdate, FrameTransform, etc. No crashes.

- [ ] **Step 6: Create example + Write tutorial doc**

Create `docs/08-内置消息类型.md` covering: Foxglove schema ecosystem, complex type composition (SceneEntity contains primitives), typed channel helpers (SceneUpdateChannel wrapping RawChannel), JSON Schema generation, golden-file testing for complex structures, comparison with official messages.hpp and .proto schemas.

- [ ] **Step 7: Tag**

```bash
git add -A
git commit -m "feat(ch08): built-in Foxglove message types with typed channels"
git tag v0.8-messages
```

---

### Task 11: Chapter 9 — End-to-End Integration

**Files:**
- Create: `examples/ch09_e2e/CMakeLists.txt`
- Create: `examples/ch09_e2e/main.cpp`
- Create: `docs/09-端到端连接Foxglove-Studio.md`

**Reference files:**
- `third-party/foxglove-sdk/cpp/examples/quickstart/`

- [ ] **Step 1: Create E2E demo application**

Application that:
1. Creates `Context` with `WebSocketServerSink` + `McapWriterSink`
2. Creates `SceneUpdateChannel` and `FrameTransformChannel`
3. In a loop: publishes animated 3D scene (rotating cube, moving sphere) + coordinate transforms
4. Records to `output.mcap` simultaneously
5. Runs for 10 seconds, then graceful shutdown

- [ ] **Step 2: Write integration test**

Test that verifies: start demo → connect scripted WebSocket client → receives valid messageData binary frames → stop → verify `output.mcap` by reading file and checking MCAP magic bytes at start (0x89 MCAP0 \r\n) and end, plus verifying file size > 0. **Do NOT depend on external `mcap` CLI tool** — all MCAP validation must be done programmatically in C++ using our own McapWriter's format knowledge.

- [ ] **Step 3: Write Chapter 9 tutorial document**

Create `docs/09-端到端连接Foxglove-Studio.md` covering: architecture review (how all layers compose), thread model diagram, performance characteristics, dual-sink pattern (WebSocket + MCAP simultaneously), design retrospective (what we'd change), comparison with official quickstart example, suggested exercises for the reader.

- [ ] **Step 4: Final verification**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ctest --test-dir build --output-on-failure
```

ALL tests must pass across ALL chapters.

QA — comprehensive final check:
Run: `ctest --test-dir build --output-on-failure`
Expected: All tests pass. Print total test count (should be 7+ test executables).
Run: `ls docs/*.md | wc -l`
Expected: 10 tutorial documents (Ch0–Ch9).
Run: `./build/examples/ch09_e2e/ch09_e2e &` then after 2 seconds check: `kill %1`
Expected: No crash. `output.mcap` file created in working directory.
Run: `xxd output.mcap | head -1`
Expected: MCAP magic bytes `89 4d 43 41 50 30 0d 0a`.
Note: `mcap` CLI is NOT a project dependency. All MCAP validation is done programmatically in tests.

- [ ] **Step 5: Tag**

```bash
git add -A
git commit -m "feat(ch09): end-to-end integration with animated 3D demo"
git tag v0.9-e2e
```

---

## Verification Checklist

After all tasks complete:

- [ ] Every git tag (v0.1 through v0.9) compiles and passes tests
- [ ] `clang-format --dry-run -Werror` passes on all source files
- [ ] All 10 tutorial docs exist in `docs/`
- [ ] `third-party/foxglove-sdk/` is pinned to exact commit
- [ ] All golden-file tests pass
- [ ] ch09_e2e example connects to Foxglove Studio and shows data (manual smoke test)
