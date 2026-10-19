// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "snc/core/IConnection.h"
#include "snc/core/SocketWrap.h"
#include "snc/core/TimerManager.h"

namespace snc {
namespace core {

class MessageQueue;
class WorkerPool;

class IEventLoop {
public:
    virtual ~IEventLoop() {}
    virtual void Run() = 0;
    virtual void Stop() = 0;

    // ── Platform helpers ────────────────────────────────────────────
    virtual void AddConnection(IConnection::Ptr conn) = 0;
    virtual void RemoveConnection(IConnection::Ptr conn) = 0;
    virtual void Wakeup() = 0;
    virtual bool IsRunning() const = 0;
    virtual TimerManager& TimerMgr() = 0;
    virtual void SetMessageQueue(MessageQueue* q) = 0;
    virtual void SetWorkerPool(WorkerPool* pool) = 0;
    // Accept-loop hooks — concrete implementations wire these up; default no-ops
    // keep the interface safe to use through IEventLoop* without casts.
    virtual void SetAcceptSock(SocketWrap sock) {}
    virtual void SetAcceptCallback(std::function<void()> cb) {}
    virtual void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) {}
    // Heartbeat / RPC pruning — default no-ops, safe to call through IEventLoop*
    virtual void ProcessHeartbeatTickAll() {}
    virtual void ProcessRpcPruneAll() {}
};

} // namespace core
} // namespace snc
