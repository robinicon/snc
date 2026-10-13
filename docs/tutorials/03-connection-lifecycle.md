# 03 - 连接生命周期

本教程深入讲解 SNC 网络层的连接管理机制，包括 EventLoop、TcpConnection 和消息路由。

## EventLoop

EventLoop 是网络 I/O 的核心调度器，底层根据平台自动选择实现：

| 平台 | 后端 | 说明 |
|------|------|------|
| Windows | IOCP | 完成端口，高并发首选 |
| Linux | epoll | epoll_wait() |
| macOS | kqueue | kevent()（BSD 等效） |

```cpp
// 创建 EventLoop
auto eventLoop = snc::core::CreateEventLoop();
if (!eventLoop) { /* handle error */ }

// 运行事件循环（阻塞，直到 RemoveAllConnections 被调用或主动退出）
eventLoop->Run();
```

## TcpConnection

`TcpConnection` 封装了 TCP 套接字的读写，提供：

- **环形缓冲区**：避免频繁内存分配，提升吞吐
- **高低水位背压**：发送缓冲区超过高水位时停止读取，低于低水位时恢复
- **安全的跨线程访问**：通过 `EntityHandle<T>` 弱引用管理生命周期

```cpp
// 创建 TcpConnection（通常由 EventLoop 自动创建）
auto conn = std::make_shared<TcpConnection>(std::move(sock));

// 发送数据（自动缓冲，批量累积后 Flush）
conn->Send(Msg::Create(0x1001, payload, len));

// 设置消息回调
conn->SetMessageCallback([](EntityHandle<IConnection> eh, Msg::Ptr msg) {
    // 处理收到的消息
});

// 设置断开回调
conn->SetCloseCallback([](IConnection::Ptr conn, DisconnectReason reason) {
    // 清理资源
});

// 加入事件循环
eventLoop->AddConnection(conn);
```

## EntityHandle

`EntityHandle<T>` 是带类型检查的弱引用，用于跨线程传递连接：

```cpp
#include "snc/rpc/Entity.h"

// 从 shared_ptr 创建 handle
auto handle = EntityHandle<IConnection>::from(conn);

// 安全地使用（handle 可能已被析构）
auto locked = handle.lock();
if (locked) {
    locked->Send(msg);
}
```

## 背压机制

`TcpConnection` 内置背压保护，防止发送端压垮接收端：

```
                    sendBuffer_
  ┌────────────────────────────────────┐
  | 已发送但未被对端确认的数据          |
  └────────────────────────────────────┘
  ↑ low watermark              ↑ high watermark
```

- 发送缓冲区超过 **高水位**时，`ProcessRecvData` 暂停解析，避免内存爆炸
- 低于 **低水位**时，自动恢复解析

可通过构造函数调整水位：

```cpp
// 默认高水位 64KB，低水位 32KB
auto conn = std::make_shared<TcpConnection>(sock, 65536, 32768);
```

## 消息路由

`MsgRouterCpp` 提供按消息 ID 分发处理函数的机制：

```cpp
#include "snc/core/MsgRouter.h"

MsgRouterCpp router;

router.RegisterHandler(0x1001, [](IConnection::Ptr conn, Msg::Ptr msg) {
    // 处理登录消息
});

router.RegisterHandler(0x1002, [](IConnection::Ptr conn, Msg::Ptr msg) {
    // 处理移动消息
});

// 在连接的消息回调中调用
conn->SetMessageCallback([&router](EntityHandle<IConnection> eh, Msg::Ptr msg) {
    auto conn = eh.lock();
    if (!conn) return;
    router.Route(conn, std::move(msg));
});
```

## 完整示例：简单游戏服务器

```cpp
#include "snc/core.h"
#include "snc/rpc.h"
#include "snc/base/Log.h"
#include <unordered_map>
#include <memory>

using namespace snc;
using namespace snc::core;
using namespace snc::rpc;

class GameServer
{
public:
    void Run(uint16_t port)
    {
        auto eventLoop = CreateEventLoop();
        if (!eventLoop) return;

        Acceptor acceptor;
        if (!acceptor.Listen("0.0.0.0", port)) return;

        rpc::RpcServer rpcServer;

        // 注册 RPC 处理函数
        rpcServer.RegisterHandler(0x1001,
            [this](IConnection::Ptr conn, RpcSession::Ptr,
                   Msg::Ptr msg,
                   std::function<void(RpcError, const uint8_t*, size_t)> reply) {
                HandleLogin(conn, msg, reply);
            });

        acceptor.SetNewConnectionCallback([this, eventLoop.get()](IConnection::Ptr conn) {
            conn->SetMessageCallback([this, conn](EntityHandle<IConnection> eh, Msg::Ptr msg) {
                rpcServer.Route(conn, nullptr, std::move(msg));
            });
            conn->SetCloseCallback([eventLoop.get()](IConnection::Ptr c,
                                                      DisconnectReason reason) {
                eventLoop->RemoveConnection(c);
            });
            eventLoop->AddConnection(conn);
        });

        eventLoop->Run();
    }

private:
    void HandleLogin(IConnection::Ptr conn, Msg::Ptr msg,
                     std::function<void(RpcError, const uint8_t*, size_t)> reply)
    {
        // 验证玩家凭证
        int32_t errorCode = 0;
        reply(RpcError::OK, reinterpret_cast<const uint8_t*>(&errorCode), sizeof(errorCode));
    }
};

int main()
{
    base::InitLogger(base::LogLevel::INFO);
    GameServer server;
    server.Run(9000);
    return 0;
}
```

## 关键 API 速查

| 类 | 用途 |
|----|------|
| `CreateEventLoop()` | 创建平台适配的事件循环 |
| `Acceptor` | TCP 监听器，接受新连接 |
| `TcpConnection` | 封装 TCP 连接，含环形缓冲和背压 |
| `Msg::Create(id, data, len)` | 创建网络消息 |
| `EntityHandle<T>` | 跨线程安全弱引用 |
| `RpcServer` | RPC 服务端，注册/路由消息 |
| `RpcClient` | RPC 客户端，带自动重连 |
| `MsgRouterCpp` | 按 msgId 分发消息处理函数 |
