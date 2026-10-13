# 18 — WorkerPool（工作线程池）

**目标**：掌握 `WorkerPool` 的使用方式，理解它与 `EventLoop`、`MessageQueue` 的协作关系。

## 概述

`snc::core::WorkerPool` 是一个固定大小的工作线程池，将耗时任务从 IO 线程分离出去，避免阻塞事件循环。SNC 中的消息分派默认通过 WorkerPool 进行：IO 线程接收数据后入队，WorkerPool 取出并处理。

头文件入口：

```cpp
#include <snc/core/WorkerPool.h>
```

## 核心 API

```cpp
// 创建指定大小的线程池
explicit WorkerPool(size_t size);

// 提交异步任务（线程安全）
void Submit(std::function<void()> task);

// 停止所有线程并等待 join（析构时自动调用）
void Stop();
```

## 与 EventLoop 协作

`EventLoop` 可以通过以下两种方式与 WorkerPool 配合：

### 方式一：全局 WorkerPool（推荐）

```cpp
#include <snc/core.h>
#include <snc/core/WorkerPool.h>

int main() {
    auto eventLoop = snc::core::CreateEventLoop();
    snc::core::WorkerPool workerPool(/* threads */ 4);

    // 设置全局 WorkerPool，所有消息队列的入队操作将投递到此池
    eventLoop->SetWorkerPool(&workerPool);

    snc::core::Acceptor acceptor;
    acceptor.Listen("0.0.0.0", 9000);
    acceptor.SetNewConnectionCallback([&](snc::core::IConnection::Ptr conn) {
        eventLoop->AddConnection(conn);
    });

    eventLoop->Run();
    workerPool.Stop();
    return 0;
}
```

### 方式二：TcpConnection 级别分发

为单个连接设置专用的消息分派函数：

```cpp
snc::core::WorkerPool workerPool(/* threads */ 4);

conn->SetWorkerDispatchFn(
    [](snc::core::EntityHandle<snc::core::IConnection> eh,
       snc::core::Msg::Ptr msg) {
        // 此 lambda 在 WorkerPool 的某个线程中执行
        auto c = eh.lock();
        if (!c) return;

        // 安全地使用 conn 和 msg
        std::cout << "Processing msg on fd=" << c->Fd() << "\n";
    },
    &workerPool   // 指定目标线程池
);
```

## 提交任意任务

`Submit()` 不限于消息处理，任何需要后台执行的 `std::function<void()>` 都可以提交：

```cpp
workerPool.Submit([]() {
    // 在 WorkerPool 线程中执行
    HeavyComputation();
});

workerPool.Submit([playerId]() {
    // lambda 捕获变量，按值或引用均可
    SavePlayerToDB(playerId);
});
```

## 注意事项

- **`WorkerPool` 对象的生命周期必须覆盖所有任务**：不能在任务执行期间析构
- **`Submit()` 线程安全**：可从任意线程（包括 IO 线程）调用
- **任务执行顺序**：先进先出，但多个 Worker 线程同时消费，不保证严格 FIFO
- **不要提交会长时间阻塞的任务**：这会占用整个 Worker 线程，降低并发能力
- **析构时自动调用 `Stop()`**：手动调用 `Stop()` 后再析构是安全的，但再次调用 `Submit()` 会忽略（线程已停止）

## 典型架构示意

```
         ┌─────────────────────────────┐
         │       EventLoop (IO)         │
         │                              │
  TCP    │  RecvData → MessageQueue     │
  连接   │       │                      │
         │       ▼                      │
         │  WorkerPool (4 threads)      │
         │       │                      │
         │       ▼                      │
         │  Business Logic              │
         └─────────────────────────────┘
```

参见 [03 - 连接生命周期](03-connection-lifecycle.md)、[02 - RPC 服务](02-rpc-service.md)
