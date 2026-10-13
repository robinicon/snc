#pragma once

#include <memory>
#include "snc/core/IEventLoop.h"
#include "snc/core/WorkerPool.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/monitor/Monitor.h"

namespace snc {
namespace mmorpg {

/// ServiceContext — 聚合常见依赖，避免每个服务重复组装。
///
/// 典型用法：
///   core::IEventLoop& loop = *CreateEventLoop();
///   auto workerPool = core::WorkerPool::Create(4);
///   auto registry   = discovery::ServiceRegistry::Create();
///   auto monitor    = monitor::Monitor::Create();
///
///   mmorpg::ServiceContext ctx{loop, registry, workerPool, monitor};
///
///   GateService gate(ctx, jwtCfg);
///   GameService game(ctx, masterRpc);
struct ServiceContext {
    core::IEventLoop&         loop;
    discovery::ServiceRegistry::Ptr registry;
    core::WorkerPool::Ptr     workerPool = nullptr;   // 可选，用于 WorkerPool 分派
    monitor::Monitor::Ptr     monitor = nullptr;      // 可选，用于指标采集
};

} // namespace mmorpg
} // namespace snc
