# 20 — 最小 TCP 服务器

**目标**：从零构建一个最小 TCP 回显服务器，理解 `EventLoop`、`Acceptor`、`TcpConnection` 的协作关系。

## 架构概览

SNC 的网络层由三个核心组件组成：

```
客户端 ──TCP──▶ Acceptor ──▶ EventLoop ──▶ TcpConnection
                            │
                    SetNewConnectionCallback  （新连接回调）
```

- **`EventLoop`**：事件循环，负责 IOCP/epoll/kqueue 多路复用
- **`Acceptor`**：监听 socket，接受新连接并创建 `TcpConnection`
- **`TcpConnection`**：封装单个 TCP 连接的收发消息

## 完整代码

创建一个 `main.cpp`，内容如下：

```cpp
#include "snc/core.h"
#include "snc/base/Log.h"
#include <iostream>

using namespace snc;
using namespace snc::core;

int main()
{
    // 1. 初始化日志
    base::InitLogger(base::LogLevel::INFO);

    // 2. 创建事件循环（跨平台自动选择 IOCP/epoll/kqueue）
    auto eventLoop = CreateEventLoop();
    if (!eventLoop) {
        SNC_LOG_ERROR("main", "Failed to create event loop");
        return 1;
    }

    // 3. 创建 Acceptor，监听 9000 端口
    Acceptor acceptor;
    if (!acceptor.Listen("0.0.0.0", 9000)) {
        SNC_LOG_ERROR("main", "Failed to listen on port 9000");
        return 1;
    }

    // 4. 设置新连接回调
    acceptor.SetNewConnectionCallback([](IConnection::Ptr conn) {
        SNC_LOG_INFO("server", "Client connected: fd=%d", conn->Fd());

        // 消息到达时触发
        conn->SetMessageCallback([](EntityHandle<IConnection> eh, Msg::Ptr msg) {
            auto c = eh.lock();
            if (!c) return;
            SNC_LOG_INFO("server", "Received %zu bytes from fd=%d",
                         msg->PayloadLen(), c->Fd());
            // 原样回显
            c->Send(msg);
        });

        // 连接关闭时触发
        conn->SetCloseCallback([](IConnection::Ptr conn, DisconnectReason reason) {
            SNC_LOG_INFO("server", "Client disconnected: fd=%d, reason=%d",
                         conn->Fd(), (int)reason);
        });

        // 将连接注册到事件循环
        eventLoop->AddConnection(conn);
    });

    SNC_LOG_INFO("server", "Echo server started on port 9000");

    // 5. 启动事件循环（阻塞直到服务器关闭）
    eventLoop->Run();
    return 0;
}
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(minimal_tcp)

add_executable(minimal_tcp main.cpp)
target_link_libraries(minimal_tcp PRIVATE snc_core snc_base)
target_include_directories(minimal_tcp PRIVATE ${CMAKE_SOURCE_DIR}/include)
```

## 编译运行

```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

.\build\Release\minimal_tcp.exe
# 输出：[INFO] Echo server started on port 9000
```

另开终端用 telnet 或 nc 连接测试：

```bash
telnet 127.0.0.1 9000
Hello World   # 输入后回车，服务器会原样回显
```

## 关键 API 速查

| 类/方法 | 说明 |
|---------|------|
| `CreateEventLoop()` | 创建跨平台事件循环（IOCP/epoll/kqueue） |
| `Acceptor::Listen(host, port)` | 绑定地址并监听 |
| `Acceptor::SetNewConnectionCallback(fn)` | 设置新连接回调，参数为 `IConnection::Ptr` |
| `TcpConnection::SetMessageCallback(fn)` | 设置消息回调，参数为 `EntityHandle<IConnection>` 和 `Msg::Ptr` |
| `TcpConnection::SetCloseCallback(fn)` | 设置断连回调 |
| `EventLoop::AddConnection(conn)` | 将连接加入事件循环，开始收发数据 |
| `EventLoop::Run()` | 启动事件循环（阻塞调用） |
| `EntityHandle<T>::lock()` | 获取 `shared_ptr<T>`，连接关闭时返回 `nullptr` |

## 为什么用 EntityHandle？

`SetMessageCallback` 的回调签名使用 `EntityHandle<IConnection>` 而非裸指针或 `shared_ptr`：

- `EntityHandle` 存储 `weak_ptr`，不延长连接生命周期
- `lock()` 返回临时 `shared_ptr`，调用方自行管理引用
- 跨线程传递安全：即使 IO 线程已关闭连接，Worker 线程仍可安全地 `lock()` 检查

参见 [12 — EntityHandle 详解](./12-entity-handle.md)

## 下一步

- [02 - RPC 服务](./02-rpc-service.md) — 在 TCP 之上添加 RPC 序列化
- [03 - 连接生命周期](./03-connection-lifecycle.md) — 深入理解反压与连接状态机
- [21 - 完整单服集成](./21-single-server-integration.md) — 将 TCP + RPC + HTTP 集成到一个服务
