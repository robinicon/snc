# 16 — 消息序列化（Protobuf / FlatBuffers）

**目标**：了解 SNC 的序列化接口 `ISerializer`，以及 Protobuf 和 FlatBuffers 两种可选后端的用法。

## 概述

SNC 不强制使用特定序列化格式，payload 以原始字节数组（`uint8_t*`）传递，业务层自行处理。但框架提供了两个可选的序列化实现，通过 CMake 开关启用：

| 后端 | 开关 | 说明 |
|------|------|------|
| **Protobuf** | `SNC_ENABLE_PROTOBUF=ON` | 业界标准，需要外部 Protobuf 库 |
| **FlatBuffers** | `SNC_ENABLE_FLATBUFFERS=ON` | 零拷贝反序列化，适合高性能场景 |
| **无**（默认） | 两个开关均为 OFF | `ISerializer` 所有方法返回 `SER_FAILED` |

头文件入口：

```cpp
#include <snc/serial/ISerializer.h>
#include <snc/serial/pb/PbSerializer.h>    // Protobuf 后端
#include <snc/serial/fbs/FbsSerializer.h>  // FlatBuffers 后端
```

## ISerializer 接口

```cpp
class ISerializer {
public:
    virtual ~ISerializer() = default;

    // 将原始数据编码为带 msgId 的 Msg 对象（用于发送）
    virtual SerialError Serialize(const void* src, size_t srcLen,
                                  core::Msg& outMsg) = 0;

    // 从 Msg.payload 中解码原始数据到 dst（用于接收）
    virtual SerialError Deserialize(const core::Msg& srcMsg, size_t offset,
                                    void* dst, size_t dstLen) = 0;
};
```

返回值：
- `SerialError::OK` — 成功
- `SerialError::INVALID_MSG` — 消息为空或格式错误
- `SerialError::SER_FAILED` — 序列化失败（未启用后端时始终返回此值）
- `SerialError::TYPE_MISMATCH` — 目标类型与编码类型不匹配

## Protobuf 后端

启用方式：

```bash
cmake -B build -DSNC_ENABLE_PROTOBUF=ON
```

需额外安装 Protobuf 库（Windows: vcpkg install protobuf，Linux: apt-get install libprotobuf-dev）。

```cpp
#include <snc/serial/pb/PbSerializer.h>

serial::pb::PbSerializer proto;

// 编码：将 proto 字节数组打包进 Msg
std::string protoData = /* 已序列化的 protobuf 数据 */;
snc::core::Msg msg;
auto err = proto.Serialize(protoData.data(), protoData.size(), msg);
if (err != serial::SerialError::OK) { /* handle error */ }

// 解码：从 Msg 中提取 proto 字节
std::string decoded;
auto err2 = proto.Deserialize(msg, /* offset */ 0,
    (void*)protoData.data(), protoData.size());
```

### 便捷方法

```cpp
// 将 protobuf 数据编码为带长度前缀的 wire 格式
size_t outLen = 0;
uint8_t* encoded = PbSerializer::Encode(protoData.data(), protoData.size(), outLen);
// 使用 encoded（注意：调用方负责 delete[] encoded）

// 解码带长度前缀的 wire 数据
std::string out;
bool ok = PbSerializer::Decode(encoded, outLen, out);
delete[] encoded;
```

## FlatBuffers 后端

启用方式：

```bash
cmake -B build -DSNC_ENABLE_FLATBUFFERS=ON
```

需额外安装 FlatBuffers 库。

```cpp
#include <snc/serial/fbs/FbsSerializer.h>

serial::fbs::FbsSerializer fbs;

// 用法与 Protobuf 相同，接口一致
snc::core::Msg msg;
auto err = fbs.Serialize(fbsData.data(), fbsData.size(), msg);
```

### 便捷方法

```cpp
size_t outLen = 0;
uint8_t* encoded = FbsSerializer::Encode(fbsData.data(), fbsData.size(), outLen);
std::string out;
bool ok = FbsSerializer::Decode(encoded, outLen, out);
delete[] encoded;
```

## 禁用后端时的行为

当两个开关均未启用时，`ISerializer` 的任何实现（`PbSerializer` / `FbsSerializer`）均退化为空操作：

- `Serialize()` 返回 `SER_FAILED`
- `Deserialize()` 返回 `SER_FAILED`
- 静态辅助方法 `Encode`/`Decode` 返回 null/`false`

这意味着业务代码在编译时不需要条件判断，只需检查返回值即可。

## 使用建议

| 场景 | 推荐方案 |
|------|---------|
| 跨语言服务通信（RPC） | Protobuf（生态完善，兼容性最好） |
| 高频游戏帧同步（极致性能） | FlatBuffers（零拷贝反序列化） |
| 快速原型 / 内部开发 | 直接传原始字节，不用序列化层 |
| 单元测试 | 不使用序列化，直接用 `Msg::Create(id)` |

## 注意事项

- **不推荐使用 Protobuf 默认配置**：`SNC_ENABLE_PROTOBUF=ON` 需要安装第三方库，CI/CD 环境需提前准备
- **FlatBuffers 需要 .fbs 文件生成 C++ 头**：使用 `flatc` 编译器生成代码后再引用
- **两种方案均不支持自动消息路由**：msgId 需业务层自己管理和分派
- **序列化和 RPC 是完全独立的层**：`ISerializer` 只负责字节编码，不负责网络传输

参见 [02 - RPC 服务](02-rpc-service.md)（RPC 直接使用原始字节 payload）
