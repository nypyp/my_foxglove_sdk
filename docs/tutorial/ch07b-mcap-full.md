# Chapter 7b：MCAP 完整写入器，把 chunking、压缩、索引和 Sink 真正接起来

> **对应 tag**：`v0.7b-mcap-full`
> **起点**：`v0.7a-mcap-basic`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `include/foxglove/mcap.hpp` — 补齐 `McapCompression`、`McapWriterOptions`、`McapWriterSink` 等完整公开接口
> - `src/mcap.cpp` — 实现 `flush_chunk()`、zstd 压缩、CRC32、`ChunkIndex` 汇总写入、`McapWriterSink` 路由逻辑
> - `tests/test_mcap.cpp` — 用项目内测试验证 Chunk、ChunkIndex、CRC32、压缩路径和 Sink 集成
>
> **深入阅读**：[07-MCAP文件写入.md](../07-MCAP文件写入.md)
> **预计时间**：75 分钟
> **前置要求**：完成 Ch07a，已经理解最小 `McapWriter` 的 record 外壳、little-endian 编码方式，以及 `Context` / `Sink` 的路由模型

---

## 7b.0 本章地图

Ch07a 解决了“能写出合法 MCAP 文件”，但还没有解决“大文件怎么高效写、怎么索引、怎么自动接入 Context”这三个工程问题。本章在最小 writer 基础上加入 chunking、zstd、ChunkIndex、CRC32 和 `McapWriterSink`。读完后，你能理解从 basic writer 演进到 full writer 的完整节奏，并且知道怎样只靠仓库内测试验证实现正确。这里说的“索引”在当前仓库里主要指 **chunk 级索引**：writer 能写出 `ChunkIndex` records，记录 chunk 的时间范围、偏移和大小；更细的 message-level index 仍未在这一章展开。

```text
Channel::log(...)
    |
    v
Context 路由
    |
    v
McapWriterSink::on_message()
    |
    v
McapWriter::write_message()
    |
    +--> use_chunks = false  -> 直接写 Message record
    |
    \--> use_chunks = true   -> 先进入 chunk_data
                               -> flush_chunk()
                               -> Chunk + ChunkIndex
                               -> close() 时写 Summary / DataEnd / Footer
```

本章是 Ch07a 的自然延续，不是推翻重写。主线非常简单，**先保留最小 writer 的正确性，再把性能、chunk 级可检索性和接入便利性一层层叠上去。**

---

## 7b.1 从需求出发

Ch07a 的最小实现已经足够把一段消息录成 `.mcap` 文件，但只要文件稍微变大，你很快会撞上三个问题。

### 问题一，消息直接平铺，文件越大越难用

如果所有 `Message` record 都直接平铺在 Data Section，reader 想找到“第 30 秒附近的数据”，就只能从前往后扫。文件越大，定位越慢。对教学版来说，这个问题在小文件里不明显，但真实录制系统一旦跑上分钟级、小时级，线性扫描的代价就会立刻显现。

### 问题二，不压缩就是白白浪费磁盘和 I/O

传感器消息、JSON 消息、结构化二进制 payload 往往有大量重复信息。如果写入前完全不压缩，离线录制就会把磁盘和带宽都当成无限资源。可一旦压缩粒度选错，比如把整个文件当一大块压缩，又会把 seek 能力一并牺牲掉。

### 问题三，业务代码不该手写“录制循环”

Ch06 已经建立了 `Context` 和 `Sink` 路由模型。WebSocket 实时发送可以通过 sink 自动接入，离线录制当然也应该走同一条路。否则每个业务示例都要自己维护 channel ID、schema 注册和 `write_message()` 调用顺序，既重复又容易错。

所以 Ch07b 的目标不是“再加几个 feature 名词”，而是把最小 writer 升级成一个更像工程模块的东西：

- 用 **chunking** 给文件建立天然边界
- 用 **zstd** 在压缩率和速度之间取一个够用平衡
- 用 **ChunkIndex** 让 Summary 区域能描述每个 chunk 的时间范围和偏移（当前实现仍未展开更细的 message-level index）
- 用 **CRC32** 校验未压缩 chunk 数据，确保写入语义没有悄悄损坏
- 用 **`McapWriterSink`** 把录制过程纳入 `Context` 的统一路由模型

从这里也能看出本章和 Ch07a 的关系。前一章回答“MCAP 文件最少要写什么”，这一章回答“当文件真的要拿去长期录制时，还缺哪些工程层”。先有 basic，再有 full，这个顺序本身就是教程想传达的工程思维。

---

## 7b.2 设计接口（先写头文件）

先看 `include/foxglove/mcap.hpp`。这一章最值得关注的不是 `McapWriter` 的最小 API 本身，而是围绕它补出来的三层扩展点：压缩枚举、chunk 选项，以及 `Context` 适配用的 `McapWriterSink`。

### 先看新增的配置入口

```cpp
enum class McapCompression : uint8_t {
  None = 0,
  Zstd = 1,
};

struct McapWriterOptions {
  std::string profile = "";
  std::string library = "my_foxglove_sdk/0.1";
  bool use_chunks = false;
  McapCompression compression = McapCompression::None;
  size_t chunk_size = 1024 * 1024;
};
```

这几个字段串起来看，意图很清楚。

- `use_chunks` 决定走最小写入路径还是完整写入路径
- `compression` 决定 chunk payload 最后是原样落盘还是压成 `zstd`
- `chunk_size` 决定多大时触发一次 flush
- `profile` / `library` 则继续保留 MCAP 头部元信息，和 Ch07a 保持兼容

也就是说，**Ch07b 不是通过新增一套 `FullMcapWriter` 类来实现升级，而是让同一个 `McapWriter` 在 options 驱动下进入更完整的行为模式。** 这让 basic 和 full 两条路径共享同一个核心类型，也更符合“渐进增强”的教学节奏。

### `McapWriterSink` 是完整 writer 的真正入口

```cpp
class McapWriterSink : public Sink {
public:
  static FoxgloveResult<std::shared_ptr<McapWriterSink>> create(
    const std::string& path, const McapWriterOptions& options = {}
  );

  void on_channel_added(RawChannel& channel) override;
  void on_channel_removed(uint32_t channel_id) override;
  void on_message(uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time) override;

  FoxgloveResult<void> close();

private:
  McapWriter writer_;
  std::mutex mutex_;
  std::unordered_map<uint32_t, uint16_t> channel_map_;
  uint32_t sequence_ = 0;

  explicit McapWriterSink(McapWriter writer);
};
```

这个设计很重要，因为它把“写 MCAP 文件”从一个手工调用型 API，变成了 `Context` 的一个 observer。你只需要把 sink 挂进去，后面 channel 创建、消息到达、sequence 递增、channel ID 映射，都会自动走 writer 逻辑。

这里有两个接口细节值得留意。

第一，`create()` 返回 `shared_ptr<McapWriterSink>`，这和前面章节的 sink 使用方式一致，方便交给 `Context` 长期持有。

第二，`channel_map_` 用 `uint32_t -> uint16_t` 建立映射。前者是 `Context` 侧 `RawChannel` 的 ID，后者是 MCAP 文件内独立分配的 channel ID。两边 ID 空间不强绑，是个很健康的分层设计，因为文件格式自己的编号策略不应该反过来污染运行时对象模型。

### 这一章的接口地图

从教程视角看，可以把 full writer 拆成下面几块职责：

| 接口/字段 | 所在位置 | 作用 |
|----------|----------|------|
| `McapCompression` | `mcap.hpp` | 选择 chunk 压缩方案 |
| `McapWriterOptions::use_chunks` | `mcap.hpp` | 控制消息是直接写出还是先进 chunk 缓冲 |
| `McapWriterOptions::chunk_size` | `mcap.hpp` | 决定 flush 门槛 |
| `McapWriter::write_message()` | `mcap.cpp` | 在 direct-write 和 chunk-buffer 两条路径之间分流 |
| `Impl::flush_chunk()` | `mcap.cpp` | 计算 CRC32、压缩、写 Chunk、记录 ChunkIndexEntry |
| `Impl::write_summary_if_needed()` | `mcap.cpp` | close 时统一写出所有 `ChunkIndex` |
| `McapWriterSink::on_channel_added()` | `mcap.cpp` | 把 `RawChannel` 描述符映射成 MCAP schema/channel |
| `McapWriterSink::on_message()` | `mcap.cpp` | 把 `Context` 消息自动落成 MCAP message |

> 💡 **🏗️ 设计决策 工程旁白：MCAP basic → full 的演进——增量优化的工程节奏**
>
> 这个章节拆成 Ch07a 和 Ch07b，看起来像“教程排版问题”，其实是很典型的工程节奏选择。先做最小 writer，有两个直接好处。第一，你会更早拿到一个能跑通的闭环，magic、Header、Schema、Channel、Message、Footer 都能稳定落盘，测试也能立刻建立起来。第二，后面的优化就有了清晰的锚点，你知道自己是在“保留正确性的前提下增强能力”，而不是一开始就把 chunk、压缩、索引、sink、并发一起搅进来。
>
> 真正的工程优化通常都该这样做。先得到一个最小且可验证的版本，再观察瓶颈出现在哪里，再沿着瓶颈方向加层次。这样每一步都有回退点，也更容易写出真正锁定行为的测试。Ch07b 不是推翻 Ch07a，而是在它之上加 chunk 级可检索性、压缩和接入能力。这种演进方式比“第一次就写成终极版”稳得多，也更接近真实项目里的迭代节奏。

---

## 7b.3 实现核心逻辑

头文件定完以后，真正的工程味主要都在 `src/mcap.cpp`。这一章要抓住五段代码，它们一起构成了 full writer 的主干：`write_message()` 分流、`flush_chunk()`、zstd 压缩、`write_summary_if_needed()`、`McapWriterSink` 集成。

### 先看 `write_message()` 怎样把 direct-write 变成 chunk-buffer

```cpp
FoxgloveResult<void> McapWriter::write_message(const McapMessage& msg) {
  if (!impl_ || impl_->closed) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  if (impl_->channels.find(msg.channel_id) == impl_->channels.end()) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  if (msg.data == nullptr && msg.data_len > 0) {
    return tl::make_unexpected(FoxgloveError::InvalidArgument);
  }

  std::vector<uint8_t> payload;
  append_u16_le(payload, msg.channel_id);
  append_u32_le(payload, msg.sequence);
  append_u64_le(payload, msg.log_time);
  append_u64_le(payload, msg.publish_time);
  append_bytes(payload, msg.data, msg.data_len);

  if (!impl_->options.use_chunks) {
    return impl_->write_record(kMessageOp, payload);
  }

  std::vector<uint8_t> record = make_record(kMessageOp, payload);
  append_bytes(impl_->chunk_data, record.data(), record.size());
  ...
}
```

和 Ch07a 相比，最大的变化其实就一行判断，`if (!impl_->options.use_chunks) { ... }`。这说明 basic 与 full 路径没有分成两个实现体系，而是共享同一套输入校验、同一套 message payload 编码，只在“最后落到哪里”上分岔。

紧接着的这段逻辑也很关键：

```cpp
if (!impl_->chunk_has_messages) {
  impl_->chunk_has_messages = true;
  impl_->chunk_start_time = msg.log_time;
  impl_->chunk_end_time = msg.log_time;
} else {
  if (msg.log_time < impl_->chunk_start_time) {
    impl_->chunk_start_time = msg.log_time;
  }
  if (msg.log_time > impl_->chunk_end_time) {
    impl_->chunk_end_time = msg.log_time;
  }
}

if (impl_->chunk_data.size() >= impl_->options.chunk_size) {
  return impl_->flush_chunk();
}
```

这段代码解决的是一个很具体的问题，**ChunkIndex 不是只需要“chunk 从哪开始”，还需要知道这个 chunk 覆盖的消息时间范围。** 所以 writer 不能只攒字节数，还得随着消息写入实时维护 `chunk_start_time` 和 `chunk_end_time`。

### `flush_chunk()` 是整个 full writer 的中心

真正把“缓冲区里的消息”变成一个 `Chunk` record 的，是 `Impl::flush_chunk()`：

```cpp
FoxgloveResult<void> flush_chunk() {
  if (!options.use_chunks || chunk_data.empty()) {
    return {};
  }

  const uint64_t uncompressed_size = static_cast<uint64_t>(chunk_data.size());
  const uint32_t uncompressed_crc =
    chunk_data.empty() ? 0U : crc32_compute(chunk_data.data(), chunk_data.size());

  std::string compression;
  std::vector<uint8_t> compressed_data;
  if (options.compression == McapCompression::Zstd && !chunk_data.empty()) {
    compression = "zstd";
    compressed_data.resize(ZSTD_compressBound(chunk_data.size()));
    const size_t compressed_size = ZSTD_compress(
      compressed_data.data(), compressed_data.size(), chunk_data.data(), chunk_data.size(), 1
    );
    if (ZSTD_isError(compressed_size) != 0U) {
      return tl::make_unexpected(FoxgloveError::IoError);
    }
    compressed_data.resize(compressed_size);
  } else {
    compression.clear();
    compressed_data = chunk_data;
  }
  ...
}
```

这段实现值得逐项拆开看。

第一，CRC32 是对 **未压缩数据** 算的，而不是对压缩后的字节流算的。这样 reader 解压后就能验证“原始 chunk 内容有没有被破坏”。

第二，压缩分支只在 `use_chunks=true` 的前提下有意义。因为只有 chunk 有完整 payload 容器，才适合把一批 message 作为一组数据统一压缩。

第三，这里把 zstd 压缩级别固定成了 `1`。教学版没有把“压缩级别”暴露成外部配置项，而是先锁定一条足够简单、速度优先的默认路径。

### 计算 CRC32 的 helper 很短，但它锁定了一个重要契约

`src/mcap.cpp` 顶部的 CRC32 helper 是：

```cpp
uint32_t crc32_compute(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      crc = (crc >> 1U) ^ (0xEDB88320U & static_cast<uint32_t>(-(crc & 1U)));
    }
  }
  return ~crc;
}
```

它的工程意义不在于“算法有多复杂”，而在于它让测试能直接验证 chunk 里的 `uncompressed_crc` 字段是不是对的。没有这个字段，很多文件仍然“能写出来”，但一旦压缩前后的字节边界、长度或者写入顺序出了偏差，问题只能在 reader 侧晚很久才暴露。

### 写出 Chunk 之后，还要留下 ChunkIndexEntry 给 Summary

`flush_chunk()` 写完 `Chunk` record 后，并没有立刻写索引，而是先把索引信息存进内存：

```cpp
const uint64_t chunk_record_start = bytes_written;
auto write_result = write_record(kChunkOp, payload);
...

ChunkIndexEntry entry;
entry.message_start_time = chunk_start_time;
entry.message_end_time = chunk_end_time;
entry.chunk_start_offset = chunk_record_start;
entry.chunk_length = static_cast<uint64_t>(1U + 8U + payload.size());
entry.compression = compression;
entry.compressed_size = static_cast<uint64_t>(compressed_data.size());
entry.uncompressed_size = uncompressed_size;
chunk_indexes.push_back(std::move(entry));
```

然后在 `close()` 里统一写出 summary：

```cpp
FoxgloveResult<void> write_summary_if_needed(uint64_t& summary_start) {
  if (!options.use_chunks) {
    summary_start = 0;
    return {};
  }

  summary_start = chunk_indexes.empty() ? 0U : bytes_written;
  for (const auto& index : chunk_indexes) {
    std::vector<uint8_t> payload;
    append_u64_le(payload, index.message_start_time);
    append_u64_le(payload, index.message_end_time);
    append_u64_le(payload, index.chunk_start_offset);
    append_u64_le(payload, index.chunk_length);
    append_u16_u64_map(payload, {});
    append_u64_le(payload, 0U);
    append_string(payload, index.compression);
    append_u64_le(payload, index.compressed_size);
    append_u64_le(payload, index.uncompressed_size);
    auto result = write_record(kChunkIndexOp, payload);
    if (!result.has_value()) {
      return result;
    }
  }

  return {};
}
```

这个顺序有两个好处。

- writer 在正常写消息时不用反复回头修改文件头或 Summary 区域
- close 时可以一次性得到完整的 chunk 列表，天然适合顺序写出 `ChunkIndex`

这里还能顺手看到一个教学版的简化点，`append_u16_u64_map(payload, {})` 和后面的 `append_u64_le(payload, 0U)` 表示当前实现还没有深入讲 `message_index_offsets` 与更细的 message index 结构。也就是说，本章的 full writer 已经足够覆盖 chunking、压缩、索引和 sink 集成，但仍然保留了继续演进的空间。

### `close()` 的顺序也必须讲清楚

和 Ch07a 一样，`close()` 依旧是文件结构收尾的关键入口，只是现在多了 flush 与 summary 两步：

```cpp
auto flush_result = impl_->flush_chunk();
...

uint64_t summary_start = 0;
auto summary_result = impl_->write_summary_if_needed(summary_start);
...

std::vector<uint8_t> data_end_payload;
append_u32_le(data_end_payload, 0U);
auto data_end_result = impl_->write_record(kDataEndOp, data_end_payload);
...

std::vector<uint8_t> footer_payload;
append_u64_le(footer_payload, summary_start);
append_u64_le(footer_payload, 0U);
append_u32_le(footer_payload, 0U);
auto footer_result = impl_->write_record(kFooterOp, footer_payload);
```

所以 full writer 的收尾顺序是：

1. flush 最后一个未写出的 chunk
2. 写 summary 中的 `ChunkIndex`
3. 写 `DataEnd`
4. 写 `Footer`
5. 写尾 magic

这也是为什么测试里会专门验证 `ChunkIndex` 出现在 `DataEnd` 之前。这个顺序不是“看着顺手”，而是当前实现明确承诺的一部分。

### 最后一层，把 writer 嵌进 `Context`

`McapWriterSink` 的两个核心入口是 `on_channel_added()` 和 `on_message()`：

```cpp
void McapWriterSink::on_channel_added(RawChannel& channel) {
  std::lock_guard<std::mutex> lock(mutex_);

  const uint32_t context_id = channel.id();
  const auto& desc = channel.descriptor();

  auto schema_result = writer_.add_schema(desc.schema.name, desc.schema.encoding, desc.schema.data);
  if (!schema_result.has_value()) {
    return;
  }

  auto channel_result = writer_.add_channel(schema_result.value(), desc.topic, desc.encoding);
  if (!channel_result.has_value()) {
    return;
  }

  channel_map_[context_id] = channel_result.value();
}
```

```cpp
void McapWriterSink::on_message(
  uint32_t channel_id, const uint8_t* data, size_t len, uint64_t log_time
) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = channel_map_.find(channel_id);
  if (it == channel_map_.end()) {
    return;
  }

  McapMessage msg{it->second, sequence_++, log_time, log_time, data, len};
  (void)writer_.write_message(msg);
}
```

这里的关键不是“把消息写进去”这么简单，而是三个细节。

- `on_channel_added()` 使用 `RawChannel::descriptor()` 自动读取 topic、encoding、schema，避免业务层重复声明一遍同样信息
- `channel_map_` 保存 Context ID 到 MCAP channel ID 的转换，保证运行时对象和文件内部编号各自独立
- `sequence_++` 在 sink 内部维护，业务侧不用再关心 message sequence 的组装

这就把 full writer 从“一个会写文件的类”变成了“一个可挂接到系统路由里的录制后端”。对于真实工程来说，这一步往往比 chunking 本身还更重要，因为它决定了这个模块是不是足够容易接入系统。

> 💡 **⚡ 性能/并发 工程旁白：zstd 压缩级别与 chunk 大小的工程选择**
>
> 压缩和 chunk 大小从来不是各自独立的旋钮，它们一定要一起看。chunk 太小，索引会很多，seek 粒度确实更细，但压缩器每次拿到的数据也更少，字典和窗口利用率就差，压缩率很难上去。chunk 太大，压缩率通常更好，Summary 也更紧凑，可每次 flush 的内存峰值、尾部延迟和失败恢复成本都会升高。当前实现把 `chunk_size` 默认成 1 MiB，再把 zstd level 固定成 1，本质上是优先保住“写得快、占内存可控、压缩收益已经明显”这条路径。
>
> 对大多数教学和中等吞吐录制场景，这个默认值已经够用。真要针对业务做细调，通常也应该先量数据特征，再动参数，而不是凭感觉调到很极端。高重复文本和结构化消息往往愿意接受更大 chunk 来换压缩率，低延迟系统则更可能把 chunk 切小，保证 flush 更及时。一个实用原则是，先确保项目内测试和真实样本录制都稳定，再谈压缩参数优化，否则很容易在“理论最优”里丢掉工程可控性。

---

## 7b.4 测试：验证正确性

这章的测试策略和 Ch07a 一样，核心原则仍然是：**优先做项目内、程序化、可重复的验证，而不是把外部 CLI 当成硬门槛。**

原因很简单。外部工具当然能帮助你理解文件结构，但教程真正需要锁定的是：当前仓库里的 writer 在 chunking、压缩、索引、CRC32 和 sink 集成上是不是按预期工作。这个问题最稳妥的回答方式，就是读我们自己写出的字节，再用测试断言它们满足契约。

### 先验证 Chunk 和 ChunkIndex 确实写出来了

`tests/test_mcap.cpp` 里最直观的两类测试，是直接检查 opcode 是否存在：

```cpp
TEST_CASE("McapWriter - chunked output contains Chunk record") {
  std::vector<uint8_t> buf;
  McapWriterOptions opts;
  opts.use_chunks = true;
  opts.compression = McapCompression::None;

  auto writer = McapWriter::open_buffer(buf, opts);
  ...
  REQUIRE(writer.write_message(msg).has_value());
  REQUIRE(writer.close().has_value());

  REQUIRE(has_opcode(buf, 0x06U));
}
```

```cpp
TEST_CASE("McapWriter - ChunkIndex records present in summary") {
  std::vector<uint8_t> buf;
  McapWriterOptions opts;
  opts.use_chunks = true;

  auto writer = McapWriter::open_buffer(buf, opts);
  ...
  REQUIRE(writer.close().has_value());

  REQUIRE(has_opcode(buf, 0x08U));
}
```

这两段测试并不“高深”，但它们先把最基本的行为钉住了，启用 chunk 路径后，文件里必须能看到 `Chunk` 和 `ChunkIndex` 两类 record。连这一步都不稳定，就没必要往更细的压缩语义和索引顺序上谈。

### 再验证收尾顺序是不是我们承诺的那样

full writer 的一个具体约定是，`ChunkIndex` 在 `DataEnd` 之前写出。测试里也直接锁住了这个顺序：

```cpp
TEST_CASE("McapWriter - close writes ChunkIndex records before DataEnd") {
  ...
  REQUIRE(writer.close().has_value());

  const size_t chunk_index_pos = find_opcode_offset(buf, 0x08U);
  const size_t data_end_pos = find_opcode_offset(buf, 0x0FU);
  REQUIRE(chunk_index_pos < buf.size());
  REQUIRE(data_end_pos < buf.size());
  REQUIRE(chunk_index_pos < data_end_pos);
}
```

这种测试特别适合文件格式类代码，因为它测的是**结构承诺**，而不是某个偶然生成出来的样本字节。以后你就算重构 `close()` 的内部代码，只要顺序被破坏，测试还是会立刻把你拉回来。

### CRC32 测试是真正在防“语义错但表面像对”

本章最有代表性的测试之一，是直接从 chunk payload 里读出 `stored_crc`，再对未压缩 chunk 数据重算一遍：

```cpp
TEST_CASE("McapWriter - CRC32 of chunk matches stored value") {
  ...
  const size_t chunk_pos = find_opcode_offset(buf, 0x06U);
  REQUIRE(chunk_pos < buf.size());

  const uint64_t payload_len = read_u64_le(buf, chunk_pos + 1U);
  const size_t payload_begin = chunk_pos + 9U;
  ...

  const uint32_t stored_crc = read_u32_le(buf, off);
  ...
  const uint64_t compressed_size = read_u64_le(buf, off);
  ...
  REQUIRE(compressed_size == uncompressed_size);

  const uint8_t* chunk_data = buf.data() + off;
  const uint32_t computed_crc = crc32_bytes(chunk_data, static_cast<size_t>(uncompressed_size));
  REQUIRE(stored_crc == computed_crc);
}
```

它验证的不是“文件里有个 CRC 字段”，而是“这个字段真的和 chunk 内容对应”。这两件事差别很大。很多文件格式 bug 都属于前者看起来对，后者其实已经错了。CRC32 这一层测试，正好把这种问题卡在 writer 侧。

### 压缩路径和 Sink 集成也要有项目内证据

压缩路径的测试没有去依赖外部 `mcap info`，而是直接比较启用 zstd 后文件尺寸是不是变小：

```cpp
TEST_CASE("McapWriter - zstd compression reduces size") {
  ...
  std::vector<uint8_t> uncompressed;
  auto writer_uncompressed = make_writer(uncompressed, McapCompression::None);
  write_messages(writer_uncompressed);
  REQUIRE(writer_uncompressed.close().has_value());

  std::vector<uint8_t> compressed;
  auto writer_compressed = make_writer(compressed, McapCompression::Zstd);
  write_messages(writer_compressed);
  REQUIRE(writer_compressed.close().has_value());

  REQUIRE(compressed.size() < uncompressed.size());
}
```

而 `McapWriterSink` 的集成测试则直接通过 `Context` 路由来写文件，最后检查结果文件里是否有 `Chunk` opcode：

```cpp
TEST_CASE("McapWriterSink - routes Context messages into MCAP") {
  constexpr const char* kPath = "/tmp/test_mcap_sink_output.mcap";

  McapWriterOptions opts;
  opts.use_chunks = true;

  auto sink_result = McapWriterSink::create(kPath, opts);
  REQUIRE(sink_result.has_value());
  auto sink = sink_result.value();

  auto context_result = Context::create();
  REQUIRE(context_result.has_value());
  auto context = std::move(context_result.value());
  context.add_sink(sink);
  ...
  channel.log(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), 123456ULL);
  REQUIRE(sink->close().has_value());
  ...
  REQUIRE(has_opcode(disk_data, 0x06U));
}
```

这段测试特别有价值，因为它证明了本章不是只把 writer 内部功能补齐，还把它真正接回了整个 SDK 的消息流。

### 本章的验证策略总结

这一章推荐的验证顺序是：

1. 先跑 `tests/test_mcap.cpp`，确认 chunk、index、CRC32、压缩、sink 相关行为都在仓库内锁住
2. 再跑整套 `ctest`，确认 full writer 没有破坏前面章节的行为
3. 如果你愿意，额外用外部 MCAP 工具做观察性检查也可以，但它只能算辅助理解，不是完成门槛

这也和本章前面的设计取向保持一致，**项目内验证才是教程主线，外部 CLI 绝不能变成“必须安装、必须成功”的前置条件。**

---

## 7b.5 与官方实现对比

这一节不是让你去追求“和官方库每一字节都长得一样”，而是帮你建立一个正确预期，教学版 full writer 走到了哪里，和官方 MCAP 生态相比还有哪些刻意保留的简化。

### 共同点，核心工程方向是一致的

本章实现已经和官方 MCAP 的主流思路对齐了几个关键点：

- 用 `Chunk` 作为压缩和索引的基本边界
- 把压缩算法名称写进 chunk 元数据，当前支持 `"zstd"`
- 用 `ChunkIndex` 在 summary 区域描述 chunk 的时间范围、偏移和大小
- 对未压缩 chunk 数据计算 CRC32，保证完整性校验有明确落点

这些点决定了我们写出的文件已经不是“只能教学演示的假格式”，而是沿着 MCAP 的真实结构在实现。

### 不同点，教学版保留了几处刻意简化

从 `src/mcap.cpp` 里的 `write_summary_if_needed()` 可以直接看出，当前实现还没有深入展开 message-level index：

```cpp
append_u16_u64_map(payload, {});
append_u64_le(payload, 0U);
```

再结合 `close()` 里的 footer 写法：

```cpp
append_u64_le(footer_payload, summary_start);
append_u64_le(footer_payload, 0U);
append_u32_le(footer_payload, 0U);
```

这表示当前章节的 full writer 重点放在：

- chunking 是否成立
- zstd 路径是否可用
- summary 中是否出现 `ChunkIndex`
- sink 路由是否打通

而不是一步把更细的 summary offset 和 footer CRC 等内容全部补满。

这种差异是合理的，因为教程目标不是复刻官方库的全部复杂度，而是让你先吃透最关键的演进层。等你真的需要兼容更丰富的索引形式，再继续补这些结构，心智负担会小得多。

### 官方工具可以帮助理解，但不是本章门槛

官方 reader、CLI、Studio 都能帮助你观察 MCAP 文件的结构，这很有价值。但本章的完成标准仍然只有一个，**项目内测试通过**。原因很现实，官方工具版本、安装方式和系统环境都可能不同，而教程要尽量保证读者在仓库内部就能复现、验证、定位问题。

所以更准确的说法应该是：

- 官方实现是对照视角，帮助你理解更完整的 MCAP 生态
- 本章实现是教学实现，强调结构清晰、验证可重复、易于继续演进
- 两者不必追求代码级完全一致，行为契约和测试证据才是关键

---

## 7b.6 打 tag，验证完成

这一章依旧沿用教程的标准完成流程，只是验证重点要明确回到项目内，而不是依赖额外安装某个外部工具。

```bash
# 1. 构建并运行测试，这是唯一的正确性标准
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag
git add .
git commit -m "feat(ch07b): add chunking compression indexing and sink integration"
git tag my-v0.7b-mcap-full
```

如果你想做**额外的参考对比**，可以再执行：

```bash
git diff v0.7b-mcap-full
```

这里比较的是**仓库提供的上游参考 tag `v0.7b-mcap-full`**，不是你刚打的本地 tag `my-v0.7b-mcap-full`。它的作用只是帮助你观察自己实现与参考实现之间的结构差异，不能替代 `ctest`，也不是完成门槛。

### 本章完成标准

你可以用下面这张清单快速自查：

- `ctest` 全部通过
- `tests/test_mcap.cpp` 中与 chunk、ChunkIndex、CRC32、zstd、sink 相关的测试全部通过
- `docs/tutorial/ch07b-mcap-full.md` 自身已经说明验证应以项目内测试为主
- 你能解释清楚 `use_chunks`、`chunk_size`、`compression`、`McapWriterSink` 四者各自负责什么

如果这几项都满足，就说明你已经完成了 Ch07b。至于外部 CLI 工具能不能额外观察到同样的结构，那是加分观察，不是过关前提。

---

## 本章小结

- **本章掌握了**：
  - 在 Ch07a 最小 writer 基础上，用 `McapWriterOptions` 打开完整写入路径
  - 通过 `write_message()` + `flush_chunk()` 把 message record 聚合成 chunk
  - 用 zstd 压缩 chunk，并在 chunk 元数据里记录压缩信息与未压缩 CRC32
  - 在 `close()` 阶段统一写出 `ChunkIndex` summary，而不是写消息时反复回头修补文件
  - 通过 `McapWriterSink` 把 MCAP 录制接回 `Context` / `Sink` 路由体系

- **工程知识点**：
  - `MCAP basic → full 的演进——增量优化的工程节奏`
  - `zstd 压缩级别与 chunk 大小的工程选择`

- **延伸练习**：
  - 试着把 `chunk_size` 分别改成 256 KiB、1 MiB、4 MiB，对同一批测试数据观察输出文件大小和 flush 次数，体会 seek 粒度与压缩率的平衡
  - 在 `tests/test_mcap.cpp` 里补一个更细的测试，检查 `ChunkIndex` 中记录的 `message_start_time` 和 `message_end_time` 是否与输入消息范围一致
  - 尝试扩展 footer 或 summary 相关字段，然后先写测试，再补实现，体验“在现有结构上继续演进”的成本是否可控

- **参考文档**：[
  `docs/07-MCAP文件写入.md`
](../07-MCAP文件写入.md)

从这一章结束开始，我们的 SDK 已经不只是“能实时推送消息”，也具备了更像真实系统的离线录制能力。下一章会继续往上走，把更高层的内置消息类型接进这条实时与离线并行的链路里。
