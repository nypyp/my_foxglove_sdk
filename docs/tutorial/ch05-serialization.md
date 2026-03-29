# Chapter 5：消息序列化，把结构体变成 Channel 能发送的字节

> **对应 tag**：`v0.5-serialization`
> **起点**：`v0.4-server`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `include/foxglove/messages.hpp` — 基础消息类型、`to_json`/`from_json` 声明、`encode<T>()` / `decode<T>()` 模板接口
> - `src/messages.cpp` — JSON 编解码实现、`encode_base64()`、NaN 和 `null` 之间的转换逻辑
> - `tests/test_messages.cpp` — roundtrip、golden file、默认值、错误处理与部分 schema/边界行为测试
> - `tests/golden/*.json` — 期望输出样本，锁定 JSON 字段顺序和精确文本格式
>
> **深入阅读**：[05-消息序列化.md](../05-消息序列化.md)
> **预计阅读时间**：55 分钟
> **前置要求**：完成第 4 章，理解 `RawChannel::log()` 只接收字节数组，以及 `FoxgloveResult<T>` 的错误返回方式

---

## 5.0 本章地图

前四章已经能把一段原始字节通过 `RawChannel` 送进 WebSocket 服务器，但业务代码还缺一层“把 C++ 结构体变成稳定字节格式”的桥梁。本章实现消息类型到 JSON 的双向转换，补齐 `encode<T>()` / `decode<T>()`、golden file 测试，以及教学版为什么先选 JSON 而不是 protobuf。读完后，你能把 `Pose`、`Color`、`CompressedImage` 这样的类型稳定地编码、解码并验证输出格式。

```text
业务结构体                 序列化层                     传输层
+------------------+      +-----------------------+      +-------------------+
| Pose / Color     | ---> | encode<T>()           | ---> | RawChannel::log() |
| Timestamp        |      | to_json / from_json   |      | uint8_t* + len    |
| CompressedImage  | <--- | decode<T>()           | <--- | messageData bytes |
+------------------+      +-----------------------+      +-------------------+
```

---

## 5.1 从需求出发

### `Channel::log()` 只认字节，不认结构体

第 3 章定义 `RawChannel` 时，我们故意把接口压到最小：

```cpp
// 概念上，Channel 只接收原始字节
channel.log(reinterpret_cast<const uint8_t*>(data.data()), data.size(), timestamp_ns);
```

这种设计有一个很现实的优点：传输层不需要理解业务类型。`WebSocketServer` 只负责把 `channel_id + timestamp + payload bytes` 按 Foxglove 协议发出去，它不该知道 `Pose` 有 `position` 和 `orientation`，更不该知道 `CompressedImage::data` 里装的是 JPEG 还是 PNG。

但问题也很直接：如果业务代码手里拿的是下面这样的结构体，怎么把它安全地喂给 `RawChannel`？

```cpp
Pose pose;
pose.position = {1.0, 2.0, 3.0};
pose.orientation = {0.0, 0.0, 0.0, 1.0};
```

你当然可以手写字符串：

```cpp
std::string json = R"({"position":{"x":1.0,"y":2.0,"z":3.0},...})";
```

可这马上会遇到三个工程问题。

1. **格式容易漂移**。字段顺序、空格、浮点数表示一旦不稳定，测试就很难写。
2. **读写逻辑会分叉**。编码时手写 JSON，解码时再手写解析，两个方向很容易不一致。
3. **边界值很麻烦**。例如浮点 NaN、二进制图像数据、以及“缺字段时是报错还是回落到默认值”这类行为，都需要统一约定。

所以这一章要做的，不只是“把结构体转成字符串”，而是建立一套稳定的序列化契约：

- 对外暴露统一入口 `encode<T>()` / `decode<T>()`
- 为每个消息类型定义清晰的 JSON 结构
- 让输出具有确定性，方便写 golden file
- 保持错误处理风格与前几章一致，失败时返回 `FoxgloveError::SerializationError`

### 为什么教学版先选 JSON，不选 protobuf

官方 Foxglove SDK 更偏向 protobuf，这是生产环境里更高效的选择。本教程这里先走 JSON，有意而为之，不是因为 JSON 更“先进”，而是因为它更适合把设计讲透。

原因有三条。

第一，**可见性高**。`{"x":1.0,"y":2.0,"z":3.0}` 一眼就能看懂，读者可以把注意力放在字段语义、稳定输出和错误处理上，而不是先学习 `.proto`、代码生成和 wire format。

第二，**调试成本低**。序列化后的内容直接就是文本，可以进 golden file，可以在测试失败时直接做字符串对比，也可以在第 4 章的网络链路里抓包观察。

第三，**更适合说明接口分层**。这一章最重要的教学目标，不是“学会某个库”，而是理解消息层怎样向上服务业务类型，向下服务字节通道。用 `nlohmann::json` 的 ADL 模式刚好能把这条链路讲清楚。

换句话说，本章选择 JSON 是为了把“序列化层的职责”讲明白。等第 8 章开始接入更复杂的内置消息类型时，你会更容易理解为什么真实 SDK 会进一步走向 protobuf 或其他更紧凑的二进制表示。

---

## 5.2 设计接口（先写头文件）

这一章最该先看的文件是 `include/foxglove/messages.hpp`。它做了三件事：定义基础消息结构体，声明 JSON 编解码函数，然后给出统一的模板入口。

### 先给读者一张文件地图

```text
include/foxglove/messages.hpp
├── Timestamp / Duration            时间相关基础类型
├── Vector3 / Quaternion / Pose     空间位姿类型
├── Color                           RGBA 颜色
├── Point3 / SceneUpdate / Log ...  更高层消息类型
├── detail::double_to_json()        NaN -> null 的内部辅助函数
├── to_json / from_json 声明        交给 nlohmann::json 的 ADL 机制发现
└── encode<T>() / decode<T>()       对上层最友好的统一入口
```

### 基础类型接口

先看本章主线里最重要的六个类型。

```cpp
// include/foxglove/messages.hpp

struct Timestamp {
  uint32_t sec = 0;
  uint32_t nsec = 0;

  static nlohmann::json json_schema();
};

struct Duration {
  int32_t sec = 0;
  uint32_t nsec = 0;

  static nlohmann::json json_schema();
};

struct Vector3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  static nlohmann::json json_schema();
};

struct Quaternion {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 1.0;

  static nlohmann::json json_schema();
};

struct Pose {
  Vector3 position;
  Quaternion orientation;

  static nlohmann::json json_schema();
};

struct Color {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 0.0;

  static nlohmann::json json_schema();
};
```

这里有几个接口层面的选择很值得先记住。

**一，结构体都很“平”。** 这些类型没有复杂成员函数，几乎只是字段加上 `json_schema()`。这让它们既适合直接拿来传值，也适合做测试输入。教程没有在这里引入 builder、校验器或者隐藏实现，因为这些类型的核心价值是“数据布局清楚”。

**二，默认值就是协议约定的一部分。** 例如 `Quaternion::w = 1.0`，表示单位四元数，也就是“无旋转”。这不是随手填的默认值，而是在让默认构造出来的 `Pose` 具有合理语义。

**三，`json_schema()` 和数据结构体放在一起。** 这让类型定义和它的模式描述不会漂离。你在同一个头文件里就能看见“这个类型长什么样”和“它序列化后该长什么样”。

### 比较运算和模板接口

继续往后看，头文件里还给这些基础类型定义了 `operator==`，以及统一的模板入口。

```cpp
inline bool operator==(const Vector3& lhs, const Vector3& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

inline bool operator==(const Pose& lhs, const Pose& rhs) {
  return lhs.position == rhs.position && lhs.orientation == rhs.orientation;
}

template<typename T>
FoxgloveResult<std::string> encode(const T& msg);

template<typename T>
FoxgloveResult<T> decode(const std::string& json_str);
```

为什么这一层要有 `operator==`？因为本章测试里最重要的一类断言是 roundtrip：先 `encode()`，再 `decode()`，最后比较对象是否还相等。提前把相等比较定义清楚，测试写起来才不会到处手敲字段对比。

而 `encode<T>()` / `decode<T>()` 的价值，在于给上层一个稳定入口。业务代码不需要知道底下具体用了 `nlohmann::json j = msg` 还是别的编码库，只知道：

- 成功时拿到 JSON 字符串或目标对象
- 失败时得到 `FoxgloveError::SerializationError`

这和前几章的 API 风格是统一的。

### `detail` 命名空间里的小机关

头文件后半段还有一组不该忽略的内部辅助函数：

```cpp
namespace detail {

inline nlohmann::json double_to_json(double value) {
  if (std::isnan(value)) {
    return nullptr;
  }
  return value;
}

inline double double_from_json(const nlohmann::json& j) {
  if (j.is_null()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return j.get<double>();
}

}  // namespace detail
```

它们被放进 `detail`，而不是做成 `double` 的全局 `to_json` / `from_json` 重载，原因很重要：如果你在 `foxglove` 命名空间里给 `double` 提供 ADL 重载，就会意外影响所有 `double` 的 JSON 序列化。现在这种写法把 NaN 处理限制在 Foxglove 自己的消息类型里，作用域更可控。

### 公开声明的 JSON 钩子

最后，头文件把 ADL 需要的函数声明都集中列出来了：

```cpp
void to_json(nlohmann::json& j, const Timestamp& ts);
void from_json(const nlohmann::json& j, Timestamp& ts);

void to_json(nlohmann::json& j, const Pose& pose);
void from_json(const nlohmann::json& j, Pose& pose);

void to_json(nlohmann::json& j, const CompressedImage& image);
void from_json(const nlohmann::json& j, CompressedImage& image);
```

这就是整章接口设计的中心思想：**上层看见的是类型和模板，下层靠 ADL 自动找到每种类型自己的编码规则。** 这种分层让你以后新增 `Log`、`SceneUpdate` 或者别的消息类型时，只需要补上该类型的 `to_json` / `from_json` 和 `json_schema()`，不需要改调用方。

> 💡 **🏗️ 设计决策 工程旁白：golden file 测试的适用边界与维护成本**
>
> golden file 很适合锁定“文本格式本身就是契约”的场景，本章就是典型例子。我们关心的不只是 `Pose` 能不能成功解码，还关心输出 JSON 的字段顺序、嵌套结构、数值文本表现是否稳定，因为这些都会直接影响抓包、日志排查和与外部工具的互通。把期望输出单独放进 `tests/golden/*.json`，测试失败时一眼就能看出是格式漂移还是值错误。
>
> 但 golden file 不是银弹。如果输出里掺杂时间戳、随机 ID、平台相关路径，golden file 会非常脆弱，维护成本会迅速升高。遇到这类场景，更稳妥的策略往往是“先解析，再断言关键字段”，而不是整段文本逐字符匹配。判断标准只有一个：你要锁定的是语义，还是连外部展示格式都要锁定。本章选 golden file，是因为 JSON 文本格式本身就属于要保护的契约。

---

## 5.3 实现核心逻辑

头文件确定以后，`src/messages.cpp` 负责把这些声明变成真正可运行的实现。整章实现可以拆成四块：统一模板入口、每个类型的 `to_json` / `from_json`、二进制字段的 base64 处理、以及 NaN 和 `null` 的互转。

### 模板统一入口，把异常收口成 `FoxgloveResult`

先看文件最前面这组模板实现：

```cpp
template<typename T>
FoxgloveResult<std::string> encode_impl(const T& msg) {
  try {
    nlohmann::json j = msg;
    return j.dump();
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}

template<typename T>
FoxgloveResult<T> decode_impl(const std::string& json_str) {
  try {
    nlohmann::json j = nlohmann::json::parse(json_str);
    T msg = j.get<T>();
    return msg;
  } catch (const std::exception&) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
}
```

这里最关键的动作不是 `j.dump()`，而是“把底层库抛出的异常转换成项目自己的错误枚举”。这保证了调用者始终沿用前几章建立起来的处理方式，不需要混着处理 `std::exception` 和 `FoxgloveError` 两套体系。

`nlohmann::json j = msg` 这一句也很值得注意，它没有显式写 `to_json(j, msg)`，因为 ADL 会根据 `msg` 的类型在 `foxglove` 命名空间中自动找到对应重载。这正是第 5.2 节提前把声明放在头文件里的意义。

### 基础类型的 `to_json` / `from_json`

看两个最基础的时间类型：

```cpp
void to_json(nlohmann::json& j, const Timestamp& ts) {
  j = nlohmann::json{{"nsec", ts.nsec}, {"sec", ts.sec}};
}

void from_json(const nlohmann::json& j, Timestamp& ts) {
  j.at("nsec").get_to(ts.nsec);
  j.at("sec").get_to(ts.sec);
}

void to_json(nlohmann::json& j, const Duration& dur) {
  j = nlohmann::json{{"nsec", dur.nsec}, {"sec", dur.sec}};
}

void from_json(const nlohmann::json& j, Duration& dur) {
  j.at("nsec").get_to(dur.nsec);
  j.at("sec").get_to(dur.sec);
}
```

这里故意把 `nsec` 放在 `sec` 前面，不是随手一写，而是在配合**本项目当前实现里**对 JSON 稳定输出的约定。相同的考虑也出现在 `Pose` 和 `Color` 这类复合类型里。

```cpp
void to_json(nlohmann::json& j, const Pose& pose) {
  j = nlohmann::json{{"orientation", pose.orientation}, {"position", pose.position}};
}

void from_json(const nlohmann::json& j, Pose& pose) {
  j.at("orientation").get_to(pose.orientation);
  j.at("position").get_to(pose.position);
}

void to_json(nlohmann::json& j, const Color& color) {
  j = nlohmann::json{
    {"a", detail::double_to_json(color.a)},
    {"b", detail::double_to_json(color.b)},
    {"g", detail::double_to_json(color.g)},
    {"r", detail::double_to_json(color.r)}
  };
}
```

为什么 `Pose` 里是 `orientation` 在前，`position` 在后？因为按字母序，`orientation` 就该排在 `position` 前面。你可以在 golden file 里直接看到这个结果：

```json
{"orientation":{"w":1.0,"x":0.0,"y":0.0,"z":0.0},"position":{"x":1.0,"y":2.0,"z":3.0}}
```

同理，`Color` 的字段顺序是 `a`、`b`、`g`、`r`，不是更“符合人类习惯”的 `r`、`g`、`b`、`a`。这看起来有点别扭，但换来的是确定性输出，适合 golden file 精确比对。`tests/golden/color.json` 里锁定的正是这一点：

```json
{"a":1.0,"b":0.0,"g":1.0,"r":1.0}
```

### NaN 和 `null` 的互转

接着看浮点类型的特殊处理。`Vector3` 和 `Quaternion` 都没有直接把 `double` 塞进 JSON，而是先过一层 `detail::double_to_json()`：

```cpp
void to_json(nlohmann::json& j, const Vector3& vec) {
  j = nlohmann::json{
    {"x", detail::double_to_json(vec.x)},
    {"y", detail::double_to_json(vec.y)},
    {"z", detail::double_to_json(vec.z)}
  };
}

void from_json(const nlohmann::json& j, Vector3& vec) {
  vec.x = detail::double_from_json(j.at("x"));
  vec.y = detail::double_from_json(j.at("y"));
  vec.z = detail::double_from_json(j.at("z"));
}
```

原因是 JSON 标准并不支持 `NaN` 这个字面值。如果直接把 `std::nan("")` 序列化出去，你得到的很可能是非标准 JSON，或者不同解析器给出不同结果。本项目采取的约定是：

- 编码时，NaN 统一写成 JSON `null`
- 解码时，JSON `null` 再还原回 `quiet_NaN()`

这样既保住了 JSON 的标准兼容性，也保住了数值语义上的“未知值”表达能力。后面的测试会专门锁定这个约定。

### 二进制字段要先做 base64

再看 `CompressedImage` 这一类带二进制 payload 的消息。JSON 本身不能直接承载原始字节，所以 `src/messages.cpp` 里提供了 `encode_base64()` 和 `decode_base64()`。

```cpp
std::string encode_base64(const std::vector<uint8_t>& data) {
  static constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string output;
  output.reserve(((data.size() + 2U) / 3U) * 4U);

  size_t index = 0;
  while (index + 3U <= data.size()) {
    const uint32_t chunk = (static_cast<uint32_t>(data[index]) << 16U) |
                           (static_cast<uint32_t>(data[index + 1U]) << 8U) |
                           static_cast<uint32_t>(data[index + 2U]);
    output.push_back(kEncodeTable[(chunk >> 18U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 12U) & 0x3FU]);
    output.push_back(kEncodeTable[(chunk >> 6U) & 0x3FU]);
    output.push_back(kEncodeTable[chunk & 0x3FU]);
    index += 3U;
  }
  // ... 余数和 '=' padding 处理，完整代码见仓库 tag
  return output;
}
```

这段逻辑的职责很单纯，就是把 `std::vector<uint8_t>` 编成标准 base64 文本。于是 `CompressedImage` 的 golden file 长这样：

```json
{"data":"/9j/2wABAg==","format":"jpeg","frame_id":"camera","timestamp":{"nsec":10,"sec":9}}
```

这里 `data` 已经不是原始图像字节，而是 base64 文本。这样它才能在 JSON 里稳定传输，同时保持跨语言兼容。

### 为什么字母序输出对 golden file 至关重要

这章的 golden file 能成立，有个前提是：同一个对象每次编码都必须产生同一个字符串。比如 `Timestamp{1234567890, 123456789}` 对应的 golden file 是：

```json
{"nsec":123456789,"sec":1234567890}
```

如果某次输出成 `{"sec":1234567890,"nsec":123456789}`，从语义上看它没错，但字符串已经不同，golden file 测试就会失败。正因为如此，`to_json` 里的字段排列不是风格问题，而是测试契约的一部分。

这一点也解释了为什么本章没有把 golden file 测试只当成“锦上添花”。对于 JSON 输出层来说，**确定性文本格式就是产品行为的一部分**。

> 💡 **⚠️ 常见陷阱 工程旁白：JSON 序列化的性能陷阱**
>
> JSON 最大的问题不是“慢”这么简单，而是它常常在你没注意的地方产生额外分配和拷贝。`nlohmann::json j = msg` 会先构建一棵动态对象树，`j.dump()` 又会再生成一份字符串；如果消息里还带大块二进制数据，比如 `CompressedImage::data`，你还会再经历一次 base64 扩容，体积大约增加三分之一。教学版里这样做很合理，因为它换来了可读性和极低的调试门槛。
>
> 真到高频、大包场景，问题就会暴露出来。第一类优化是减少中间对象，比如避免不必要的临时 `json` 复制。第二类优化是改协议，直接改用 protobuf 或别的紧凑编码，把二进制字段留在原始字节层，不再经由 base64。第三类优化是分层缓存，例如 schema、固定字段模板和复用缓冲区。本章不做这些优化，是为了先把接口契约和测试方法立住。等性能成为主要矛盾时，再换编码格式也不迟。

---

## 5.4 测试：验证正确性

这一章的测试策略要先讲清楚，再看测试代码。因为我们要验证的不是单一风险，而是两类完全不同的风险。

### 我们到底要验证什么

第一类风险是**值语义是否正确**。也就是一个对象编码后再解码，字段还能不能保持一致。这个风险用 roundtrip 测试最合适。

第二类风险是**文本输出是否稳定**。即便 roundtrip 通过，仍可能出现字段顺序变了、base64 文本变了、NaN 表示方式漂了。这个风险需要 golden file 测试来锁定。

所以本章把测试拆成两个主轴：

- roundtrip 测试，保护“编码和解码互相一致”
- golden file 测试，保护“输出文本稳定且符合协议约定”

再辅以默认值、schema、错误处理和 NaN 特殊行为的测试，补齐边界条件。

### roundtrip 测试，锁定值语义

`tests/test_messages.cpp` 里最基础的一类用例长这样：

```cpp
TEST_CASE("Pose - roundtrip serialization") {
  Pose pose;
  pose.position.x = 1.0;
  pose.position.y = 2.0;
  pose.position.z = 3.0;
  pose.orientation.x = 0.0;
  pose.orientation.y = 0.0;
  pose.orientation.z = 0.0;
  pose.orientation.w = 1.0;

  auto encoded = encode(pose);
  REQUIRE(encoded.has_value());

  auto decoded = decode<Pose>(encoded.value());
  REQUIRE(decoded.has_value());

  REQUIRE(decoded.value().position.x == pose.position.x);
  REQUIRE(decoded.value().position.y == pose.position.y);
  REQUIRE(decoded.value().position.z == pose.position.z);
  REQUIRE(decoded.value().orientation.w == pose.orientation.w);
}
```

这种测试回答的问题是：我们写的 `to_json` 和 `from_json` 是否彼此匹配。它特别适合发现以下错误：

- 某个字段编码了但解码漏掉了
- 字段名拼错，导致解码取错键
- 类型映射不对，比如 `int32_t` 和 `uint32_t` 搞混

不过它也有明显盲区。只要编码和解码“错得一致”，roundtrip 仍然可能通过。所以它不能替代 golden file。

### golden file 测试，锁定精确文本输出

看另一类用例：

```cpp
std::string read_golden_file(const std::string& filename) {
  std::ifstream file(filename);
  REQUIRE(file.is_open());
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  while (!content.empty() &&
         (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
    content.pop_back();
  }
  return content;
}

TEST_CASE("Timestamp - golden file match") {
  Timestamp ts;
  ts.sec = 1234567890;
  ts.nsec = 123456789;

  auto encoded = encode(ts);
  REQUIRE(encoded.has_value());

  std::string expected = read_golden_file(golden_path("timestamp.json"));
  REQUIRE(encoded.value() == expected);
}
```

`tests/golden/timestamp.json` 内容只有一行：

```json
{"nsec":123456789,"sec":1234567890}
```

这样的测试会精确卡住以下行为：

- 字段顺序必须稳定
- 输出里不能多空格、少字段或换成别的数值表示
- 嵌套对象结构必须和预期完全一致

对 `Pose`、`Color`、`Vector3` 等基础类型，本章都用了这种方式。工作树里的 `tests/golden/` 目前不止六个文件，还包含 `compressed_image.json`、`scene_update.json` 等更复杂消息的期望输出。要注意：**这些 richer 类型的 golden 文件在当前仓库里更像参考样本库，而不是都已经在 `tests/test_messages.cpp` 中获得同等强度的自动回归覆盖**。它们一方面展示“这类消息编码后长什么样”，另一方面为后续章节继续补测试提供稳定基线。

### NaN 和错误处理也要单独测试

本章还有两个很关键的边界测试。

第一个是 NaN：

```cpp
TEST_CASE("NaN handling - serializes to null") {
  Vector3 vec;
  vec.x = std::nan("");
  vec.y = 2.0;
  vec.z = 3.0;

  auto encoded = encode(vec);
  REQUIRE(encoded.has_value());
  REQUIRE(encoded.value().find("null") != std::string::npos);
}

TEST_CASE("NaN handling - deserializes null back to NaN") {
  std::string json = R"({"x":null,"y":2.0,"z":3.0})";

  auto decoded = decode<Vector3>(json);
  REQUIRE(decoded.has_value());
  REQUIRE(std::isnan(decoded.value().x));
}
```

它锁定的是一条协议级约定，而不是普通字段值。没有这组测试，别人以后很可能把 `detail::double_to_json()` 改掉却不自知。

第二个是错误处理：

```cpp
TEST_CASE("Decode - invalid JSON returns error") {
  std::string invalid_json = "{invalid json";
  auto result = decode<Vector3>(invalid_json);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::SerializationError);
}

TEST_CASE("Decode - missing required field returns error") {
  std::string incomplete_json = R"({"x":1.0,"y":2.0})";
  auto result = decode<Vector3>(incomplete_json);
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == FoxgloveError::SerializationError);
}
```

这里验证的不是 `nlohmann::json` 会不会抛异常，而是**我们的 API 有没有把异常正确收口成 Foxglove 项目自己的错误语义**。同时要提醒一句：本章里“缺字段一定报错”并不是对所有消息类型都成立的总规则。像 `Vector3` 这类基础类型因为使用 `j.at(...)`，缺字段会直接走错误路径；而部分更高层类型在当前实现里会先 `contains(...)` 再决定是否保留默认值。这也是为什么你在阅读 richer 类型时，要把“默认值策略”和“错误策略”分开理解。

### 为什么 golden file 正适合这章

这一章明确选择 JSON 序列化，所以 golden file 就不是附加手段，而是主测试策略的一部分。原因很简单：JSON 的外观本身就是和外部世界交互的接口。Foxglove Studio、调试日志、抓包工具、示例文档，看到的都是最终文本，不是内部对象树。

也正因为这样，roundtrip 和 golden file 不能互相替代。

- 只有 roundtrip，没有 golden file，你会放过很多“语义没错、格式漂了”的回归。
- 只有 golden file，没有 roundtrip，你会缺少对解码路径和对象一致性的覆盖。

两者组合起来，才刚好覆盖这一章最核心的风险面。

---

## 5.5 与官方实现对比

这一节不能省，因为它能帮助读者把“教程里的教学简化”与“真实 SDK 的工程取舍”区分开。

### 官方为什么更偏向 protobuf

官方 Foxglove SDK 面向的是生产环境里的真实数据流，典型场景包括更大消息体、更高发送频率、跨语言互通和更严格的带宽约束。在这些场景里，protobuf 相比 JSON 有天然优势：

- 消息体更紧凑，不需要字段名重复出现
- 二进制字段不需要 base64，避免额外膨胀
- schema 和代码生成链路更完整，跨语言体验更好
- 编解码开销通常更低，特别是高频路径

所以，如果你的目标是“尽量接近官方生产实现”，JSON 并不是最终答案。

### 但教程版为什么故意不跟官方完全一样

本教程当前的目标不是追求最强吞吐，而是让读者能从源码里直接看见消息长什么样、序列化规则如何落地、测试为什么能兜底。为了这个教学目标，JSON 的收益非常具体：

- 你可以直接打开 `tests/golden/*.json` 理解输出
- 你可以肉眼检查字段排序和 NaN 表示方式
- 你不用先引入 `.proto`、生成代码和额外构建链条，就能专注看接口分层

换句话说，官方实现和教程实现追求的是不同阶段的最优解。一个优先生产指标，一个优先可读性与可学习性。

### 两者的共同点比表面差异更重要

虽然编码格式不同，但两者在设计目标上仍然有明显共通之处。

| 维度 | 本教程实现 | 官方实现的典型方向 |
|------|------------|--------------------|
| 主要编码格式 | JSON | protobuf |
| 上层使用方式 | `encode<T>()` / `decode<T>()` | 由消息类型或生成代码提供编码入口 |
| 消息类型建模 | C++ 结构体 + 明确字段默认值 | 同样需要清晰的数据模型和 schema |
| 测试重点 | roundtrip + golden file | roundtrip、兼容性、跨语言和性能验证 |
| 二进制数据处理 | 先 base64 再写入 JSON | 通常保留为原始二进制字段 |

真正值得学的是背后的工程思想，而不是死记“官方用 protobuf，所以 JSON 就错了”。本章先把这些思想讲清楚，后面你再看更复杂的消息体系，就知道哪些地方是为了教学做的简化，哪些地方是任何实现都绕不开的刚性约束。

---

## 5.6 打 tag，验证完成

以下命令为标准完成流程，所有章节统一：

```bash
# 1. 构建并运行测试（这是唯一的正确性标准）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag（my- 前缀避免与仓库参考 tag 冲突）
git add .
git commit -m "feat(ch05): add JSON message serialization layer"
git tag my-v0.5-serialization

# 3. 与参考实现对比（辅助理解，非强制门槛）
git diff v0.5-serialization
```

**完成标准**：`ctest` 全部通过是硬性门槛。`git diff v0.5-serialization` 可能仍然有代码风格、注释组织、局部实现顺序等差异，这是正常的。只要测试通过，你的实现就是正确的。diff 的意义是帮助你观察参考实现在结构化、命名或边界处理上的选择，而不是逼你逐行抄到完全一致。

对本章来说，建议你额外做两件事再宣布完成：

1. 手动打开一两个 golden file，看自己输出的字段顺序是否稳定。
2. 随手构造一个带 NaN 的 `Vector3`，确认编码结果里出现的是 `null` 而不是非标准 JSON 字面值。

---

## 本章小结

- **本章掌握了**：
  - 为什么 `RawChannel` 只接受字节数组后，系统必须补上一层独立的消息序列化层
  - 如何用 `to_json` / `from_json` + ADL 给 C++ 结构体定义稳定的 JSON 编解码规则
  - 为什么教学版先选择 JSON，它在可读性、调试和教学节奏上的具体优势是什么
  - golden file 和 roundtrip 测试分别在保护什么风险，为什么两者都不能少
  - NaN、base64 二进制字段、字母序输出这些看似细节的约定，为什么都属于序列化契约的一部分

- **工程知识点**：
  - golden file 测试的适用边界与维护成本
  - JSON 序列化的性能陷阱

- **延伸练习**：
  - 给 `CompressedImage` 增加一组 roundtrip 测试，观察 base64 编码后的 JSON 大小变化，体会文本协议对二进制数据的额外成本。
  - 随机打乱某个 `to_json` 里的字段顺序，再跑 golden file 测试，亲手感受“语义没变但契约破坏”的回归长什么样。
  - 尝试给一个新类型补上 `json_schema()`、`to_json`、`from_json` 和测试，练习这一章的扩展方式。

- **参考文档**：[05-消息序列化.md](../05-消息序列化.md)
