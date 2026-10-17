# 12 — EntityHandle 详解

**目标**：理解 `EntityHandle` 的设计原理，掌握跨线程安全访问连接的用法。

## 背景：为什么需要 EntityHandle？

在 SNC 架构中，**IO 线程**负责网络 I/O（接收数据、发送响应），**Worker 线程**负责业务逻辑处理。当 IO 线程关闭连接时，Worker 线程可能仍持有对该连接的引用，若不加以保护就会发生 use-after-free。

```
IO 线程                    Worker 线程
   │                            │
   ├── 收到数据 ───────────────→│  持有 connection 裸指针
   ├── 关闭连接（析构）─────────→│  💥 use-after-free！
```

`EntityHandle<T>` 基于 `weak_ptr` 包装，解决此问题：Worker 线程每次使用前先 `lock()` 获取临时强引用，若对象已销毁则返回 `nullptr`，安全跳过。

## EntityHandle 核心 API

```cpp
#include <snc/core/EntityHandle.h>

// 从 shared_ptr 创建（存储 weak_ptr 内部）
auto handle = snc::core::EntityHandle<IConnection>::from(conn_shared_ptr);

// 锁定获取临时强引用；若对象已销毁返回 nullptr
auto conn = handle.lock();
if (!conn) return;   // 连接已关闭，安全跳过

// 检查有效性（不获取强引用，无额外引用计数开销）
bool alive = (bool)handle;   // 隐式转换为 bool（调用 weak_ptr::lock() 不存储结果）
// 或
bool alive = !!handle;
```

## TcpConnection 提供的便捷方法

`TcpConnection` 内置了获取连接句柄的方法：

```cpp
#include <snc/core/TcpConnection.h>

// 获取 EntityHandle<IConnection>，用于跨线程传递
auto handle = conn->GetConnHandle();

// 在 Worker 线程中使用：
workerPool_->Submit([handle]() {
    auto conn = handle.lock();
    if (!conn) return;     // 连接已关闭
    // 安全地使用 conn...
});
```

## 完整示例：WorkerPool 分发消息

```cpp
#include <snc/core.h>
#include <snc/core/TcpConnection.h>
#include <snc/core/WorkerPool.h>
#include <snc/core/MessageQueue.h>

using namespace snc;
using namespace snc::core;

int main() {
    auto eventLoop = CreateEventLoop();
    WorkerPool workerPool(/* threads */ 4);

    // 设置消息分发：直接提交到 WorkerPool，绕过全局消息队列
    eventLoop->SetWorkerPool(&workerPool);

    Acceptor acceptor;
    acceptor.Listen("0.0.0.0", 9000);
    acceptor.SetNewConnectionCallback([&](IConnection::Ptr conn) {
        eventLoop->AddConnection(conn);

        // 设置 Worker 分派函数
        conn->SetWorkerDispatchFn(
            [](EntityHandle<IConnection> eh, Msg::Ptr msg) {
                // Worker 线程执行此 lambda
                auto c = eh.lock();
                if (!c) return;   // 连接已关闭，安全退出

                // 安全使用 c 和 msg
                std::cout << "fd=" << c->Fd()
                          << " payload=" << msg->PayloadLen() << "\n";
            },
            &workerPool
        );
    });

    eventLoop->Run();
    workerPool.Stop();
    return 0;
}
```

## Entity vs EntityHandle（两个概念的区别）

SNC 中有两套相关的句柄机制：

| 类型 | 所在命名空间 | 用途 |
|------|-------------|------|
| `EntityHandle<T>` | `snc::core` | 通用跨线程安全引用，适用于任何 `shared_ptr<T>` |
| `rpc::Entity` + `rpc::EntityHandle<Entity>` | `snc::rpc` | 专门用于游戏实体（含连接），在 `Entity` 析构时自动 `Invalidate()` |

`rpc::Entity` 额外提供了 `Invalidate()` 机制：析构时清空内部连接指针，使所有 `EntityHandle` 立即失效（`lock()` 返回 `nullptr`）。这在玩家断开时特别有用。

```cpp
// rpc::Entity 使用示例
auto entity = std::make_shared<rpc::Entity>(conn);
auto handle = entity->GetHandle();   // EntityHandle<rpc::Entity>

// Worker 线程：
worker->Submit([handle]() {
    auto ent = handle.lock();
    if (!ent) return;    // 实体已被销毁（玩家断线）
    // 安全操作 ent->GetConn()
});
```

## 注意事项

- **`lock()` 返回 `nullptr` 时表示对象已销毁**，必须检查后再使用
- **不要缓存 `lock()` 结果跨帧使用**：两次 `lock()` 之间对象可能已被销毁
- **`EntityHandle` 默认构造为空**，`lock()` 永远返回 `nullptr`，需通过 `from()` 静态方法初始化
- **`from()` 接受 `shared_ptr` 而非裸指针**：传入 `nullptr` 的 `shared_ptr` 会创建空句柄
- 使用 `operator bool()` 检查有效性是最轻量方式（仅调用 `weak_ptr::lock()` 但不存储结果）

参见 [03 - 连接生命周期](03-connection-lifecycle.md)、[02 - RPC 服务](02-rpc-service.md)
