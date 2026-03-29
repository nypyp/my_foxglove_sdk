# 设计文档：docs/tutorial/ 手把手教程体系

**日期**：2026-03-28  
**状态**：已批准（Oracle 两轮审查后修订，v2）  
**作者**：Sisyphus (brainstorming session)

---

## 1. 背景与目标

### 1.1 现状

`docs/` 目录下已有 9 篇高质量参考文档（`00-前言与项目概览.md` 至 `09-端到端连接Foxglove-Studio.md`），覆盖了各章节的概念、API 设计和工程决策。这些文档定位为**概念参考手册**——读者是被动接受知识的。

### 1.2 缺口

缺少一套**教学式手把手教程**：像陈硕《Linux多线程服务端编程》（muduo）那样，读者自己创建分支，跟着教程一步步把代码写出来，过程中通过工程旁白获得库设计的通用知识。

### 1.3 目标

在 `docs/tutorial/` 下新建一套教程，满足：

1. 读者能从零开始，跟着教程在自己的分支上实现完整的 SDK
2. 每章对应一个 git tag，有明确的阶段性成就和可验证的完成状态
3. 工程知识（设计决策、陷阱、性能、对比）以旁白形式嵌入，不打断主干叙事
4. 与现有 docs 互补而非重复——教程聚焦「怎么写」，现有 docs 聚焦「是什么/为什么」

---

## 2. 目标读者

- **C++ 水平**：熟悉 C++17 语法，无需额外解释语言特性
- **工程背景**：没有网络库/SDK 开发经验，但有一定的工程直觉
- **学习方式**：愿意动手跟着写代码，而不仅仅是阅读

---

## 3. 教程结构方案：双轨制（Dual-Track）

每章分两个轨道并行：

- **主轨（叙事）**：引导读者从需求出发，一步步写出代码，配工程旁白
- **副轨（地图）**：章首给出本章的文件变更地图，章尾给出与现有 docs 的交叉引用

读者可以跟着主轨写代码，也可以用副轨快速定位感兴趣的工程知识点。

**章节严格顺序依赖**：每章假设读者已完成上一章。非线性阅读者应从目标章节的前一个 upstream tag checkout：

```bash
# 例如：想直接从 Ch04 开始
git checkout -b my_foxglove_sdk v0.3-channel
```

---

## 4. 目录结构

```
docs/
├── tutorial/
│   ├── README.md                     ← 教程导航页
│   ├── ch01-skeleton.md              ← 对应 tag v0.1-skeleton
│   ├── ch02-protocol.md              ← 对应 tag v0.2-protocol
│   ├── ch03-channel.md               ← 对应 tag v0.3-channel
│   ├── ch04-server.md                ← 对应 tag v0.4-server
│   ├── ch05-serialization.md         ← 对应 tag v0.5-serialization
│   ├── ch06-context.md               ← 对应 tag v0.6-context
│   ├── ch07a-mcap-basic.md           ← 对应 tag v0.7a-mcap-basic
│   ├── ch07b-mcap-full.md            ← 对应 tag v0.7b-mcap-full
│   ├── ch08-messages.md              ← 对应 tag v0.8-messages
│   └── ch09-e2e.md                   ← 对应 tag v0.9-e2e
├── 00-前言与项目概览.md               ← 现有 docs 保持不变
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

**MCAP 拆为两章的原因**：`v0.7a-mcap-basic` 和 `v0.7b-mcap-full` 是两个独立教学里程碑。ch07a 讲最小可工作的 MCAP writer（无压缩、无 chunking），ch07b 在此基础上演进到生产级实现（chunking + zstd + 索引）。拆开让读者先获得运行版本，再理解优化层次。

---

## 5. 每章固定模板（硬性约束）

所有章节必须包含以下全部 section，保证跨章节一致性。section 深度可按复杂度调整，但不得省略。

```markdown
# Chapter N[a/b]：[标题]

> **对应 tag**：`vN.x-name`
> **起点**：`vN-1.x-prev-name`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `src/xxx.cpp` — [一句话职责说明]
> - `include/foxglove/xxx.hpp` — [一句话职责说明]
> - `tests/test_xxx.cpp` — [测试覆盖范围说明]
>
> **深入阅读**：[0N-概念文档.md](../0N-概念文档.md)
> **预计时间**：XX 分钟

## N.0 本章地图

[100 字以内：本章解决什么问题，读完后读者掌握什么能力。]

## N.1 从需求出发

[问题引入：没有这个模块系统缺什么？引出本章动机。]

## N.2 设计接口（先写头文件）

[展示完整 .hpp 公开接口，逐块解释设计意图。]

> 💡 **[类型] 工程旁白：[主题]**
> [150–300 字]

## N.3 实现核心逻辑

[展示 .cpp 核心片段。每个代码块后必须有至少一句解释：这段代码解决了什么问题，
为什么这样写而不是另一种写法。非关键样板代码可省略，但省略处注明「完整代码见仓库 tag」。]

> 💡 **[类型] 工程旁白：[主题]**
> [...]

## N.4 测试：验证正确性

[展示关键测试用例。顺序：先说「我们要验证什么」，再展示测试代码，最后才是实现细节。
说明测试策略（单元 / golden file / 集成）及选择理由。]

> 💡 **[类型] 工程旁白：[主题]**（可选）

## N.5 打 tag，验证完成

以下命令为标准完成流程，所有章节统一：

```bash
# 1. 构建并运行测试（这是唯一的正确性标准）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag（my- 前缀避免与仓库参考 tag 冲突）
git add .
git commit -m "feat(chN): [描述]"
git tag my-vN.x-name

# 3. 与参考实现对比（辅助理解，非强制门槛）
git diff vN.x-name
```

**完成标准**：`ctest` 全部通过是硬性门槛。`git diff vN.x-name` 可能存在风格差异，这是正常的——测试通过即代表实现正确，diff 仅用于辅助理解参考实现的设计选择。

## 本章小结

- **本章掌握了**：[3–5 条 bullet]
- **工程知识点**：[本章工程旁白标题索引]
- **延伸练习**：[1–3 个可选练习，每条说明预期收获]
- **参考文档**：[链接到现有 docs 对应章节]
```

---

## 6. 工程旁白规范

### 6.1 类型标签

| 标签 | 含义 |
|-----|------|
| 🏗️ **设计决策** | 为什么选这个方案而不是其他方案 |
| ⚠️ **常见陷阱** | 这里容易犯什么错，后果是什么 |
| ⚡ **性能/并发** | 对 cache、线程、内存的影响 |
| 🔍 **对比视角** | 与 boost/gRPC/标准库/其他方案的对比 |
| 🧰 **C++ 技巧** | 语言层面的工程实践 |

### 6.2 质量约束

- 每章 **2–4 个**旁白
- 每个旁白 **150–300 字**，超出则拆分或移至现有 docs
- 旁白必须与周围代码**直接相关**，不插入泛泛之论
- 旁白末尾可附 1–2 个延伸阅读链接

---

## 7. git 工作流规范

### 7.1 读者初始设置（教程 README 中说明）

```bash
# 克隆仓库
git clone <repo-url>
cd my_foxglove_sdk

# 从第一章起点创建自己的分支
git checkout -b my_foxglove_sdk v0.1-skeleton

# 验证环境
cmake -B build && cmake --build build -j$(nproc)
ctest --test-dir build
```

### 7.2 本地 tag 命名约定

读者打 tag 时统一使用 `my-` 前缀，避免与仓库已有参考 tag 名称冲突：

| 章节完成后 | 读者打的 tag | 仓库参考 tag |
|----------|------------|------------|
| Ch01 完成 | `my-v0.1-skeleton` | `v0.1-skeleton` |
| Ch02 完成 | `my-v0.2-protocol` | `v0.2-protocol` |
| ... | ... | ... |
| Ch07a 完成 | `my-v0.7a-mcap-basic` | `v0.7a-mcap-basic` |
| Ch07b 完成 | `my-v0.7b-mcap-full` | `v0.7b-mcap-full` |

### 7.3 对比验证方式

```bash
# 查看与参考 tag 的差异（理解自己和参考实现的不同）
git diff v0.2-protocol

# 查看本章新增了哪些文件
git diff --name-only my-v0.1-skeleton my-v0.2-protocol

# 如果卡住，查看参考实现的完整文件
git show v0.2-protocol:src/protocol.cpp
```

### 7.4 调试代码偏离参考时的策略

当 `git diff vN.x-name` 显示功能性差异时，排查顺序：
1. 先跑 `ctest`——测试是唯一的正确性标准，不是与参考 tag 完全一致
2. 如果测试失败，逐条比对失败测试的输入/输出与预期
3. 如果测试通过但 diff 很大，说明实现了同样功能但方式不同，这是正常的

---

## 8. README.md 导航页结构

```markdown
# Foxglove SDK 手把手教程

## 如何使用本教程

1. 克隆仓库，从第一章的参考起点创建自己的分支：
   ```bash
   git clone <repo-url>
   cd my_foxglove_sdk
   git checkout -b my_foxglove_sdk v0.1-skeleton
   ```
   Ch01 从 `v0.1-skeleton` 这个已有骨架出发，逐步添加代码。

2. 跟着每章教程，在你的分支上逐步实现代码

3. 每章末尾打本地 tag（my-vN.x-name），与参考 tag 对比验证

## 教程 vs 参考文档

| 你想要... | 看这里 |
|----------|-------|
| 手把手写代码 | 本教程 docs/tutorial/ |
| 深入理解某个概念 | 参考文档 docs/0N-xxx.md |

## 章节索引

| 章节 | Tag | 主题 | 核心工程知识点 |
|-----|-----|------|---------------|
| Ch01 | v0.1-skeleton | 项目骨架与错误处理 | CMake 工程结构、tl::expected |
| Ch02 | v0.2-protocol | Foxglove 协议编解码 | 二进制序列化、小端序陷阱 |
| Ch03 | v0.3-channel | Channel/Schema 抽象 | 类型设计、RAII |
| Ch04 | v0.4-server | WebSocket 服务器 | 事件循环、线程安全 |
| Ch05 | v0.5-serialization | 消息序列化 | JSON、golden file 测试 |
| Ch06 | v0.6-context | Context/Sink 路由 | 观察者模式、死锁预防 |
| Ch07a | v0.7a-mcap-basic | MCAP 最小实现 | 二进制格式设计、文件 I/O |
| Ch07b | v0.7b-mcap-full | MCAP chunking/压缩/索引 | zstd 压缩、索引设计 |
| Ch08 | v0.8-messages | 内置消息类型 | 类型安全 channel、template 设计 |
| Ch09 | v0.9-e2e | 端到端集成 | 集成测试设计、系统组合 |
```

---

## 9. 各章工程旁白规划（预设）

| 章节 | 旁白主题 | 类型 |
|-----|---------|------|
| Ch01 | CMake FetchContent vs 系统包管理器 | 🏗️ 设计决策 |
| Ch01 | `tl::expected` vs 异常：什么时候用哪个 | 🔍 对比视角 |
| Ch02 | 小端序与跨平台陷阱：为什么不能直接 memcpy | ⚠️ 常见陷阱 |
| Ch02 | 二进制协议向后兼容性设计 | 🏗️ 设计决策 |
| Ch03 | RAII 与资源生命周期管理 | 🧰 C++ 技巧 |
| Ch03 | `std::string_view` vs `std::string`：零拷贝的代价 | ⚡ 性能/并发 |
| Ch04 | libwebsockets 事件循环模型 vs asio | 🔍 对比视角 |
| Ch04 | 多线程与事件循环：哪些操作是线程安全的 | ⚡ 性能/并发 |
| Ch05 | golden file 测试：适用边界与维护成本 | 🏗️ 设计决策 |
| Ch05 | JSON 序列化的性能陷阱 | ⚠️ 常见陷阱 |
| Ch06 | 观察者模式的死锁风险：回调持锁的反模式 | ⚠️ 常见陷阱 |
| Ch06 | type erasure 与 `std::function` 的开销 | 🧰 C++ 技巧 |
| Ch07a | MCAP 格式设计：为什么不用 rosbag | 🔍 对比视角 |
| Ch07a | 文件 I/O 的错误处理：fwrite 的返回值你真的检查了吗 | ⚠️ 常见陷阱 |
| Ch07b | MCAP basic → full 的演进：增量优化的工程节奏 | 🏗️ 设计决策 |
| Ch07b | zstd 压缩级别与 chunk 大小的工程选择 | ⚡ 性能/并发 |
| Ch08 | 类型安全 channel 的 template 设计 | 🧰 C++ 技巧 |
| Ch08 | Protobuf schema 描述符的自描述性 | 🏗️ 设计决策 |
| Ch09 | 集成测试边界：不测外部，测组合行为 | 🏗️ 设计决策 |
| Ch09 | 测试中的时间依赖：如何避免 flaky test | ⚠️ 常见陷阱 |