# 11 — 定时器管理器

**目标**：掌握 `TimerManager` 的用法，了解如何在事件循环中集成定时任务。

## 概述

`snc::core::TimerManager` 是一个进程内定时器管理工具，支持一次性定时器和周期定时器。它独立于事件循环运行，由调用方在合适时机轮询触发到期任务。

头文件入口：

```cpp
#include <snc/core/TimerManager.h>
```

## 核心 API

```cpp
// 添加一次性定时器（ms 后触发，只执行一次）
int64_t AddTimer(long long ms, TimerCallback cb);

// 添加周期定时器（每隔 ms 触发，持续执行）
int64_t AddRepeatingTimer(long long ms, TimerCallback cb);

// 取消定时器
bool CancelTimer(int64_t id);

// 处理所有到期的定时器（应在事件循环每帧调用）
void ProcessTimers();

// 返回下一个到期定时器的剩余毫秒数；无定时器返回 -1
long long NextTimeoutMs();
```

返回值 `int64_t` 为定时器 ID，用于 `CancelTimer()` 取消。

## 基本用法

```cpp
#include <snc/core/TimerManager.h>
#include <iostream>

snc::core::TimerManager tm;

// 一次性定时器：5 秒后执行
int64_t tid = tm.AddTimer(5000, []() {
    std::cout << "5 秒到了！\n";
});

// 周期定时器：每秒打印一次
int64_t repeatId = tm.AddRepeatingTimer(1000, []() {
    std::cout << "每秒 tick\n";
});

// 取消定时器（如需）
// tm.CancelTimer(tid);
```

## 在事件循环中集成

`TimerManager` 不自带事件循环，需由调用方在每帧驱动 `ProcessTimers()`。
EventLoop 在独立线程中运行，主线程通过 `IsRunning()` 判断是否退出：

```cpp
#include <snc/core.h>
#include <snc/core/TimerManager.h>

int main() {
    auto eventLoop = snc::core::CreateEventLoop();

    snc::core::TimerManager tm;

    // 注册一个周期性心跳上报任务
    tm.AddRepeatingTimer(5000, [&]() {
        // 上报心跳、清理过期数据等
        std::cout << "heartbeat\n";
    });

    // 启动事件循环线程
    std::thread loopThread([&]() {
        eventLoop->Run();
    });

    // 主线程主循环：驱动定时器 + 等待事件循环退出
    while (eventLoop->IsRunning()) {
        tm.ProcessTimers();   // 驱动到期任务
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    loopThread.join();
    return 0;
}
```

## 注意事项

- **`ProcessTimers()` 必须在事件循环线程内调用**，不要在回调内再次调用（可能重入导致未定义行为）
- **定时器回调中不要长时间阻塞**，否则会拖慢整个事件循环
- **取消已触发的定时器无效**：若回调正在执行中，`CancelTimer` 会静默忽略（该定时器已从内部 map 删除，但回调仍在运行）
- **重复定时器**每次触发后自动重新计算到期时间（基于当前时刻 + `ms`），不会累积漂移

参见 [03 - 连接生命周期](03-connection-lifecycle.md)（心跳机制）
