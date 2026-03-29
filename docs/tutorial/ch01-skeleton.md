# 第 1 章：项目骨架与错误处理基础

> **对应 tag**：`v0.1-skeleton`
> **预计阅读时间**：45 分钟
> **前置要求**：熟悉 C++17 基础语法，了解 CMake 的 `add_library` / `target_link_libraries`，安装好 Git

---

## 1.0 本章地图

本章从零开始搭建整个 SDK 的骨架。不会上来就写业务逻辑，而是先把「能跑起来的最小工程结构」定下来，再实现贯穿全书的错误处理机制。

完成本章后，你会拥有：

- 一个能编译、能测试的 CMake 工程
- 统一的错误枚举 `FoxgloveError` 和结果类型别名 `FoxgloveResult<T>`
- 基于 `tl::expected` 的无异常错误传播模式，以及简化样板代码的 `FOXGLOVE_TRY` 宏
- 覆盖主要分支的单元测试（Catch2 v3）
- 打好本地 tag `my-v0.1-skeleton`，为后续章节奠定基础

本章核心问题：**在 C++ 库里，如何以可组合、零开销的方式表达「操作可能失败」？**

---

## 1.1 从需求出发

### 为什么错误处理要第一个做？

在写任何功能之前，先把错误处理的「语言」定下来，能避免后续大量重构。foxglove SDK 对外是一个 C++ 库，调用方可能是机器人框架、数据采集工具，甚至嵌入式系统。这些场景有一个共同限制：**不能或不愿意使用 C++ 异常**。

需求归结为三条：

1. **可组合**：调用方能链式处理错误，不必层层嵌套 try/catch。
2. **零开销**：正常路径没有任何额外分支或堆分配。
3. **可读性**：函数签名本身就说清楚「这个操作可能失败，失败时返回什么」。

### 起点：检出骨架 tag

```bash
git clone <repo-url> my_foxglove_sdk
cd my_foxglove_sdk
git checkout -b my_foxglove_sdk v0.1-skeleton
```

验证骨架能编译并通过测试：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

所有测试全绿，说明你站在了正确的起点上。

### 项目骨架概览

`v0.1-skeleton` 的目录结构：

```
my_foxglove_sdk/
├── CMakeLists.txt
├── cmake/
│   └── dependencies.cmake
├── include/
│   └── foxglove/
│       └── error.hpp
├── src/
│   └── error.cpp
├── tests/
│   ├── CMakeLists.txt
│   └── test_error.cpp
└── examples/
    └── ch01_skeleton/
```

头文件是给调用方看的公开契约，源文件是实现细节，两者清晰分离。

---

## 1.2 设计接口（先写头文件）

### 完整的 error.hpp

设计模块先从「调用方怎么用它」出发。完整的 `include/foxglove/error.hpp`：

```cpp
#pragma once

#include <tl/expected.hpp>
#include <string>

namespace foxglove {

enum class FoxgloveError {
  None = 0,
  InvalidArgument = 1,
  ChannelClosed = 2,
  ServerStartFailed = 3,
  IoError = 4,
  SerializationError = 5,
  ProtocolError = 6,
};

template <typename T>
using FoxgloveResult = tl::expected<T, FoxgloveError>;

std::string foxglove_error_string(FoxgloveError error);

}  // namespace foxglove

#define FOXGLOVE_TRY(var, expr)                                      \
  auto _foxglove_try_##var = (expr);                                 \
  if (!_foxglove_try_##var.has_value()) {                            \
    return tl::make_unexpected(_foxglove_try_##var.error());         \
  }                                                                  \
  auto var = std::move(_foxglove_try_##var.value())
```

三个元素各司其职：`FoxgloveError` 定义所有失败场景，`FoxgloveResult<T>` 统一返回类型，`FOXGLOVE_TRY` 消除重复的错误传播样板。

### FoxgloveError 枚举设计要点

使用 `enum class` 而非裸 `enum`：

- **作用域隔离**：`FoxgloveError::IoError` 不会污染全局命名空间。
- **禁止隐式转换**：不能把错误码直接当整数用，减少 bug。

七个错误码覆盖 SDK 全部失败场景，后续章节复用这套枚举，不再扩充。

### FoxgloveResult 别名

```cpp
template <typename T>
using FoxgloveResult = tl::expected<T, FoxgloveError>;
```

`FoxgloveResult<Connection>` 清晰传达：要么返回 `Connection`，要么返回 `FoxgloveError`，不会抛异常。调用方用 `has_value()` 判断成功，`value()` 取值，`error()` 取错误码。

> **工程旁白：`tl::expected` vs 异常**
>
> C++ 异常有三个已知问题：
>
> **零开销神话**：即使不抛出异常，编译器也要为每个可能抛出的函数生成栈展开表（unwind table），增加二进制体积。嵌入式系统里这可能超出 Flash 限额。
>
> **不可组合**：`try/catch` 块打断了链式调用。三个可能失败的操作串起来，要么嵌套三层 try，要么引入额外变量，可读性差。
>
> **隐式契约**：函数签名看不出会抛什么异常，读者必须翻文档，而文档经常过时。
>
> `tl::expected<T, E>` 把「值或错误」编码进返回类型，让编译器帮你检查有没有处理失败情况。正常路径完全在栈上，无堆分配，性能与普通返回值相同。它是 C++23 `std::expected` 的向后兼容实现，在 C++17 项目里直接可用。

### FOXGLOVE_TRY 宏

手动展开错误检查很繁琐：

```cpp
auto _result = some_operation();
if (!_result.has_value()) {
  return tl::make_unexpected(_result.error());
}
auto value = std::move(_result.value());
```

`FOXGLOVE_TRY` 把它压缩成一行：

```cpp
FOXGLOVE_TRY(value, some_operation());
```

失败时自动传播错误；成功时 `value` 绑定到结果可直接使用。`##var` 是预处理器 token pasting，拼接临时变量名，同一函数里多次使用不会命名冲突。

---

## 1.3 实现核心逻辑

### CMake 工程配置

顶层 `CMakeLists.txt` 关键部分（下面只展示 Ch01 阶段的教学精简版；完整仓库的 `CMakeLists.txt` 还包含后续章节的源文件和系统依赖，这里略去以保持焦点）：

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_foxglove_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

add_library(foxglove
  src/error.cpp
  # Ch02+ 会陆续添加 src/protocol.cpp、src/channel.cpp 等
)
target_include_directories(foxglove PUBLIC include)
target_link_libraries(foxglove PUBLIC tl::expected)

enable_testing()
add_subdirectory(tests)
```

`CMAKE_CXX_STANDARD_REQUIRED ON`：找不到 C++17 就报错，不降级。`CMAKE_EXPORT_COMPILE_COMMANDS ON`：生成 `compile_commands.json`，让 clangd 提供准确补全。`PUBLIC` 关键字：链接 `foxglove` 的目标自动继承 `include/` 路径。

> **工程旁白：CMake FetchContent vs 系统包管理器**
>
> 管理 C++ 依赖有三条主要路径：系统包管理器（apt/brew）、Git Submodules、CMake FetchContent。
>
> **系统包管理器**最省事，但版本由系统决定。不同开发者机器上版本可能不同，CI 和本地行为不一致，难以复现 bug。
>
> **Git Submodules** 版本锁定，但需要 `--recursive` 克隆，且子模块的 CMake 集成往往需要手动处理，维护成本高。
>
> **FetchContent** 在 CMake 配置阶段自动下载指定版本的源码并构建。优点：版本锁定、自包含、ABI 兼容；缺点：首次配置需要网络，每个项目各自下载（不共享系统缓存）。对于 SDK 类型的库，FetchContent 是最优选择——调用方无需预先安装任何依赖，`git clone` 后直接 `cmake -B build && cmake --build build` 即可。
>
> `cmake/dependencies.cmake` 的关键片段（注意名称使用连字符 `tl-expected`，与 CMake 约定一致）：
>
> ```cmake
> include(FetchContent)
> FetchContent_Declare(
>   tl-expected
>   GIT_REPOSITORY https://github.com/TartanLlama/expected.git
>   GIT_TAG        v1.1.0
> )
> FetchContent_MakeAvailable(tl-expected)
> ```

### error.cpp 实现

`src/error.cpp` 把 `FoxgloveError` 枚举值映射到可读字符串：

```cpp
#include <foxglove/error.hpp>

namespace foxglove {

std::string foxglove_error_string(FoxgloveError error) {
  switch (error) {
    case FoxgloveError::None:               return "no error";
    case FoxgloveError::InvalidArgument:    return "invalid argument";
    case FoxgloveError::ChannelClosed:      return "channel closed";
    case FoxgloveError::ServerStartFailed:  return "server start failed";
    case FoxgloveError::IoError:            return "I/O error";
    case FoxgloveError::SerializationError: return "serialization error";
    case FoxgloveError::ProtocolError:      return "protocol error";
  }
  return "unknown error";
}

}  // namespace foxglove
```

用 `switch` 而非 `std::unordered_map`：编译器对不完整的 `enum class` switch 发出 `-Wswitch` 警告，新增错误码忘了更新这里，编译时就能发现。

---

## 1.4 测试：验证正确性

`tests/test_error.cpp` 用 Catch2 v3 覆盖三个维度。

### 枚举值与规范一致

```cpp
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
```

这组测试防止有人无意中调整枚举顺序。后续章节的 MCAP 会把错误码序列化到文件，顺序变化会破坏向后兼容性。

### FoxgloveResult 的成功与失败路径

```cpp
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
```

### FOXGLOVE_TRY 宏的传播行为

`test_error.cpp` 用两个 `TEST_CASE` 分别验证错误传播和成功透传：

**错误传播**：底层函数返回错误时，宏立即从调用方 return，不继续执行：

```cpp
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
```

**成功透传**：底层函数返回值时，宏把值绑定到变量，后续代码正常执行：

```cpp
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
```

成功路径：`val` 绑定到 `42`，`caller` 返回 `84`。错误路径：`FOXGLOVE_TRY` 立即从 `caller` 返回 `IoError`，`return std::string("should not reach")` 不会被执行。

运行测试：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期输出：所有测试通过，无失败。

---

## 1.5 与官方实现对比

> 官方 SDK 路径：`third-party/foxglove-sdk/cpp/foxglove/include/foxglove/error.hpp`

学习一个库最好的方式之一是把自己的实现和官方实现并排对比。下面从三个维度做横向分析。

### 错误码设计对比

| 维度 | 本教程实现 | 官方 SDK |
|------|-----------|----------|
| 底层类型 | `enum class FoxgloveError`（默认 `int`） | `enum class FoxgloveError : uint8_t` |
| 错误码数量 | 7 个（覆盖教程章节所需） | 20 个（覆盖完整生产需求） |
| 起始值 | `None = 0` | `Ok`（隐式为 0） |
| 序列化友好性 | 整数值有显式标注（`= 1`、`= 2`…） | 依赖枚举顺序，未显式标注 |

官方使用 `uint8_t` 作为底层类型是有意为之：错误码会被序列化进 MCAP 文件和网络协议帧，用 1 字节节省空间。本教程在 Ch01 阶段不涉及序列化，默认 `int` 足够；到 Ch07（MCAP）时你会看到为什么官方要锁定底层类型。

### 错误字符串函数对比

```cpp
// 本教程实现（error.cpp）
std::string foxglove_error_string(FoxgloveError error);

// 官方 SDK
const char* strerror(FoxgloveError error);
```

两者都提供错误码到字符串的映射，但签名不同：

- 本教程返回 `std::string`（堆分配，生命周期明确，使用方便）
- 官方返回 `const char*`（指向静态存储，零分配，适合嵌入式/实时场景）

在 SDK 库代码中，`const char*` 返回静态字符串是更保守的选择——调用方无需管理内存，也不会触发堆分配。本教程选择 `std::string` 是为了降低入门难度。

### 官方独有：WarnStream

官方 `error.hpp` 还包含一个 `WarnStream` 工具类：

```cpp
class WarnStream {
public:
  template<typename T>
  WarnStream& operator<<(const T& value) { ... }
  ~WarnStream() { std::cerr << "[foxglove] " << msg << "\n"; }
};

inline WarnStream warn() { return WarnStream{}; }
```

这是一个 RAII 风格的警告输出工具：在析构时把缓冲内容一次性输出到 `stderr`，并加上 `[foxglove]` 前缀。可以用 `FOXGLOVE_DISABLE_CPP_WARNINGS` 宏在编译期完全关闭。本教程没有实现这个工具——它属于生产库的「便利设施」，不影响核心架构理解。

### 小结

本教程实现是官方 SDK 的**精简教学版**：保留了相同的类型名（`FoxgloveError`、`FoxgloveResult<T>`）和 `tl::expected` 基础，去掉了与当前章节无关的生产细节（`uint8_t` 底层类型、`WarnStream`、完整错误码集合）。这样你能专注于理解「为什么这样设计」，而不是被细节淹没。后续章节会在需要时逐步引入官方的设计选择。

---

## 1.6 打 tag，验证完成

本章任务全部完成后，打本地 tag 标记进度：

```bash
git add -A
git commit -m "ch01: project skeleton and error handling"
git tag my-v0.1-skeleton
```

### 验证工作流

完整的验证步骤：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

预期结果：

```
Test project /path/to/my_foxglove_sdk/build
    Start 1: test_error
1/1 Test #1: test_error .......................   Passed    0.xx sec

100% tests passed, 0 tests failed out of 1
```

### 本地 tag 约定

用 `my-` 前缀（如 `my-v0.1-skeleton`）与上游的 `v0.x-xxx` tag 区分，不会互相污染。

**唯一完成标准**：`ctest` 全部通过。`git diff` 是辅助参考，不是门槛。

---

## 本章小结

本章建立了整个 SDK 的工程基础：

| 文件 | 作用 |
|------|------|
| `include/foxglove/error.hpp` | 公开错误类型：`FoxgloveError`、`FoxgloveResult<T>`、`FOXGLOVE_TRY` |
| `src/error.cpp` | `foxglove_error_string()` 实现，错误码到字符串映射 |
| `CMakeLists.txt` | CMake 工程定义，FetchContent 拉取 `tl::expected` |
| `tests/test_error.cpp` | Catch2 单元测试，覆盖枚举值、成功路径、错误路径、宏传播 |

**关键设计决策**：

- `enum class` 保证作用域隔离和类型安全
- `tl::expected` 让错误处理可组合、零开销、类型安全
- `FOXGLOVE_TRY` 消除重复的错误传播样板
- `FetchContent` 使整个工程自包含，调用方无需预装依赖

**下一章**（`v0.2-protocol`）在这套错误处理机制的基础上，实现 Foxglove WebSocket 协议的编解码层。所有协议错误都会用 `FoxgloveResult<T>` 返回，你会看到 `FOXGLOVE_TRY` 在真实业务逻辑里的用武之地。
