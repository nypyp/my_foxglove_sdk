# 第七章：MCAP 文件写入（Phase 2）

本章在 Phase 1 的最小可用写入器基础上，继续实现可用于“真实录制流程”的 MCAP 写入能力：

- 分块（Chunk）写入
- zstd 压缩（通过系统 `pkg-config` 引入 `libzstd`）
- ChunkIndex 索引记录
- Chunk 级 CRC32 数据完整性校验
- `McapWriterSink`（将 Context/Channel 消息自动落盘到 MCAP）

你可以把这一章理解为：从“能写文件”升级到“能高效写大文件，并具备可检索能力”。

---

## 1. MCAP二进制格式概览

MCAP 是记录型二进制容器。文件一般由以下部分组成：

1. 文件头 magic
2. Header 记录（profile/library）
3. Data Section（Schema/Channel/Message 或 Chunk）
4. Summary Section（可选，常见是索引）
5. Footer（summary 起始偏移、CRC 等）
6. 文件尾 magic

本章我们重点新增两类记录：

- `Chunk`（opcode `0x06`）
- `ChunkIndex`（opcode `0x08`）

并且支持：

- Chunk 内数据可压缩（none / zstd）
- Chunk 保存 `uncompressed_crc`

在教学版实现中，我们采用“够用且易懂”的策略：

- 不实现 MessageIndex 明细表（`message_index_offsets` 为空，`message_index_length=0`）
- Footer 的 `summary_offset_start` 和 `crc` 先固定为 0

这与“先跑通，再完善”的章节节奏一致。

---

## 2. 基础写入器回顾

Phase 1 的最小写入器已经支持：

- `add_schema()`
- `add_channel()`
- `write_message()`
- `close()` 写 DataEnd + Footer + 尾 magic

当 `use_chunks=false` 时，Message 记录直接写入 Data Section。

代码风格核心是：

- 所有整数按 little-endian 写入
- 字符串统一使用 `uint32 length + bytes`
- 每条 record 都是 `opcode + uint64 record_length + payload`

示例（概念代码）：

```cpp
std::vector<uint8_t> record;
record.push_back(opcode);
append_u64_le(record, payload.size());
append_bytes(record, payload.data(), payload.size());
```

本章新增能力不会破坏上述基础写入路径。

---

## 3. 分块策略

分块的目标：

1. 降低大文件写入和压缩的内存峰值
2. 为索引与随机访问提供天然边界
3. 让压缩比和读取效率达到平衡

### 3.1 缓冲模型

当 `McapWriterOptions.use_chunks=true` 时：

- Message 不直接写入最终输出
- 先编码成完整 Message record
- 追加到 `chunk_data` 内存缓冲

并维护：

- `chunk_start_time`：当前块最小 `log_time`
- `chunk_end_time`：当前块最大 `log_time`

### 3.2 flush 时机

触发 flush 的两个时机：

1. `chunk_data.size() >= chunk_size`
2. `close()` 时若有剩余数据

flush 过程：

1. 计算未压缩长度与 CRC32
2. 按配置执行压缩（或不压缩）
3. 写 Chunk record
4. 记录 ChunkIndexEntry（用于后续 Summary）
5. 清空 chunk 缓冲

这套流程对应一个典型的“写时聚合 + 收尾写索引”设计。

---

## 4. zstd压缩集成

### 4.1 构建系统

本项目不使用 FetchContent 拉取 zstd，而是走系统库：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
```

并在主库链接：

```cmake
target_link_libraries(
  foxglove PUBLIC
  PkgConfig::ZSTD
)
```

这样可以直接复用系统安装的 `libzstd`（例如 1.5.5）。

### 4.2 压缩实现

压缩分支（概念代码）：

```cpp
std::vector<uint8_t> compressed;
compressed.resize(ZSTD_compressBound(src.size()));
size_t out = ZSTD_compress(
  compressed.data(), compressed.size(),
  src.data(), src.size(),
  1
);
if (ZSTD_isError(out)) {
  return tl::make_unexpected(FoxgloveError::IoError);
}
compressed.resize(out);
```

当 `McapCompression::None`：

- `compression` 字段写空字符串 `""`
- `compressed_data` 直接等于原始 chunk 数据

当 `McapCompression::Zstd`：

- `compression` 字段写 `"zstd"`
- `compressed_data` 写压缩输出

---

## 5. CRC32数据完整性

Chunk payload 的 `uncompressed_crc` 用于校验“解压后数据一致性”。

实现采用内联算法（不引入额外依赖）：

```cpp
static uint32_t crc32_compute(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
  }
  return ~crc;
}
```

注意：CRC 计算对象是**未压缩 chunk 数据**，而不是压缩后的字节流。

测试中我们会解析 Chunk record：

1. 取出 stored CRC
2. 定位 chunk data
3. 重算 CRC
4. 比较一致

这能有效避免“写入字段正确但语义错误”的问题。

---

## 6. 索引记录与可寻址性

Chunk 写完后，我们保存必要的索引信息：

- `message_start_time`
- `message_end_time`
- `chunk_start_offset`
- `chunk_length`
- `compression`
- `compressed_size`
- `uncompressed_size`

在 `close()` 阶段，按顺序写出所有 `ChunkIndex`。

### 6.1 Summary 与 DataEnd 顺序

本章实现中，`close()` 的顺序为：

1. flush 最后一块
2. 写所有 ChunkIndex（Summary）
3. 写 DataEnd
4. 写 Footer
5. 写尾 magic

并将 Footer 的 `summary_start` 指向第一条 ChunkIndex 的偏移。

这种顺序与本项目测试约定一致（验证 ChunkIndex 出现在 DataEnd 之前）。

### 6.2 Footer 策略

- `use_chunks=true`：`summary_start` 指向 Summary 起点
- `use_chunks=false`：沿用最小实现，Footer 字段保持 0

`summary_offset_start` 与 `crc` 在当前阶段固定为 0，后续章节可继续扩展。

---

## 7. McapWriterSink：Context适配器

`McapWriterSink` 让 MCAP 写入器可以直接挂到 `Context`：

- channel 创建时自动注册 schema/channel
- channel 收到消息时自动写入 MCAP

这样应用层无需手写“每条消息都构造 McapMessage”的样板代码。

### 7.1 创建方式

```cpp
auto sink_result = foxglove::McapWriterSink::create("recording.mcap", options);
if (!sink_result.has_value()) {
  return;
}
auto sink = sink_result.value();
context.add_sink(sink);
```

### 7.2 通道映射

`on_channel_added(RawChannel&)` 中：

1. 从 `RawChannel::descriptor()` 读 topic/encoding/schema
2. `writer_.add_schema(...)`
3. `writer_.add_channel(...)`
4. 记录 `context channel id -> mcap channel id` 映射

`on_message(...)` 中按映射构造：

```cpp
McapMessage msg{mcap_channel_id, sequence_++, log_time, log_time, data, len};
writer_.write_message(msg);
```

`on_channel_removed(...)` 在本阶段可为 no-op。

### 7.3 线程安全

`McapWriterSink` 使用 `std::mutex` 保护：

- channel_map_
- sequence_
- writer_ 调用

这与 Context 的并发路由模型兼容。

---

## 8. 与官方对比

| 维度 | 教学版实现 | 官方 MCAP Writer（总体能力） |
|---|---|---|
| 分块写入 | ✅ | ✅ |
| zstd 压缩 | ✅（基础级） | ✅（更完整配置） |
| Chunk CRC32 | ✅ | ✅ |
| ChunkIndex | ✅（MessageIndex 空） | ✅（含更完整索引） |
| Summary Offset Section | 暂未实现 | ✅ |
| Footer CRC | 固定 0 | ✅ |

我们刻意保留“可增量扩展”的实现边界：

- 先把可用主干打通
- 再在后续章节补更细节的索引和校验

这种策略特别适合教学与迭代开发。

---

## 9. 示例程序

`examples/ch07_mcap/main.cpp` 已扩展为两段演示：

1. 最小写入：`output.mcap`
2. 分块 + zstd：`output_chunked_zstd.mcap`

关键配置如下：

```cpp
foxglove::McapWriterOptions options;
options.use_chunks = true;
options.compression = foxglove::McapCompression::Zstd;
options.chunk_size = 4 * 1024;
```

运行后可以对比两个文件大小，观察压缩收益。

### 与 Context 联动示例

```cpp
auto context_result = foxglove::Context::create();
auto sink_result = foxglove::McapWriterSink::create("ctx.mcap", options);
if (context_result.has_value() && sink_result.has_value()) {
  auto context = std::move(context_result.value());
  context.add_sink(sink_result.value());
}
```

在此模式下，业务代码只需 `channel.log(...)`，录制自动完成。

---

## 10. 下一步

在下一阶段，你可以继续增强：

1. MessageIndex 写入与 `message_index_offsets` 填充
2. Summary Offset Section（提升解析速度）
3. Footer CRC 与更严格的一致性校验
4. 可配置压缩级别和策略（速度优先 / 比率优先）
5. 更精细的错误类型（区分压缩失败、I/O 失败、格式失败）

完成这些后，教学版写入器将更接近生产级 MCAP writer 的结构。

---

## 附：本章检查清单

- [x] CMake 通过 `pkg-config` 找到 `libzstd`
- [x] `McapWriterOptions` 支持 chunk/compression/chunk_size
- [x] 写入 Chunk（0x06）
- [x] 写入 ChunkIndex（0x08）
- [x] 写入 `uncompressed_crc`
- [x] zstd 压缩路径可用
- [x] `McapWriterSink` 可从 Context 接收消息
- [x] 单测覆盖 chunk/index/compress/crc/sink

这意味着 Chapter 7 Phase 2 的目标已经形成闭环：

“能写、能压、能索引、能校验、能接 Context”。
