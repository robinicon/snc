# 01 - Quick Start

本教程介绍如何编译 SNC 框架，并构建第一个网络游戏服务器。

## 环境要求

- **编译器**：MSVC 2019+ / GCC 8+ / Clang 7+（C++17）
- **CMake**：≥ 3.20
- **操作系统**：Windows (IOCP) 或 Linux (epoll)

## 克隆与配置

```bash
cd snc_cpp
cmake -B build -G "Visual Studio 17 2022"    # Windows
cmake -B build -DCMAKE_BUILD_TYPE=Release    # Linux
```

## 最小编译配置

`snc_cpp/CMakePresets.json` 提供四个预设：

| Preset   | 说明                          |
|----------|-------------------------------|
| `minimal` | `snc_base` + `snc_core` + `snc_rpc` |
| `normal`  | Minimal + Lua + Monitor       |
| `full`    | 全部功能（Lua、Protobuf、FlatBuffers） |
| `relwithdebinfo` | Release 含调试信息（默认）    |

```bash
cmake --preset=minimal
cmake --build --preset=minimal --config Debug
```

## 构建第一个 TCP 服务器

以下代码创建一个监听端口、接受客户端连接、回显收到的消息的服务器。

```cpp
#include "snc/core.h"
#include "snc/base/Log.h"
#include <iostream>
#include <memory>
#include <string>

using namespace snc;
using namespace snc::core;

int main()
{
    // 启动日志
    base::GetLogger().Start("server.log");

    // 创建事件循环（Windows 使用 IOCP，Linux 使用 epoll）
    auto eventLoop = CreateEventLoop();
    if (!eventLoop) {
        SNC_LOG_ERROR("Failed to create event loop");
        return 1;
    }

    // 创建 Acceptor，监听 9000 端口
    Acceptor acceptor;
    if (!acceptor.Listen("0.0.0.0", 9000)) {
        SNC_LOG_ERROR("Failed to listen on port 9000");
        return 1;
    }

    // 新连接到达时触发
    acceptor.SetNewConnectionCallback([](IConnection::Ptr conn) {
        SNC_LOG_INFO("Client connected: fd=%d", conn->Fd());

        // 设置消息回调：收到数据时触发
        conn->SetMessageCallback([](EntityHandle<IConnection> eh, Msg::Ptr msg) {
            auto c = eh.lock();
            if (!c) return;
            SNC_LOG_INFO("Received %zu bytes from fd=%d", msg->PayloadLen(), c->Fd());
            // 回显给客户端
            c->Send(msg);
        });

        // 断开连接时触发
        conn->SetCloseCallback([](IConnection::Ptr conn, DisconnectReason reason) {
            SNC_LOG_INFO("Client disconnected: fd=%d, reason=%d", conn->Fd(), (int)reason);
        });

        eventLoop->AddConnection(conn);
    });

    SNC_LOG_INFO("Echo server started on port 9000");
    eventLoop->Run();   // 阻塞直到服务器关闭
    return 0;
}
```

## 客户端测试

用 telnet 或 nc 连接验证：

```bash
telnet 127.0.0.1 9000
```

输入任意文字，服务器会原样回显。

## 下一步

- [02 - RPC 服务](02-rpc-service.md) — 学习 RPC 客户端与服务端
- [03 - 连接生命周期](03-connection-lifecycle.md) — 深入 EventLoop 和 TcpConnection
