# Chapter 7a：MCAP 最小写入器，先把离线录制跑起来

> **对应 tag**：`v0.7a-mcap-basic`
> **起点**：`v0.6-context`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `include/foxglove/mcap.hpp` — 定义 `McapWriter` 的最小公开接口、记录数据结构和写入选项
> - `src/mcap.cpp` — 实现 magic、Header、Schema、Channel、Message、DataEnd、Footer 的二进制写入逻辑
> - `tests/test_mcap.cpp` — 验证最小 MCAP 文件的二进制结构、record 存在性和落盘行为
>
> **深入阅读**：[07-MCAP文件写入.md](../07-MCAP文件写入.md)
> **预计时间**：60 分钟
> **前置要求**：完成第 6 章，理解 `Context` / `Sink` 的路由关系，并且已经接受“在线推送”和“离线记录”是两条独立链路

---

## 7a.0 本章地图

前六章解决了实时链路，数据可以通过 WebSocket 送进 Foxglove Studio，但系统还缺一个稳定的离线出口。本章实现最小可工作的 `McapWriter`。读完后，你能写出一个包含 magic、Header、Schema、Channel、Message、DataEnd、Footer 的合法 `.mcap` 文件，并用项目内测试验证其二进制结构。

```text
起始 magic
    |
    v
Header -> Schema -> Channel -> Message -> DataEnd -> Footer -> 结束 magic
```

这章只做一件事，**先把最小 writer 跑通**。chunking、zstd 压缩、ChunkIndex、`McapWriterSink` 的完整生产级路径，都留到 Ch07b 再讲。

---

## 7a.1 从需求出发

### 实时可视化不是全部，工程系统还需要离线回放

走到 Ch06，我们已经能把消息送进 WebSocket 服务器，再被 Foxglove Studio 实时显示出来。这个链路很重要，因为它让我们能边开发边观察系统行为。但只靠实时链路有一个天然短板，**数据一旦错过，就真的错过了**。

想象几个非常常见的现场场景。

第一类场景是调 bug。程序在凌晨 2 点偶发一次姿态跳变，你不可能永远盯着 Studio 看，也不可能要求故障每次都稳定复现。第二类场景是算法回放。你想比较旧算法和新算法对同一段输入数据的表现差异，就需要把原始消息保存下来。第三类场景是现场留证。机器人、车载系统、传感器网关这类系统，一旦部署出去，日志和数据录制往往是排障的最后防线。

这说明在线可视化和离线回放并不是“一个替代另一个”的关系，而是两条独立价值链：

- WebSocket 解决“现在能不能看见”
- MCAP 解决“之后还能不能复盘”

如果没有文件录制层，我们做出的 SDK 只能算一个实时传输工具，还称不上完整的数据基础设施。

### 为什么这里选 MCAP，而不是自己发明一个 dump 格式

从教学角度说，我们完全可以自己定义一个二进制日志格式，例如每条消息都写成：topic 长度 + topic 内容 + 时间戳 + payload。这样做短期看很省事，但长期一定会走向重复造轮子。

MCAP 的价值不是“字段更多”，而是它已经把数据录制里几个关键问题回答清楚了：

- 文件开头和结尾如何自描述
- Schema、Channel、Message 这些记录怎样组织
- 读工具怎样知道某段 payload 属于哪个 topic、哪个 schema
- 未来如果要加压缩、索引、随机访问，该放在哪一层扩展

教学版在这里的策略很明确，**先只实现最小合法子集**。也就是先让文件结构正确、消息能写进去、测试能证明结构有效。之后再在 Ch07b 里讨论大文件优化、压缩和索引问题。这和前面章节的节奏一致，先做能工作的版本，再做更强的版本。

### 这一章真正要解决的问题

本章不是在讲“如何用第三方工具检查 MCAP”，也不是在讲“如何做最高性能的文件录制器”。这章只回答三个最基本的问题：

1. `McapWriter` 的接口应该长什么样，才能和当前 SDK 风格一致？
2. 最小合法 MCAP 文件需要写入哪些 record？顺序是什么？
3. 我们怎样在项目内用测试证明自己写出的二进制结构大体正确？

这三个问题一旦回答清楚，Ch07b 才有稳固地基。否则一上来就谈 chunk、压缩、索引，读者很容易只记住特性名，却没吃透最基本的 record layout。

---

## 7a.2 设计接口（先写头文件）

这一章先看 `include/foxglove/mcap.hpp`。当前仓库里其实已经同时放着最小 writer 和更完整的 full-writer 路径，包括后面 Ch07b 会详细讨论的选项与 `McapWriterSink`。但在 Ch07a，我们有意识地只聚焦**最小写入路径**：打开 writer，注册 schema 和 channel，写消息，最后 close。

### 先看最小公开接口

先说明一个术语对齐问题：计划文档里把最小入口写成 `create()`，但当前仓库真实源码把这个动作拆成了 `open()`（面向文件）和 `open_buffer()`（面向内存缓冲区）。从职责上看，它们扮演的就是“创建/打开一个可写 MCAP writer”的角色，所以本章会以真实源码接口为准，同时把它视为计划里 `create()` 语义的落地版本。

```cpp
struct McapWriterOptions {
  std::string profile = "";
  std::string library = "my_foxglove_sdk/0.1";
  bool use_chunks = false;
  McapCompression compression = McapCompression::None;
  size_t chunk_size = 1024 * 1024;
};

struct McapMessage {
  uint16_t channel_id;
  uint32_t sequence;
  uint64_t log_time;
  uint64_t publish_time;
  const uint8_t* data;
  size_t data_len;
};

class McapWriter {
 public:
  static FoxgloveResult<McapWriter> open(
    const std::string& path, const McapWriterOptions& options = {}
  );

  static McapWriter open_buffer(
    std::vector<uint8_t>& buf, const McapWriterOptions& options = {}
  );

  FoxgloveResult<uint16_t> add_schema(
    const std::string& name, const std::string& encoding,
    const std::vector<uint8_t>& data
  );

  FoxgloveResult<uint16_t> add_channel(
    uint16_t schema_id, const std::string& topic,
    const std::string& message_encoding,
    const std::map<std::string, std::string>& metadata = {}
  );

  FoxgloveResult<void> write_message(const McapMessage& msg);
  FoxgloveResult<void> close();
};
```

这里先抓住四个点。

**第一，接口风格和前面章节一致。** `open()` 返回 `FoxgloveResult<McapWriter>`，说明打开文件可能失败。`add_schema()`、`add_channel()` 返回分配出的 ID。`write_message()` 和 `close()` 用 `FoxgloveResult<void>` 表达“要么成功，要么给出错误”。这和前面 `Context`、`WebSocketServer`、`RawChannel` 的错误处理方式是统一的。

**第二，最小 writer 也要区分 file-backed 和 in-memory 两种打开方式。** `open()` 面向真正落盘，`open_buffer()` 面向测试。后者非常关键，因为教程不希望把验证建立在外部 CLI 工具上，而是希望测试直接检查内存里的字节布局。

**第三，Schema、Channel、Message 被建模为独立阶段。** 这不是 API 形式主义，而是 MCAP 格式本身要求 message 记录并不自带完整 topic/schema 描述，它只引用 `channel_id`。所以调用顺序天然应该是：先注册 schema，再注册 channel，再写 message。

**第四，`McapWriterOptions` 在头文件里已经出现了 `use_chunks`、`compression`、`chunk_size`。** 这说明作者在一开始就给未来优化留了接口位置。但 Ch07a 不会使用这些能力，默认值 `use_chunks = false` 才是本章主角。

### 这章只关心哪些 record

最小合法 writer 只需要关注下面这组 record。它们也是本章叙事主线。

| 顺序 | 内容 | opcode | 本章职责 |
|------|------|--------|----------|
| 1 | Magic | 无 record opcode | 标记这就是一个 MCAP 文件 |
| 2 | Header | `0x01` | 写 `profile` 和 `library` |
| 3 | Schema | `0x03` | 描述消息 schema |
| 4 | Channel | `0x04` | 把 topic 与 schema 绑定 |
| 5 | Message | `0x05` | 记录真正的数据 payload |
| 6 | DataEnd | `0x0F` | 标记 data section 结束 |
| 7 | Footer | `0x02` | 写 summary 起点等尾部元信息 |
| 8 | Magic | 无 record opcode | 尾 magic，收尾自校验 |

如果你只记住一句话，那就是：**最小 MCAP 文件不是“把 payload 直接追加到文件里”，而是把 payload 放进一串有边界、有类型的 records 里。**

### 头文件里有哪些内容先按下不表

`mcap.hpp` 还定义了这些内容：

- `McapCompression`
- `McapSchema` / `McapChannel`
- `McapWriterSink`

它们都已经在当前仓库中实现，并且后面的测试也会覆盖其中一部分 richer 路径；只是 Ch07a 不把它们当主线展开。原因很简单，Ch07a 的目标是理解**最小二进制写入路径**，不是一次把 full writer 的所有层次都装进脑子里。等你能稳定写出 Header、Schema、Channel、Message、Footer，再回头看 chunking、索引和 sink 适配层，理解成本会低很多。

> 💡 **🔍 对比视角 工程旁白：MCAP 格式设计，为什么不用 rosbag**
>
> 很多人第一次看到 MCAP，会问一个现实问题：ROS 生态已经有 rosbag 了，为什么还要学另一个文件格式？关键不在“谁历史更久”，而在目标边界不同。rosbag 天然带着 ROS 运行时语义，很多设计默认读写双方都活在 ROS 世界里。MCAP 则更像一个中立容器，它把重点放在跨语言、跨中间件、跨工具链的数据录制与回放上。你可以把同一份 MCAP 文件拿给 Foxglove Studio、Python 脚本、C++ 工具甚至别的非 ROS 系统处理，而不用把整个运行时环境一起搬过去。
>
> 对教学版来说，这个选择还有一个额外好处：它迫使我们把 Schema、Channel、Message 的边界讲清楚，而不是躲进“反正 rosbag 已经替我们规定好了”这类既有约定里。也正因为如此，本章先只实现最小 MCAP writer，先把容器格式的骨架吃透，再在 Ch07b 进入压缩、索引和更完整的录制能力。

---

## 7a.3 实现核心逻辑

头文件定好以后，`src/mcap.cpp` 要解决的核心问题其实可以压缩成一句话：**把一组结构化字段稳定地写成 little-endian 的 record 流。**

### 第一步，先把最基础的字节写入助手准备好

源码一开头就是一组 little-endian helper：

```cpp
void append_u16_le(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32_le(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
}

void append_u64_le(std::vector<uint8_t>& out, uint64_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 32U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 40U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 48U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 56U) & 0xFFU));
}
```

为什么这里不用“直接把整数地址 `reinterpret_cast` 成字节再写出去”？因为那样会把宿主机字节序、对齐和布局细节偷偷带进文件格式。手工按 little-endian 逐字节写入虽然啰嗦，但含义最明确，也最接近协议文档本身。

字符串和原始字节数组也有自己的小助手：

```cpp
void append_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
  if (len == 0) {
    return;
  }
  out.insert(out.end(), data, data + len);
}

void append_string(std::vector<uint8_t>& out, const std::string& value) {
  append_u32_le(out, static_cast<uint32_t>(value.size()));
  append_bytes(out, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}
```

这也解释了后面所有 record payload 的共同风格，**字符串一律是 `uint32 length + bytes`，原始数据一律是明确长度后再追加字节**。

### 第二步，把“record”抽象成统一外壳

MCAP 在本章范围内最关键的一条格式约定就是：

```text
record = opcode(1 byte) + record_length(8 bytes, little-endian) + payload(record_length bytes)
```

源码里对应的是：

```cpp
FoxgloveResult<void> write_record(uint8_t opcode, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> header;
  header.reserve(9);
  header.push_back(opcode);
  append_u64_le(header, static_cast<uint64_t>(payload.size()));

  write_raw(header.data(), header.size());
  if (!payload.empty()) {
    write_raw(payload.data(), payload.size());
  }

  if (io_error) {
    return tl::make_unexpected(FoxgloveError::IoError);
  }
  return {};
}
```

这个抽象的好处是，Header、Schema、Channel、Message、DataEnd、Footer 的差异全都被压缩到了“payload 怎么编码”，外壳一律交给 `write_record()` 负责。也就是说，**本章不是在写六套互不相干的文件格式逻辑，而是在写一套通用外壳，加上六种 payload 编码方式。**

### 第三步，写文件开头，magic 和 Header 先落地

最早写入文件的是 magic bytes：

```cpp
constexpr std::array<uint8_t, 8> kMagic = {
  0x89, 0x4D, 0x43, 0x41, 0x50, 0x30, 0x0D, 0x0A
};

FoxgloveResult<void> write_magic() {
  write_raw(kMagic.data(), kMagic.size());
  if (io_error) {
    return tl::make_unexpected(FoxgloveError::IoError);
  }
  return {};
}
```

紧接着是 Header：

```cpp
FoxgloveResult<void> write_header() {
  std::vector<uint8_t> payload;
  append_string(payload, options.profile);
  append_string(payload, options.library);
  return write_record(kHeaderOp, payload);
}
```

这里的含义很直接。magic 负责说明“这就是 MCAP 文件”，Header 负责说明“这是哪个 profile、哪个 library 写出来的”。这两步都发生在真正业务数据之前，因为 reader 要先知道自己打开的是什么。

### 第四步，Schema、Channel、Message 三连击才是真正的数据主体

`add_schema()` 的编码逻辑很直接：先分配 `schema_id`，再按字段顺序编码。

```cpp
const uint16_t schema_id = impl_->next_schema_id++;

std::vector<uint8_t> payload;
append_u16_le(payload, schema_id);
append_string(payload, name);
append_string(payload, encoding);
append_u32_le(payload, static_cast<uint32_t>(data.size()));
append_bytes(payload, data.data(), data.size());

auto write_schema_result = impl_->write_record(kSchemaOp, payload);
```

`add_channel()` 与它类似，只不过要把 topic、message encoding 和 metadata 写进去：

```cpp
const uint16_t channel_id = impl_->next_channel_id++;

std::vector<uint8_t> payload;
append_u16_le(payload, channel_id);
append_u16_le(payload, schema_id);
append_string(payload, topic);
append_string(payload, message_encoding);
append_u32_le(payload, static_cast<uint32_t>(metadata.size()));
for (const auto& [key, value] : metadata) {
  append_string(payload, key);
  append_string(payload, value);
}
```

而 `write_message()` 则是把真正的消息内容和时间戳写入 record：

```cpp
std::vector<uint8_t> payload;
append_u16_le(payload, msg.channel_id);
append_u32_le(payload, msg.sequence);
append_u64_le(payload, msg.log_time);
append_u64_le(payload, msg.publish_time);
append_bytes(payload, msg.data, msg.data_len);

if (!impl_->options.use_chunks) {
  return impl_->write_record(kMessageOp, payload);
}
```

这里有两个非常值得注意的边界。

第一，writer 会检查关键 ID 的一致性约束，但要分清细节：`write_message()` 会严格要求 `channel_id` 已存在；`add_channel()` 则允许 `schema_id == 0` 作为“无 schema”通道，同时也支持引用一个已注册 schema。也就是说，**最小 writer 不是无约束的字节泵**，但它的约束是按 MCAP 语义精细划分的，而不是简单“一刀切”。

第二，虽然代码里已经出现了 `use_chunks` 分支，但本章默认走的是 `false` 路径。也就是 message record 直接写入 data section。后面的 chunk buffer、压缩和索引都属于 Ch07b 的叙事范围，这里先只认清“最短成功路径”即可。

### 一个最小 record 布局图

只看 Ch07a，脑中应该形成下面这张结构图：

```text
[magic 8B]
[Header op=0x01][len 8B][profile][library]
[Schema op=0x03][len 8B][schema_id][name][encoding][schema_data]
[Channel op=0x04][len 8B][channel_id][schema_id][topic][message_encoding][metadata]
[Message op=0x05][len 8B][channel_id][sequence][log_time][publish_time][payload]
[DataEnd op=0x0F][len 8B][0x00000000]
[Footer op=0x02][len 8B][summary_start][summary_offset_start][summary_crc]
[magic 8B]
```

这张图的核心价值不在于你要背下每个偏移，而在于你要意识到，**MCAP 文件是一串 record 的线性流，不是一个神秘黑盒。** 测试也正是据此去定位 opcode、解析长度和检查关键字段。

### 第五步，`close()` 负责收尾，不只是“关文件”

最小 writer 的 `close()` 很有代表性，因为它告诉你文件格式层面的“关闭”到底意味着什么：

```cpp
FoxgloveResult<void> close() {
  if (!impl_ || impl_->closed) {
    return {};
  }

  uint64_t summary_start = 0;
  auto summary_result = impl_->write_summary_if_needed(summary_start);
  if (!summary_result.has_value()) {
    return tl::make_unexpected(summary_result.error());
  }

  std::vector<uint8_t> data_end_payload;
  append_u32_le(data_end_payload, 0U);
  auto data_end_result = impl_->write_record(kDataEndOp, data_end_payload);

  std::vector<uint8_t> footer_payload;
  append_u64_le(footer_payload, summary_start);
  append_u64_le(footer_payload, 0U);
  append_u32_le(footer_payload, 0U);
  auto footer_result = impl_->write_record(kFooterOp, footer_payload);

  auto tail_magic_result = impl_->write_magic();
  impl_->closed = true;
  return impl_->finalize_file();
}
```

注意这个顺序。`close()` 不是单纯的 `fclose()` 包装，而是在语义上完成三件事：

1. 结束 data section，写入 `DataEnd`
2. 写入 `Footer`
3. 补上尾 magic，再真正关闭底层文件

在 Ch07a 的最小实现里，`summary_start` 还保持为 0，这也是刻意的。因为 summary、ChunkIndex 和更完整的 summary section 会放到 Ch07b 再展开。这里先让 Footer 的骨架存在，但不提前灌进后续章节的复杂度。

> 💡 **⚠️ 常见陷阱 工程旁白：文件 I/O 的错误处理，`fwrite` / `ofstream::write` 的返回值你真的检查了吗**
>
> 文件写入最容易让人掉以轻心的地方是，代码“看起来写了”，不等于数据“真的落下去了”。磁盘满了、权限不对、底层设备异常、网络文件系统抖动，这些都可能让 `fwrite` 只写出一部分字节。如果你忽略返回值，程序表面继续往下跑，最后得到的可能是一个结构损坏但又很难第一时间发现的文件。本仓库在 `open()` 里显式检查 `std::fwrite` 的写入字节数，只要 `wrote != len`，就把 `io_error` 置为真，后续统一返回 `FoxgloveError::IoError`。
>
> 这个习惯不只适用于 `fwrite`。如果你改用 `std::ofstream::write`，也应该检查 stream state，而不是默认“C++ 流对象会自己处理”。二进制文件格式一旦写坏，排查成本通常远高于网络请求失败，因为错误往往要等到读回放时才暴露出来。对 writer 这类组件来说，宁可尽早失败，也不要沉默损坏。

---

## 7a.4 测试：验证正确性

### 先说策略，我们要验证什么

这章最重要的不是“文件能不能创建出来”，而是**创建出来的字节流是不是长得像一个最小合法 MCAP 文件**。所以测试策略以二进制结构测试为主，而不是依赖外部 `mcap info` 之类工具。

具体来说，本章至少要锁住三类风险：

1. 文件是否真的以前后 magic 包裹，并包含 Header / Footer
2. Schema / Channel / Message 这些关键 record 是否出现
3. 写入到内存 buffer 和写入真实文件两条路径是否都可用

### 测试一，先验证最基础的外壳

`tests/test_mcap.cpp` 先准备了几个很朴素但很有用的 helper，比如检查前后 magic，或者在字节流里扫描 opcode：

```cpp
bool has_magic_prefix(const std::vector<uint8_t>& buf) {
  if (buf.size() < kMcapMagic.size()) {
    return false;
  }
  for (size_t i = 0; i < kMcapMagic.size(); ++i) {
    if (buf[i] != kMcapMagic[i]) {
      return false;
    }
  }
  return true;
}

size_t find_opcode_offset(const std::vector<uint8_t>& buf, uint8_t opcode) {
  size_t i = 8;
  while (i + 9 <= buf.size()) {
    const uint8_t op = buf[i];
    uint64_t len = 0;
    for (int b = 0; b < 8; ++b) {
      len |= static_cast<uint64_t>(buf[i + 1 + static_cast<size_t>(b)]) << (8U * b);
    }
    if (op == opcode) {
      return i;
    }
    i += 9 + static_cast<size_t>(len);
  }
  return buf.size();
}
```

然后最基本的测试会验证 writer 在 close 之后，前后 magic 都存在，Header 在开头，Footer 在结尾附近：

```cpp
TEST_CASE("McapWriter - writes valid header and footer") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  REQUIRE(writer.close().has_value());

  REQUIRE(has_magic_prefix(buf));
  REQUIRE(has_magic_suffix(buf));
  REQUIRE(buf.size() > 8U);
  REQUIRE(buf[8] == 0x01U);

  const size_t footer_index = find_opcode_offset(buf, 0x02U);
  REQUIRE(footer_index < buf.size() - 8U);
}
```

这类测试看起来没有“业务语义”，但它直接锁住了最小文件格式边界。因为一旦开头不是 Header，或者尾部找不到 Footer，reader 连继续解析的基础都没有。

### 测试二，验证 Schema / Channel / Message 的存在与顺序感

下一组测试验证关键 record 是否真的写了进去：

```cpp
TEST_CASE("McapWriter - writes schema record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  REQUIRE(writer.close().has_value());
  REQUIRE(find_opcode(buf, 0x03U));
}

TEST_CASE("McapWriter - writes channel record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  REQUIRE(channel_result.has_value());

  REQUIRE(writer.close().has_value());
  REQUIRE(find_opcode(buf, 0x04U));
}
```

这组测试的价值是把“注册 schema / channel”从纯内存状态推进为“真的反映到文件结构里”。否则你只能证明 API 调用了，不能证明文件里有对应 record。

### 测试三，message 不只存在，还要检查关键字段

只验证 `0x05` opcode 出现还不够，因为那只能证明“某个 message record 在文件里”，却不能证明 payload 结构一定对。所以下面这个测试继续向前迈一步，至少检查 message payload 开头的 `channel_id`：

```cpp
TEST_CASE("McapWriter - writes message record") {
  std::vector<uint8_t> buf;
  auto writer = McapWriter::open_buffer(buf);

  const std::vector<uint8_t> schema_data = {'{', '}'};
  auto schema_result = writer.add_schema("Example", "jsonschema", schema_data);
  REQUIRE(schema_result.has_value());

  auto channel_result = writer.add_channel(schema_result.value(), "/demo/topic", "json");
  REQUIRE(channel_result.has_value());

  const std::array<uint8_t, 3> payload = {0x10U, 0x20U, 0x30U};
  McapMessage msg{channel_result.value(), 7, 1234567890ULL, 1234567999ULL,
                  payload.data(), payload.size()};
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  const size_t message_index = find_opcode_offset(buf, 0x05U);
  const size_t payload_offset = message_index + 1U + 8U;
  REQUIRE(read_u16_le(buf, payload_offset) == channel_result.value());
}
```

这里的思路很值得记住。我们并没有把整个 message payload 全量解码一遍，而是只抓一个最关键、最容易错位的字段做断言。这样的测试既能锁住二进制布局风险，也不会把测试本身写成另一个复杂 reader。

### 测试四，真实文件路径也要覆盖

除了 `open_buffer()`，测试里还专门覆盖了 `open()` 的真实落盘路径：

```cpp
TEST_CASE("McapWriter - write to file") {
  constexpr const char* kPath = "/tmp/test_mcap_output.mcap";

  auto open_result = McapWriter::open(kPath);
  REQUIRE(open_result.has_value());
  auto writer = std::move(open_result.value());

  // 省略 add_schema / add_channel / write_message

  REQUIRE(writer.close().has_value());

  FILE* file = std::fopen(kPath, "rb");
  REQUIRE(file != nullptr);
  // 读回磁盘内容，再检查 magic
}
```

这一步是在防一个很实际的问题，**内存 buffer 路径对，不代表文件 I/O 路径也对**。尤其前面我们刚强调过 I/O 错误处理的重要性，所以必须有一个测试真正走到磁盘。

### 这一章刻意不测什么

你会注意到 `tests/test_mcap.cpp` 后半段已经出现了 chunk、zstd、ChunkIndex、CRC32 甚至 `McapWriterSink` 的测试。这说明当前仓库里的 richer path 其实已经存在；但这些都不属于 Ch07a 的主线。

本章刻意不展开这些内容，是因为最小 writer 的学习目标是：

- 理解 record 外壳
- 理解 Header / Schema / Channel / Message / Footer 的顺序与职责
- 理解怎样在项目内验证字节流结构

等这三点站稳之后，再看 full writer 的 chunk 缓冲、压缩分支、索引布局，读者就不会把“优化层”误当成“基本层”。

---

## 7a.5 与官方实现对比

写到这里，很适合退后一步，看看教学版最小 writer 和官方 `mcap` 生态的关系。

先说结论，**这章不是在复刻官方 full writer，而是在复刻一个足够小、足够清楚的最小子集。**

可以从三个角度理解这件事。

### 第一，教学版先强调 record layout，官方实现更强调完整能力

官方实现通常会同时覆盖更多 record 类型、更完整的 summary section、压缩路径、索引和兼容性细节。那是生产级库应有的样子。

教学版 Ch07a 刻意只保留：

- magic
- Header
- Schema
- Channel
- Message
- DataEnd
- Footer

这样做不是功能偷懒，而是教学聚焦。因为你要先真正明白“一个 message 为什么不能脱离 channel 单独存在”“Footer 为什么不是简单的文件结束标记”“为什么所有 record 都复用 `opcode + length + payload` 这个统一骨架”。这些概念先吃透，比一开始就接触全量特性更重要。

### 第二，官方实现会把性能和可检索性做得更彻底

只用最小 writer，已经足够生成一个能表达 schema、topic、时间戳和 payload 的离线文件。但它还有明显边界：

- 大文件写入时没有 chunk 分段
- 没有 zstd 压缩
- 没有 ChunkIndex 这类 summary 索引
- 不能直接代表生产环境里更完整的录制链路

这些边界并不是缺陷，而是本章刻意保留的“下一层复杂度”。换句话说，Ch07a 对官方实现的态度不是“我和你一样完整”，而是“我先把你的骨架学会，再去学你的肌肉”。

### 第三，验证方式也不同，教程优先项目内证明

官方工具链通常会附带 reader、CLI 或更完备的检查器。但教程故意把完成标准收敛到项目内测试，不要求读者依赖额外工具。原因有两个。

一是可重复。仓库内测试可以直接进 CI，也能保证每个读者在相同环境下得到相同结论。二是可解释。我们不是只想知道“外部工具说这个文件合法”，而是想知道“这个 opcode 为什么在这个偏移，这个长度为什么这样算，这个字段为什么这么写”。

所以本章和官方实现的差异，不该理解为“哪个更好”，而该理解为“教学版在当前章节只拿出最适合学习的那一层”。下一章 Ch07b 才会把优化层加回来。

---

## 7a.6 打 tag，验证完成

以下命令是本章统一完成流程。注意，唯一硬性门槛仍然是 `ctest` 全部通过。

```bash
# 1. 构建并运行测试（这是唯一的正确性标准）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag（my- 前缀避免与仓库参考 tag 冲突）
git add .
git commit -m "feat(ch07a): add basic mcap writer"
git tag my-v0.7a-mcap-basic

# 3. 与参考实现对比（辅助理解，非强制门槛）
git diff v0.7a-mcap-basic
```

**完成标准**：`ctest` 全部通过，就是本章唯一的硬门槛。即使你的 `git diff v0.7a-mcap-basic` 与参考实现有风格差异，只要测试全绿，就说明你的最小 writer 已经完成了本章目标。

这里还要特别提醒一句，Ch07a 的完成不等于“MCAP 模块已经彻底做完”。它只代表最小 writer 已经站稳。下一章 Ch07b 才会在这个基础上加入 chunking、zstd 压缩、ChunkIndex 和更完整的录制链路。

---

## 本章小结

- **本章掌握了**：
  - 为什么 SDK 除了 WebSocket 实时链路，还需要 MCAP 离线录制链路
  - `McapWriter` 最小接口如何与现有 `FoxgloveResult<T>` 风格保持一致
  - MCAP 最小 record 集合的职责，magic、Header、Schema、Channel、Message、DataEnd、Footer 各自负责什么
  - 为什么所有 record 都复用 `opcode + length + payload` 这层统一外壳
  - 如何用项目内二进制结构测试验证 writer，而不是依赖外部 CLI

- **工程知识点**：
  - `MCAP 格式设计，为什么不用 rosbag`
  - `文件 I/O 的错误处理，fwrite / ofstream::write 的返回值你真的检查了吗`

- **延伸练习**：
  - 尝试在 `test_mcap.cpp` 里再加一个断言，检查 Footer payload 中的 `summary_start` 在 Ch07a 默认仍为 0。你会更熟悉 Footer 的字段布局。
  - 自己画一张只包含 Header、Schema、Channel、Message 的偏移草图，然后对照 `find_opcode_offset()` 的扫描逻辑验证它。你会更扎实地理解 record 边界。
  - 在不打开 `use_chunks` 的前提下，试着写两条不同 `sequence` 的 message，并在测试里检查第二条 message record 确实存在。你会更清楚 data section 的线性追加模型。

- **参考文档**：[07-MCAP文件写入.md](../07-MCAP文件写入.md)

到这里，我们已经把离线录制这条链路真正打开了，但只打开了第一层。下一章会继续沿着同一个 `McapWriter` 往前走，把 chunking、zstd、ChunkIndex 和 `McapWriterSink` 补上，完成从“能写”到“更适合真实录制”的过渡。
