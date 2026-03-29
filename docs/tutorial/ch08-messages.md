# Chapter 8：内置消息类型，把基础序列化升级成可直接发布的业务语义

> **对应 tag**：`v0.8-messages`
> **起点**：`v0.7b-mcap-full`（上一章完成时的 tag）
> **本章新增/修改文件**：
> - `include/foxglove/messages.hpp` — 定义复合消息类型、`TypedChannel<T>` 模板，以及 `SceneUpdateChannel` / `FrameTransformChannel` / `LogChannel` 别名
> - `src/messages.cpp` — 实现复杂类型的 `to_json` / `from_json`、base64 编解码、枚举整数序列化与模板特化
> - `tests/test_messages.cpp` — 用 roundtrip、golden file 和 schema 测试锁定基础消息层行为（基础类型已覆盖；复杂类型如 `SceneUpdate`、`FrameTransform` 的扩展测试作为延伸练习留给读者补充）
> - `tests/golden/*.json` — 保存复杂消息的期望 JSON 文本，锁定字段顺序和精确输出格式
>
> **深入阅读**：[08-内置消息类型.md](../08-内置消息类型.md)
> **预计阅读时间**：65 分钟
> **前置要求**：完成 Ch05 和 Ch07b，已经理解 `encode<T>()` / `decode<T>()` 的 JSON 路径，以及 `RawChannel::log()` 只接收 `uint8_t* + len`

---

## 8.0 本章地图

第五章已经建立了“结构体 <-> JSON 字符串”的基础序列化层，但它主要处理 `Vector3`、`Pose`、`Color` 这类基础积木。本章把这些积木组合成 `SceneUpdate`、`FrameTransform`、`Log`、`CompressedImage` 等可直接驱动 Foxglove Studio 的业务消息，并在 `RawChannel` 上包一层 `TypedChannel<T>`。读完后，你会清楚消息层怎样站在 Ch05 之上，把“能编码”推进到“能表达业务语义、能安全发布”。

```text
Ch05：基础消息 -> JSON
    |
    v
Ch08：复合消息建模
    |
    +--> SceneUpdate        场景实体的新增、更新、删除
    +--> FrameTransform     坐标系之间的关系
    +--> Log                运行时日志
    +--> CompressedImage    带 base64 的图像负载
    |
    v
TypedChannel<T>
    |
    v
encode(msg) -> std::string -> RawChannel::log(bytes, len, timestamp_ns)
```

如果把 Ch05 看成“怎么序列化”，那 Ch08 解决的就是“该序列化什么”。而且这里的答案不再是基础数学类型，而是带业务语义的复合结构。第九章会把这批类型真正挂到端到端链路里跑起来。

---

## 8.1 从需求出发

### 只有 `Vector3` 和 `Pose`，还不足以做三维可视化

第五章结束时，我们已经能写出下面这种代码：

```cpp
Pose pose;
pose.position = {1.0, 2.0, 3.0};
pose.orientation = {0.0, 0.0, 0.0, 1.0};

auto json = encode(pose);
```

这当然很有用，但它离“Foxglove Studio 能直接消费的业务对象”还差一层语义。

举几个直观例子。

- 你想在 3D 视图里画一个立方体，不只是需要位置和姿态，还需要 `size`、`color`，而且这个立方体还得属于某个 entity。
- 你想表达一个场景增量更新，不只是发一个 `Pose`，而是要同时描述“新增了哪些实体，删除了哪些实体”。
- 你想发布 TF 树，不只是发一个 `Vector3` 和 `Quaternion`，而是要显式带上 `parent_frame_id` 和 `child_frame_id`。
- 你想记录日志或压缩图像，也不能靠现有的基础结构体硬拼字符串。

所以从系统设计角度看，Ch05 提供的是**序列化能力**，而 Ch08 需要补的是**业务语义层**。没有这层，调用方会被迫在业务代码里自己组装 JSON，马上就会遇到几个问题。

1. **字段命名不统一。** 有人写 `frameId`，有人写 `frame_id`，有人用 `pose.position`，有人拆成 `x/y/z`。
2. **类型边界变松。** 画立方体和发坐标变换本来是两个不同语义，但如果都用匿名 JSON 对象，编译器根本帮不上忙。
3. **测试很难收敛。** 没有统一结构体，golden file 只能锁定某一段业务代码的输出，锁不住公共契约。

这就是为什么 Foxglove 体系里会有“built-in message types”。它们不是为了让类型数量变多，而是把常见可视化语义变成稳定约定。对教程版来说，我们仍然沿用 Ch05 的 JSON path，而不是直接跨到 protobuf，目的就是把这层约定讲清楚。

### Ch08 解决的是“组合后的语义对象”

这章会出现三类新东西。

第一类是 **primitive**，比如 `CubePrimitive`、`SpherePrimitive`、`LinePrimitive`。它们把基础几何和颜色组合起来，变成一个个可绘制部件。

第二类是 **entity/update 层**，比如 `SceneEntity`、`SceneEntityDeletion`、`SceneUpdate`。这层负责告诉系统“场景里有哪些对象，现在要新增谁、删除谁”。

第三类是 **高频业务消息**，比如 `FrameTransform`、`Log`、`CompressedImage`。它们各自服务一个很明确的使用场景。

而 `TypedChannel<T>` 则是把这些类型和第三章的 `RawChannel` 接起来。它不改变底层仍然发送字节流这个事实，但它把“先 `encode()`，再 `reinterpret_cast`，再 `log()`”这段样板代码封装掉，让调用方更不容易出错。

---

## 8.2 设计接口（先写头文件）

这一章的公开接口主要都在 `include/foxglove/messages.hpp`。可以把它看成在 Ch05 的基础上继续扩容，只不过这次从基础数学类型走到了具象业务类型。

### 先看几组关键结构体

先看几种最有代表性的复合消息。下面这段摘录能看到这一章的建模思路，几乎全部是“由已有基础类型继续组合”。

```cpp
struct CubePrimitive {
  Pose pose;
  Vector3 size;
  Color color;

  static nlohmann::json json_schema();
};

struct SpherePrimitive {
  Pose pose;
  Vector3 size;
  Color color;

  static nlohmann::json json_schema();
};

struct SceneEntity {
  Timestamp timestamp;
  std::string frame_id;
  std::string id;
  Duration lifetime;
  bool frame_locked = false;
  std::vector<KeyValuePair> metadata;
  std::vector<ArrowPrimitive> arrows;
  std::vector<CubePrimitive> cubes;
  std::vector<SpherePrimitive> spheres;
  std::vector<CylinderPrimitive> cylinders;
  std::vector<LinePrimitive> lines;

  static nlohmann::json json_schema();
};

struct SceneUpdate {
  std::vector<SceneEntityDeletion> deletions;
  std::vector<SceneEntity> entities;

  static nlohmann::json json_schema();
};
```

这里最值得先抓住的一点是，**教程版坚持“组合优先”而不是重新发明一套独立字段系统。** `CubePrimitive` 没有自己再定义 `x/y/z/qx/qy/qz/qw`，而是直接复用 `Pose`。`SceneEntity` 没有内嵌匿名 JSON 结构，而是清楚地列出自己持有哪些 primitive 数组、哪些元数据字段、哪些时序字段。

这样设计有几个实际好处。

- 序列化规则直接复用 Ch05，NaN、`null`、字段顺序这套约定不用重讲。
- schema 层能自然复用 `Pose::json_schema()`、`Color::json_schema()` 等子结构。
- 测试能按层递进写，先测基础类型，再测组合后的复杂类型。
- 以后扩展 primitive 时，不需要动 `RawChannel` 或协议层，只是在消息层追加结构体和对应的编解码规则。

### 再看高频业务类型

本章还补了几个非常常见的“业务语义消息”。

```cpp
struct FrameTransform {
  Timestamp timestamp;
  std::string parent_frame_id;
  std::string child_frame_id;
  Vector3 translation;
  Quaternion rotation;

  static nlohmann::json json_schema();
};

struct Log {
  Timestamp timestamp;
  LogLevel level = LogLevel::UNKNOWN;
  std::string message;
  std::string name;
  std::string file;
  uint32_t line = 0;

  static nlohmann::json json_schema();
};

struct CompressedImage {
  Timestamp timestamp;
  std::string frame_id;
  std::vector<uint8_t> data;
  std::string format;

  static nlohmann::json json_schema();
};
```

这三种类型代表了三类完全不同的需求。

- `FrameTransform` 关心的是坐标系关系，所以除了平移和旋转，还必须带 frame 名字。
- `Log` 关心的是运行时诊断，所以要带 `level`、`message`、`file`、`line`。
- `CompressedImage` 关心的是二进制载荷，所以要把 `std::vector<uint8_t>` 明确建模进来。

它们都站在 Ch05 之上，但不是简单地“再多几个 struct”。更准确地说，这一章在建立一个稳定的**消息字典**。上层应用拿到这些类型时，知道每个字段的含义，下层序列化也知道该怎样输出确定性的 JSON。

### `TypedChannel<T>` 是消息层和 Channel 层之间的胶水

Ch08 最关键的新接口不是某个 primitive，而是 `TypedChannel<T>`。它把“消息类型的静态约束”和“第三章的字节流通道”接到一起。

```cpp
template<typename T>
class TypedChannel {
public:
  explicit TypedChannel(RawChannel channel)
      : channel_(std::move(channel)) {}

  FoxgloveResult<void> log(const T& msg, uint64_t timestamp_ns) {
    auto json = encode(msg);
    if (!json.has_value()) {
      return tl::make_unexpected(json.error());
    }
    const auto& data = json.value();
    channel_.log(reinterpret_cast<const uint8_t*>(data.data()), data.size(), timestamp_ns);
    return {};
  }

  RawChannel& raw() {
    return channel_;
  }

private:
  RawChannel channel_;
};

using SceneUpdateChannel = TypedChannel<SceneUpdate>;
using FrameTransformChannel = TypedChannel<FrameTransform>;
using LogChannel = TypedChannel<Log>;
```

这段代码一定要读透，因为它正是任务要求里提到的重点，**`TypedChannel<T>` 明确把 `encode()` 与 `RawChannel::log()` 绑在了一起。** 调用者不再自己手写下面这段流程：

```cpp
auto json = encode(update);
raw_channel.log(
  reinterpret_cast<const uint8_t*>(json.value().data()),
  json.value().size(),
  timestamp_ns
);
```

而是可以直接写：

```cpp
SceneUpdateChannel channel(std::move(raw_channel));
channel.log(update, timestamp_ns);
```

两段代码底层做的是同一件事，但后者多了两层保护。

1. 编译期限制了你只能往这个 channel 里写 `SceneUpdate`。
2. 序列化失败会统一走 `FoxgloveResult<void>` 返回，而不是让调用方漏掉错误分支。

> 💡 **🧰 C++ 技巧 工程旁白：类型安全 channel 的 template 设计**
>
> `TypedChannel<T>` 看起来像个很薄的模板壳，但它体现了一个很实用的 C++ 设计技巧：**把“容易出错的跨层样板代码”收进类型系统里。** 如果没有这层模板，业务代码每次发布消息都要自己调用 `encode()`，再把 `std::string` 转成 `const uint8_t*`，最后交给 `RawChannel::log()`。这套流程不复杂，但重复多了以后，最容易出错的恰恰就是这些看似机械的步骤，比如忘记检查 `encode()` 返回值，或者把错误的消息类型发到错误的 channel。
>
> 用模板包装以后，`TypedChannel<SceneUpdate>` 本身就成了一个带语义的句子，意思是“这个通道只负责发送 `SceneUpdate`”。模板参数把错误前移到了编译期，而不是运行时。与此同时，它没有破坏底层分层，内部仍然持有 `RawChannel`，仍然发送字节流，没有把序列化和传输层硬粘死。换句话说，这是一种很克制的类型安全做法，既减少样板代码，也保留了原始接口的通用性。

### 这一章的头文件地图

如果把 `messages.hpp` 再压缩成一张阅读地图，可以这样记：

```text
messages.hpp
├── 基础类型（来自 Ch05）
│   ├── Timestamp / Duration
│   ├── Vector3 / Quaternion / Pose
│   └── Color / Point3
├── primitive 层
│   ├── ArrowPrimitive / CubePrimitive / SpherePrimitive / CylinderPrimitive
│   └── LinePrimitive + LineType
├── entity/update 层
│   ├── SceneEntityDeletion + DeletionType
│   ├── KeyValuePair
│   ├── SceneEntity
│   └── SceneUpdate
├── 高层业务类型
│   ├── FrameTransform
│   ├── Log + LogLevel
│   └── CompressedImage
└── 发布层包装
    ├── encode<T>() / decode<T>()
    ├── TypedChannel<T>
    └── SceneUpdateChannel / FrameTransformChannel / LogChannel
```

读到这里，你应该能看到 Ch08 和 Ch05 的关系了。Ch05 定的是“基础语法”，Ch08 开始有了“完整句子”。

---

## 8.3 实现核心逻辑

头文件定完以后，`src/messages.cpp` 负责把这批复杂类型真正变成稳定 JSON。这里最值得看的不是把所有函数从头到尾抄一遍，而是抓住四个核心点：复杂类型的 `to_json` / `from_json`，枚举按整数编码，可选字段的处理方式，以及 `CompressedImage` 的 base64 路径。

### 复杂类型继续遵守“字段顺序稳定”的输出规则

先看 `SceneEntity` 和 `SceneUpdate` 的核心实现。

```cpp
void to_json(nlohmann::json& j, const SceneEntity& entity) {
  j = nlohmann::json{
    {"arrows", entity.arrows},
    {"cubes", entity.cubes},
    {"cylinders", entity.cylinders},
    {"frame_id", entity.frame_id},
    {"frame_locked", entity.frame_locked},
    {"id", entity.id},
    {"lifetime", entity.lifetime},
    {"lines", entity.lines},
    {"metadata", entity.metadata},
    {"spheres", entity.spheres},
    {"timestamp", entity.timestamp}
  };
}

void to_json(nlohmann::json& j, const SceneUpdate& update) {
  j = nlohmann::json{{"deletions", update.deletions}, {"entities", update.entities}};
}
```

这里延续了第五章的关键约定，**字段顺序由 `to_json` 里的显式插入顺序决定，并且与 golden file 严格对齐。** 本项目使用的是显式指定顺序，而不是 nlohmann::json 的自动排序——`SceneEntity` 里先写 `arrows`，再写 `cubes`，最后才是 `timestamp`，这是代码层面显式排定的。参考文档（08-内置消息类型.md）对此有"字母序"的描述，那是一个近似说法，实际技术保证来自 `to_json` 里的明确插入顺序。因为 golden file 做的是文本精确匹配，只要字段顺序漂了，测试就会失败。

对照真实样本，你能更直观地看到这一点：

```json
{"deletions":[{"id":"old_entity","timestamp":{"nsec":2,"sec":1},"type":0}],"entities":[{"arrows":[],"cubes":[],"cylinders":[],"frame_id":"world","frame_locked":false,"id":"new_entity","lifetime":{"nsec":0,"sec":0},"lines":[],"metadata":[],"spheres":[],"timestamp":{"nsec":4,"sec":3}}]}
```

这段内容来自 `tests/golden/scene_update.json`。它不是“展示用示意图”，而是测试里真正拿来比对的契约文本。

### `from_json` 采用“字段存在才覆盖”的策略

复杂类型的反序列化没有一股脑用 `j.at(...).get_to(...)`，而是大量使用 `contains()` 检查：

```cpp
void from_json(const nlohmann::json& j, SceneEntity& entity) {
  if (j.contains("arrows")) {
    j.at("arrows").get_to(entity.arrows);
  }
  if (j.contains("cubes")) {
    j.at("cubes").get_to(entity.cubes);
  }
  if (j.contains("frame_id")) {
    j.at("frame_id").get_to(entity.frame_id);
  }
  if (j.contains("frame_locked")) {
    j.at("frame_locked").get_to(entity.frame_locked);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(entity.timestamp);
  }
}
```

这段代码解决的不是“偷懒少写几行”，而是一个很实际的工程问题，**复合消息在演进过程中，部分字段缺省是常态。** 教学版的策略是：JSON 里有字段就覆盖，没有字段就保留结构体默认值。这样做带来的效果是，反序列化更宽容，也更适合逐步扩展消息定义。

当然，这种宽容策略的前提是默认值本身有语义。例如 `frame_locked = false`、空数组、零时间戳，都必须是合理默认值，否则“缺字段保留默认值”就会变成隐藏 bug 的来源。

### 枚举统一按整数序列化，和 schema 保持一致

这一章还引入了多个枚举：`LineType`、`DeletionType`、`LogLevel`。实现里统一把它们序列化成整数。

```cpp
void to_json(nlohmann::json& j, const LinePrimitive& primitive) {
  j = nlohmann::json{
    {"color", primitive.color},
    {"colors", primitive.colors},
    {"indices", primitive.indices},
    {"points", primitive.points},
    {"pose", primitive.pose},
    {"scale_invariant", primitive.scale_invariant},
    {"thickness", detail::double_to_json(primitive.thickness)},
    {"type", static_cast<uint8_t>(primitive.type)}
  };
}

void to_json(nlohmann::json& j, const Log& log) {
  j = nlohmann::json{
    {"file", log.file},
    {"level", static_cast<uint8_t>(log.level)},
    {"line", log.line},
    {"message", log.message},
    {"name", log.name},
    {"timestamp", log.timestamp}
  };
}
```

为什么这里不直接输出字符串，比如 `"WARNING"` 或 `"LINE_LIST"`？原因很简单，这一章希望保持 JSON 表达和 `json_schema()` 一致。schema 已经把这些字段定义成 integer，并给出范围约束，编码层跟着走整数，就能减少一层双向映射成本，也更贴近官方 Foxglove schema 的风格。

### `CompressedImage` 把二进制数据接回文本世界

本章实现里最不能跳过的一段，是 `CompressedImage` 的 base64 编解码。因为它证明了 Ch05 那条“统一 JSON 路径”在面对二进制载荷时仍然能走得通。

```cpp
std::string encode_base64(const std::vector<uint8_t>& data) {
  static constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  ...
}

FoxgloveResult<std::vector<uint8_t>> decode_base64(const std::string& text) {
  if (text.size() % 4U != 0U) {
    return tl::make_unexpected(FoxgloveError::SerializationError);
  }
  ...
}
```

再看 `CompressedImage` 自己的 `to_json` / `from_json`：

```cpp
void to_json(nlohmann::json& j, const CompressedImage& image) {
  j = nlohmann::json{
    {"data", encode_base64(image.data)},
    {"format", image.format},
    {"frame_id", image.frame_id},
    {"timestamp", image.timestamp}
  };
}

void from_json(const nlohmann::json& j, CompressedImage& image) {
  if (j.contains("data")) {
    const auto decoded = decode_base64(j.at("data").get<std::string>());
    if (!decoded.has_value()) {
      throw std::runtime_error("invalid base64 data");
    }
    image.data = decoded.value();
  }
  if (j.contains("format")) {
    j.at("format").get_to(image.format);
  }
  if (j.contains("frame_id")) {
    j.at("frame_id").get_to(image.frame_id);
  }
  if (j.contains("timestamp")) {
    j.at("timestamp").get_to(image.timestamp);
  }
}
```

这段代码体现了教程版一个非常重要的边界意识。底层消息传输最终还是字节，但这一章不急着把“字节如何进 protobuf wire format”也一起讲掉，而是先把字节安全地包进 JSON 字符串里，让读者看得见、测得着、抓包时也能肉眼验证。

真实 golden 样本如下：

```json
{"data":"/9j/2wABAg==","format":"jpeg","frame_id":"camera","timestamp":{"nsec":10,"sec":9}}
```

这条 JSON 正来自 `tests/golden/compressed_image.json`。如果 base64 padding、字段顺序或者时间戳嵌套结构有任何漂移，golden 测试都会立刻告诉你。

### 模板特化把“所有类型都走同一入口”这件事做实

最后再看文件尾部的模板特化区，它把 Ch05 的统一入口延伸到了本章所有复杂类型：

```cpp
template<>
FoxgloveResult<std::string> encode<SceneUpdate>(const SceneUpdate& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<SceneUpdate> decode<SceneUpdate>(const std::string& json_str) {
  return decode_impl<SceneUpdate>(json_str);
}

template<>
FoxgloveResult<std::string> encode<FrameTransform>(const FrameTransform& msg) {
  return encode_impl(msg);
}

template<>
FoxgloveResult<std::string> encode<CompressedImage>(const CompressedImage& msg) {
  return encode_impl(msg);
}
```

这部分代码看着有点机械，但它很重要，因为它让 `TypedChannel<T>` 真正有了统一依赖目标。`TypedChannel<SceneUpdate>` 不需要知道 `SceneUpdate` 的内部结构，只需要知道 `encode(scene_update)` 会得到 `FoxgloveResult<std::string>`。这正是“消息层”和“发布层”之间最干净的接口边界。

> 💡 **🏗️ 设计决策 工程旁白：Protobuf schema 描述符的自描述性，为什么教学版先走 JSON path**
>
> 官方 Foxglove 内置消息类型大量依赖 protobuf，不只是因为它更省带宽，还因为 protobuf schema 描述符本身有很强的自描述性。字段编号、嵌套消息、枚举、重复字段，这些信息都能被统一表达，Studio 也更容易在不知道具体 C++ 类型的前提下理解消息结构。从生产 SDK 角度看，这是一条更强的路线。
>
> 但教程版在 Ch08 仍然坚持 JSON path，是因为当前章节的教学目标不是“把官方技术栈一比一复刻”，而是让读者先看清消息层的设计轮廓。JSON 有两个很现实的优势。第一，可见性强，golden file、抓包、测试失败输出都能直接读。第二，它和 Ch05 完全同构，不需要在这一章同时引入 `.proto`、代码生成、descriptor、二进制 wire format 这些新概念。先把 primitive、entity、typed channel 的分层讲清楚，再去理解 protobuf 版官方实现，会容易很多。换句话说，这不是否认 protobuf 的价值，而是有意识地把教学路径拆成了更平缓的两步。

---

## 8.4 测试：验证正确性

这一章的测试策略，本质上是对 Ch05 的扩展。第五章已经证明了基础类型可以用 roundtrip 和 golden file 两条线来锁定行为，Ch08 只是把同样的方法套到更复杂的数据结构上。

### 我们要验证什么

对 Ch08 来说，至少要验证三件事。

1. **复杂类型 roundtrip 后语义不丢。** 比如 `SceneUpdate`、`FrameTransform` 这样的嵌套结构，编码再解码后，关键字段应该保持一致。
2. **golden file 仍然稳定。** 复杂结构更容易在字段顺序、数组布局、base64 输出上发生漂移，所以需要继续做文本精确匹配。
3. **错误路径没被静默吞掉。** 非法 JSON、缺失字段、非法 base64 都应该回到 `FoxgloveError::SerializationError` 这一统一错误面。

当前仓库里的 `tests/test_messages.cpp` 仍然以基础类型为主，但结构已经把这套测试方法搭好了。先看它的通用 golden file 辅助函数：

```cpp
std::string read_golden_file(const std::string& filename) {
  std::ifstream file(filename);
  REQUIRE(file.is_open());
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
    content.pop_back();
  }
  return content;
}

std::string golden_path(const std::string& filename) {
  return std::string(FOXGLOVE_TEST_GOLDEN_DIR) + "/" + filename;
}
```

这组辅助函数虽然简单，却把 `tests/golden/*.json` 的价值说明白了。golden 文件不是随便存一些示意输出，而是测试的一部分。它们负责把“期望文本长什么样”固定下来，尤其适合锁字段顺序和精确格式。

### 先看 roundtrip 测试模式

文件里已经有大量基础类型的 roundtrip 测试，写法很统一：

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

到了 Ch08，思路完全一样，只是对象从 `Pose` 换成了更复杂的 `SceneUpdate` 或 `FrameTransform`。也就是说，**本章新增的不是一套新测试哲学，而是把 Ch05 的方法自然扩展到复合结构。**

### 再看 golden file 测试模式

golden file 侧的写法同样已经成型：

```cpp
TEST_CASE("Pose - golden file match") {
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

  std::string expected = read_golden_file(golden_path("pose.json"));
  REQUIRE(encoded.value() == expected);
}
```

本章对应的 golden 样本已经在 `tests/golden/` 里准备好了，至少包括：

- `cube_primitive.json`
- `sphere_primitive.json`
- `cylinder_primitive.json`
- `arrow_primitive.json`
- `line_primitive.json`
- `scene_entity_deletion.json`
- `scene_entity.json`
- `scene_update.json`
- `frame_transform.json`
- `log.json`
- `compressed_image.json`

其中 `frame_transform.json` 是一个很好的例子：

```json
{"child_frame_id":"base_link","parent_frame_id":"map","rotation":{"w":1.0,"x":0.0,"y":0.0,"z":0.0},"timestamp":{"nsec":456,"sec":123},"translation":{"x":1.0,"y":2.0,"z":3.0}}
```

它锁定的不只是值对不对，还锁定了 `child_frame_id` 和 `parent_frame_id` 的字段名、嵌套 `rotation` 的格式，以及整个对象的输出顺序。

### 错误路径测试也延续了 Ch05 的风格

`test_messages.cpp` 里还保留了两类很关键的错误用例：

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

它们说明了一件很重要的事，**消息层测试不是只验证“好路径能跑通”，还要验证坏输入会被统一收口。** Ch08 的 `CompressedImage` base64 错误、复杂对象缺字段、枚举越界，都应该沿着同一思路继续补测试。

从教学节奏看，这恰好体现了 Ch05 和 Ch08 的承接关系。Ch05 建的是测试方法论，Ch08 只是把它应用到更复杂的消息形状上。

---

## 8.5 与官方实现对比

这一节不能省，因为本章和官方 Foxglove SDK 的差异非常典型，而且正好能帮助你理解“教学版做了什么取舍”。

### 相同点，消息语义和分层思路是一致的

无论是教程版还是官方实现，都承认同一件事，Foxglove 需要一批约定好的 built-in message types，来表达场景更新、坐标变换、日志和图像。也就是说，`SceneUpdate`、`FrameTransform`、`Log` 这种层次不是教程随手发明出来的，而是 Foxglove 生态本来就需要的语义层。

分层思路也基本一致。

- 下层传输仍然发送原始字节。
- 中层需要一个 schema 或类型描述层，告诉接收端“这些字节代表什么”。
- 上层调用方希望拿到的是强类型接口，而不是到处手写 JSON 或 protobuf 字节数组。

`TypedChannel<T>` 在教程版里的角色，本质上也对应着官方 SDK 里“typed publisher / typed topic”那类思路，只不过这里的实现更轻、更易读。

### 不同点一，教程版走 JSON，官方更偏 protobuf

这是最明显的差异。教程版当前把复杂消息编码成 JSON 字符串，再把这段字符串作为 payload 交给 `RawChannel::log()`。官方实现通常会走 protobuf schema 和二进制 payload，这样效率更高，schema 自描述能力也更完整。

但对教程读者来说，JSON 路径有两个直接优势。

1. 你可以直接看见输出结果，`tests/golden/*.json` 也能肉眼审查。
2. 这一章不需要再引入 `.proto`、代码生成器和 descriptor 这套新工具链。

所以这里不是“谁更先进”的问题，而是“教学路径先讲哪一层”的问题。教程版刻意先把结构设计讲清楚，再把更强的生产实现留给深入阅读。

### 不同点二，教程版的 `TypedChannel<T>` 更克制

官方 SDK 往往会把 schema 注册、topic advertisement、序列化格式、消息定义绑定得更紧，有时还会配合代码生成，让“某个消息类型应该发到什么 schema”这件事从工具链阶段就被固定下来。

而教程版的 `TypedChannel<T>` 只做一件事，把 `encode(msg)` 和 `RawChannel::log()` 收口成一个强类型调用。它没有接管 schema 广告，也没有引入生成代码。这样做的好处是，读者能把注意力集中在一条最核心的链路上：

```text
T 类型对象 -> encode(T) -> JSON 字符串 -> RawChannel::log()
```

这条链路足够小，足够透明，也足够适合作为教学落点。

### 不同点三，教程版测试更依赖 golden file

官方实现如果以 protobuf 为主，测试重点往往更偏向 schema、wire compatibility、binary payload 解码和跨语言互通。教程版因为走 JSON，所以更自然地会强调 golden file。`tests/golden/*.json` 在这里不是辅助材料，而是本章契约的一部分。

这也正是为什么本章很适合用来学习消息层设计。你能直接看到复杂类型序列化后的最终文本长什么样，也能直接理解字段顺序为什么重要，调试成本比直接看十六进制 protobuf 字节低很多。

### 该怎么使用这份对比

最好的用法不是把教程版和官方实现硬比较“谁更完整”，而是把它们看成两个层次。

- 教程版回答的是：消息层为什么要这样建模，typed channel 为什么能减少错误。
- 官方实现回答的是：当系统进入生产环境，怎样用 protobuf 和更完整的 schema 体系把这件事做得更高效、更标准化。

如果你把这章吃透，再去读官方实现，会更容易看懂那些 descriptor、schema name、encoding 选择背后的动机，而不会只觉得“为什么这里多了这么多工具链”。

---

## 8.6 打 tag，验证完成

以下命令为标准完成流程，所有章节统一：

```bash
# 1. 构建并运行测试（这是唯一的正确性标准）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# 2. 提交并打本地 tag（my- 前缀避免与仓库参考 tag 冲突）
git add .
git commit -m "feat(ch08): add built-in message types"
git tag my-v0.8-messages

# 3. 与参考实现对比（辅助理解，非强制门槛）
git diff v0.8-messages
```

**完成标准**：`ctest` 全部通过是硬性门槛。`git diff v0.8-messages` 可能会有风格差异，这很正常。只要测试通过，说明消息建模、JSON 编解码、typed channel 的核心行为已经正确。diff 的价值在于帮助你观察参考实现还做了哪些工程取舍。

对这一章来说，建议额外做两件有针对性的检查。

第一，打开 `tests/golden/scene_update.json`、`frame_transform.json`、`compressed_image.json`，确认你理解每个字段的来源。这样当 golden file mismatch 出现时，你知道是在 primitive 层、entity 层，还是 base64 路径出了问题。

第二，手动顺一遍 `TypedChannel<T>::log()` 的执行链路。确认它确实是 `encode(msg)` 成功后，把 `std::string` 的底层字节喂进 `RawChannel::log()`，而不是偷偷引入了新的发送协议。这个认识会直接帮助你理解第九章的端到端装配。

---

## 本章小结

- **本章掌握了**：
  - 为什么基础类型之上还需要 `SceneUpdate`、`FrameTransform`、`Log`、`CompressedImage` 这类业务语义消息
  - 教学版如何坚持“组合优先”的建模策略，把 `Pose`、`Color`、`Timestamp` 等基础积木复用到复杂类型里
  - `TypedChannel<T>` 怎样把 `encode()` 与 `RawChannel::log()` 绑在一起，减少重复样板代码和类型误用
  - `messages.cpp` 如何处理复杂对象的稳定 JSON 输出、整数枚举、可选字段和 base64 图像数据
  - `tests/golden/*.json` 为什么仍然是本章测试契约的重要组成部分

- **工程知识点**：
  - 类型安全 channel 的 template 设计
  - Protobuf schema 描述符的自描述性，为什么教学版先走 JSON path

- **延伸练习**：
  - 给 `tests/test_messages.cpp` 补一组 `SceneUpdate` 的 roundtrip 测试，练习如何为复杂嵌套结构设计断言
  - 尝试新增一个自定义 primitive，比如 `TextPrimitive`，体会“组合优先 + schema + golden file”这套扩展节奏
  - 对照官方 Foxglove built-in message 定义，看看教程版当前省略了哪些 protobuf 相关能力，理解教学版和生产版的边界

- **参考文档**：[08-内置消息类型.md](../08-内置消息类型.md)
