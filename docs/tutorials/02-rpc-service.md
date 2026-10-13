# 02 - RPC 服务

本教程介绍如何使用 SNC 的 RPC 模块构建分布式网络游戏服务器。

## 架构概览

```
┌─────────────────┐      TCP/RPC       ┌─────────────────┐
│  网关服务        │ ──────────────────▶ │  业务服务        │
│  (Gate)         │ ◀────────────────── │  (Login / Map)  │
└─────────────────┘                     └─────────────────┘
```

RPC 模块提供：
- **RpcServer**：注册消息处理函数，接收来自其他服务的 RPC 调用
- **RpcClient**：向远程服务发起调用，自动管理连接和重连
- **RpcSession**：维护请求/响应的序列号映射，确保回调与正确请求匹配

## 构建 RPC 服务端

```cpp
#include "snc/rpc.h"
#include "snc/base/Log.h"

using namespace snc;
using namespace snc::rpc;

void RunServer(uint16_t port)
{
    auto eventLoop = core::CreateEventLoop();
    if (!eventLoop) return;

    core::Acceptor acceptor;
    if (!acceptor.Listen("0.0.0.0", port)) return;

    RpcServer rpcServer;

    // 注册处理函数：处理来自网关的请求
    rpcServer.RegisterHandler(0x1001,
        [](core::IConnection::Ptr conn,
           RpcSession::Ptr /*session*/,
           core::Msg::Ptr msg,
           std::function<void(RpcError, const uint8_t*, size_t)> reply) {
            SNC_LOG_INFO("Received RPC request, payload len=%zu", msg->PayloadLen());

            // 构造响应
            int32_t errorCode = 0;
            reply(RpcError::OK,
                  reinterpret_cast<const uint8_t*>(&errorCode),
                  sizeof(errorCode));
        });

    acceptor.SetNewConnectionCallback([&rpcServer, el=eventLoop.get()](core::IConnection::Ptr conn) {
        SNC_LOG_INFO("Service connection accepted: fd=%d", conn->Fd());
        conn->SetMessageCallback([&rpcServer](core::EntityHandle<core::IConnection>,
                                               core::Msg::Ptr msg) {
            rpcServer.Route(conn, nullptr, std::move(msg));
        });
        conn->SetCloseCallback([el](core::IConnection::Ptr c, core::DisconnectReason reason) {
            SNC_LOG_WARN("Connection closed: fd=%d, reason=%d", c->Fd(), (int)reason);
            el->RemoveConnection(c);
        });
        el->AddConnection(conn);
    });

    eventLoop->Run();
}
```

## 构建 RPC 客户端

```cpp
#include "snc/rpc.h"
#include "snc/base/Log.h"

using namespace snc;
using namespace snc::rpc;

void RunClient(const std::string& serverHost, uint16_t serverPort, core::IEventLoop* eventLoop)
{
    // RpcSession 需要绑定一个 Entity（持有连接），Entity 可传 nullptr 延迟绑定
    auto entity    = std::make_shared<Entity>(nullptr);
    auto rpcSession = std::make_shared<RpcSession>(entity);
    auto rpcClient = std::make_shared<RpcClient>(
        rpcSession, eventLoop, nullptr);  // 事件循环通过构造函数传入

    // 发起 RPC 调用
    std::string payload = "player_name";
    rpcClient->Call(0x1001, payload.c_str(), payload.size(),
        [](RpcError err, const uint8_t* resp, size_t len) {
            if (err == RpcError::OK) {
                int32_t code = *reinterpret_cast<const int32_t*>(resp);
                SNC_LOG_INFO("RPC response OK, code=%d", code);
            } else {
                SNC_LOG_ERROR("RPC failed: err=%d", (int)err);
            }
        });

    eventLoop->Run();
}
```

## 消息 ID 规范

建议将消息 ID 按服务分层管理，例如：

```cpp
// 网关 → 业务服务
static constexpr int32_t MSG_GATE_TO_LOGIN  = 0x1001;
static constexpr int32_t MSG_GATE_TO_MAP    = 0x1002;

// 业务服务 → 网关
static constexpr int32_t MSG_LOGIN_RESULT   = 0x2001;
static constexpr int32_t MSG_MAP_RESULT     = 0x2002;

// 服务间通信
static constexpr int32_t MSG_SERVER_TO_SERVER = 0x3001;
```

## 序列化

SNC 不强制使用特定序列化格式。Payload 以原始字节数组（`uint8_t*`）传递，
业务层自行处理序列化/反序列化。可选支持：

- **Protobuf**：`SNC_ENABLE_PROTOBUF=ON`（需外部安装 Protobuf）
- **FlatBuffers**：`SNC_ENABLE_FLATBUFFERS=ON`（需外部安装 FlatBuffers）

## 下一步

- [03 - 连接生命周期](03-connection-lifecycle.md) — 深入 EventLoop 和背压机制
