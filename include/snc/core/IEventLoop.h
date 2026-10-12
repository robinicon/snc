#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "snc/core/IConnection.h"
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
};

} // namespace core
} // namespace snc
