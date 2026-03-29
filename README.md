# my_foxglove_sdk

一个纯 C++17 实现的 Foxglove WebSocket 协议 SDK，支持：

- **WebSocket 实时推送**：通过 Foxglove 协议与 Foxglove Studio 实时可视化
- **MCAP 文件录制**：支持 chunking、zstd 压缩和索引的离线录制
- **类型安全的消息通道**：基于模板的 TypedChannel，编译期类型检查
- **模块化架构**：Context + Sink 路由模式，便于扩展

---

## 快速开始

### 克隆仓库

```bash
git clone https://github.com/nypyp/my_foxglove_sdk.git
cd my_foxglove_sdk
```

### 构建与测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### 运行示例

```bash
# Chapter 9 端到端示例（实时可视化 + MCAP 录制）
./build/examples/ch09_e2e/ch09_e2e
# 程序运行约 10 秒后自动退出，生成 output.mcap
```

---

## 文档体系

本项目有两套文档互补：

| 你想要... | 看这里 |
|----------|-------|
| 跟着教程一步步实现 SDK | [docs/tutorial/](docs/tutorial/) |
| 深入理解某个模块的设计 | [docs/](docs/) |
| 查阅 API 细节 | [include/foxglove/](include/foxglove/) |

---

## 教程（推荐学习路径）

手把手教程带你从零实现完整 SDK，每章对应一个 git tag：

| 章节 | Tag | 主题 |
|------|-----|------|
| Ch01 | `v0.1-skeleton` | 项目骨架、CMake、错误处理 |
| Ch02 | `v0.2-protocol` | Foxglove 协议编解码 |
| Ch03 | `v0.3-channel` | Channel 与 Schema 抽象 |
| Ch04 | `v0.4-server` | WebSocket 服务器 |
| Ch05 | `v0.5-serialization` | JSON 消息序列化 |
| Ch06 | `v0.6-context` | Context 与 Sink 路由 |
| Ch07a | `v0.7a-mcap-basic` | MCAP 最小实现 |
| Ch07b | `v0.7b-mcap-full` | MCAP 压缩与索引 |
| Ch08 | `v0.8-messages` | 内置消息类型 |
| Ch09 | `v0.9-e2e` | 端到端集成 |

详见 [docs/tutorial/README.md](docs/tutorial/README.md)

---

## 项目结构

```
my_foxglove_sdk/
├── include/foxglove/     # 头文件
│   ├── server.hpp       # WebSocket 服务器
│   ├── context.hpp      # 消息路由上下文
│   ├── mcap.hpp         # MCAP 文件写入
│   ├── messages.hpp     # 消息类型定义
│   └── ...
├── src/                 # 实现文件
├── tests/               # 单元测试
├── examples/            # 示例程序
│   └── ch09_e2e/        # 端到端演示
├── docs/                # 文档
│   ├── tutorial/         # 手把手教程
│   └── *.md             # 参考文档
└── CMakeLists.txt
```

---

## 技术栈

- **C++17**
- **libwebsockets**: WebSocket 服务端
- **nlohmann/json**: JSON 序列化
- **zstd**: MCAP 压缩
- **Catch2**: 单元测试

---

## 参考

- [Foxglove WebSocket Protocol](https://docs.foxglove.dev/docs/protocols/websocket)
- [MCAP Recording Format](https://foxglove.dev/docs/data-recording/mcap)
- [Foxglove Studio](https://foxglove.dev/studio)
