# 13 — Runtime Monitor（运行时监控）

**目标**：了解 `snc::monitor::Monitor` 如何采集和暴露服务器运行指标。

## 概述

`snc_monitor` 模块提供进程内运行时监控，无需外部依赖即可采集四类核心指标：

| 指标 | 说明 |
|------|------|
| **IO Idle Rate** | 事件循环迭代中的空闲比例，反映负载情况 |
| **Backpressure** | 消息队列积压的深度分布（p50/p95/p99/max） |
| **Lua Blocks** | Lua 执行超时记录（阻塞时长和次数） |
| **Msg Queue Depth** | 所有 Worker 消息队列的深度实时分布 |

头文件入口：

```cpp
#include <snc/monitor/Monitor.h>
```

## 创建与访问

```cpp
#include <snc/monitor/Monitor.h>

auto monitor = snc::monitor::Monitor::Create();
```

也可以通过便捷方法直接记录指标：

```cpp
monitor->RecordIoIteration(/* idle */ false);   // 每帧调用，idle=true 表示无事可做
monitor->RecordBackpressure(/* queueDepth */ 50); // 入队前调用，记录当前积压深度
monitor->RecordLuaBlock(/* durationMs */ 15);    // Lua 脚本执行超过阈值时调用
```

## 报告生成

```cpp
// 获取完整结构化报告
auto report = monitor->FullReport();

// 或直接获取可读字符串
std::string text = monitor->ToString();
std::cout << text;
```

输出示例：

```
=== SNC Monitor Report ===
  [IO Idle Rate]      iterations=10240 idle=3072 rate=0.3000
  [Backpressure]      triggers=42 maxDepth=856 p95=720 p99=812
  [Lua Blocks]        count=3 totalMs=45 avg=15.0ms max=20ms
  [Msg Queue]         current=12 max=856 p50=5 p95=720 p99=812 avg=23
```

## 各子指标详解

### IO Idle Rate（`IoIdleStats`）

每次事件循环迭代调用 `RecordIoIteration(idle)`，`idle=true` 表示本轮无任何 I/O 事件。长期 idle rate 过高说明服务负载不足或阻塞在无关紧要的操作上。

```cpp
// 在事件循环主循环中：
while (loop->IsRunning()) {
    bool hadEvents = loop->PollOnce(timeoutMs);
    monitor->RecordIoIteration(!hadEvents);
}
```

### Backpressure（`BackpressureStats`）

在消息入队前调用 `RecordBackpressure(depth)`，跟踪消息队列深度分布。当队列深度超过水位时触发反压，保护服务端内存。

```cpp
// 入队前记录当前深度
monitor->RecordBackpressure(messageQueue->Size());
bool ok = messageQueue->Enqueue(msg);
if (!ok) {
    // 触发反压：暂停读取
}
```

### Lua Blocks（`LuaBlockStats`）

仅在 Lua 执行超过预定阈值（如 10ms）时调用，用于定位 Lua 脚本的性能瓶颈。

```cpp
auto t0 = std::chrono::steady_clock::now();
luaL_dostring(L, script);
auto ms = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - t0).count();
if (ms > 10.0) {   // 超过 10ms 视为阻塞
    monitor->RecordLuaBlock((uint64_t)ms);
}
```

### Msg Queue（`MsgQueueStats`）

与 Backpressure 共享数据源，通过 `RecordBackpressure()` 同步记录。提供 p50/p95/p99 分位深度统计。

## 与 WorkerPool 集成

`snc_monitor` 的背压指标应在 `WorkerPool` 消息分发路径中记录：

```cpp
workerPool_->SetDispatchFn([monitor](EntityHandle<IConnection> eh, Msg::Ptr msg) {
    monitor->RecordBackpressure(workerPool_->GetPendingCount());
    // ... 分发逻辑
});
```

## 注意事项

- **`Monitor` 是线程安全的**：各子统计类使用 `atomic` 操作，可安全地从多个线程调用
- **不自动采集**：必须手动调用 `Record*` 系列方法，框架不自动采样
- **`ToString()` 仅用于日志输出**：在需要结构化数据时应使用 `FullReport()`
- **采样窗口有限**：内部只保留最近 500~2000 个样本，旧数据自动丢弃

参见 [09 - 熔断器](09-circuit-breaker.md)、[11 - 定时器管理器](11-timer-manager.md)
