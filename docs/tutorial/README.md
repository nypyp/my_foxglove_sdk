# my_foxglove_sdk 手把手教程

本教程带你从零开始，一行一行地实现 `my_foxglove_sdk`——一个纯 C++17 的 Foxglove WebSocket 协议 SDK。
风格参考陈硕《Linux 多线程服务端编程》：先讲清楚「为什么」，再带你写「是什么」。

---

## 如何使用本教程

每一章对应一个 git tag。你在该 tag 的骨架上添加代码，跑通测试，再进入下一章。

**第一步：克隆仓库**

```bash
git clone <repo-url>
cd my_foxglove_sdk
```

**第二步：从骨架出发**

```bash
git checkout -b my_foxglove_sdk v0.1-skeleton
```

这会在 `v0.1-skeleton` tag 上创建本地分支 `my_foxglove_sdk`，你的所有改动都在这个分支上进行。Ch01 就从这个骨架出发，逐步添加代码。

**第三步：验证起点可编译**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

初始状态下测试会失败（因为实现还是空的），这是正常的。每章结束时 `ctest` 全部通过才算完成。

---

## 教程 vs 参考文档

| 你想要... | 看这里 |
|----------|-------|
| 跟着写代码，理解实现思路 | `docs/tutorial/`（本目录） |
| 深入理解某个模块的设计 | `docs/0N-xxx.md` 参考文档 |
| 查阅 API 细节 | `include/foxglove/*.hpp` 头文件注释 |

两套文档互补，不重复。教程带你「走」一遍，参考文档帮你「查」细节。

---

## 章节索引

| 章节 | Tag | 主题 | 核心工程知识点 |
|------|-----|------|---------------|
| Ch01 | `v0.1-skeleton` | 项目骨架与错误处理 | CMake 工程结构、`tl::expected`、`FoxgloveResult<T>` |
| Ch02 | `v0.2-protocol` | Foxglove 协议层 | 二进制序列化、小端字节序、JSON 协议报文 |
| Ch03 | `v0.3-channel` | Channel 与 Schema 抽象 | RAII、回调模型、原子 ID 分配 |
| Ch04 | `v0.4-server` | WebSocket 服务器 | libwebsockets 事件循环、PIMPL 模式 |
| Ch05 | `v0.5-serialization` | 消息序列化 | JSON 序列化、golden file 测试 |
| Ch06 | `v0.6-context` | Context 与 Sink 路由 | 观察者模式、type erasure、死锁预防 |
| Ch07a | `v0.7a-mcap-basic` | MCAP 最小实现 | 二进制格式、文件 I/O 错误处理 |
| Ch07b | `v0.7b-mcap-full` | MCAP chunking/压缩/索引 | zstd 压缩、索引设计 |
| Ch08 | `v0.8-messages` | 内置消息类型 | 类型安全 channel、template 设计 |
| Ch09 | `v0.9-e2e` | 端到端集成 | 集成测试设计、系统组合 |

Ch07 拆为两章：Ch07a 完成最小可用的 MCAP 写入，Ch07b 在此基础上加入 chunking、zstd 压缩和完整索引。

---

## Git 工作流速查

```bash
# 打本地 tag 记录进度（用 my- 前缀，不污染上游 tag）
git tag my-v0.1-skeleton

# 查看与参考实现的差异（辅助工具，非完成标准）
git diff v0.2-protocol HEAD -- src/protocol.cpp

# 完成标准：ctest 全部通过
ctest --test-dir build --output-on-failure

# 从任意章节重新开始（若分支已存在，用 -B 强制重置；见「章节间依赖」）
git checkout -B my_foxglove_sdk v0.3-channel
```

**本地 tag 约定**：用 `my-` 前缀（如 `my-v0.1-skeleton`、`my-v0.2-protocol`），与上游的 `v0.x-xxx` tag 区分开，不会互相污染。

**唯一完成标准**：`ctest` 全部通过。`git diff` 是辅助参考，不是门槛。

---

## 章节间依赖

章节严格顺序依赖：Ch01 → Ch02 → Ch03 → ... → Ch09。每章假设你已完成上一章的全部代码。

若要从中间某章开始，检出**前一章**对应的 upstream tag 作为起点（tag 名见上方「章节索引」表）：

```bash
# 例：想直接从 Ch04 开始，检出 Ch03 对应的 tag
# 若本地分支 my_foxglove_sdk 已存在，用 -B 强制重置到该 tag
git checkout -B my_foxglove_sdk v0.3-channel
# 然后跟着 ch04-server.md 的指引添加代码
```

通用规则：想从 ChN 开始，就将分支重置到前一章的 tag。各章 tag 名可在「章节索引」表中查到（如 Ch03 对应 `v0.3-channel`，Ch06 对应 `v0.6-context`）。

---

## 工程旁白说明

每章穿插「工程旁白」专栏，用图标区分类型：

| 图标 | 类型 | 内容 |
|------|------|------|
| 🏗️ | 设计决策 | 为什么这样设计，有哪些备选方案 |
| ⚠️ | 常见陷阱 | 这里容易犯什么错，如何避免 |
| ⚡ | 性能/并发 | 性能考量、线程安全分析 |
| 🔍 | 对比视角 | 与其他库/方案的横向对比 |
| 🧰 | C++ 技巧 | 值得记住的 C++ 惯用法 |

旁白独立于主线，可以跳过，不影响代码实现。

---

## 章节文件速查

| 文件 | 章节 |
|------|------|
| [ch01-skeleton.md](ch01-skeleton.md) | Ch01 项目骨架与错误处理 |
| [ch02-protocol.md](ch02-protocol.md) | Ch02 Foxglove 协议层 |
| [ch03-channel.md](ch03-channel.md) | Ch03 Channel 与 Schema 抽象 |
| [ch04-server.md](ch04-server.md) | Ch04 WebSocket 服务器 |
| [ch05-serialization.md](ch05-serialization.md) | Ch05 消息序列化 |
| [ch06-context.md](ch06-context.md) | Ch06 Context 与 Sink 路由 |
| [ch07a-mcap-basic.md](ch07a-mcap-basic.md) | Ch07a MCAP 最小实现 |
| [ch07b-mcap-full.md](ch07b-mcap-full.md) | Ch07b MCAP chunking/压缩/索引 |
| [ch08-messages.md](ch08-messages.md) | Ch08 内置消息类型 |
| [ch09-e2e.md](ch09-e2e.md) | Ch09 端到端集成 |
