#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <thread>
#include <cassert>
#include "snc/core/IEventLoop.h"
#include "snc/core/IConnection.h"
#include "snc/core/SocketWrap.h"
#include "snc/core/TimerManager.h"
#include "snc/core/TcpConnection.h"

namespace snc {
namespace core {

class MessageQueue;
class WorkerPool;

// Forward-declared for use in the map below (defined in TcpConnection.h).
using WorkerDispatchFn = std::function<void(EntityHandle<IConnection>, Msg::Ptr)>;

class EventLoopIOCP : public IEventLoop {
public:
    EventLoopIOCP();
    ~EventLoopIOCP() override;

    void Run() override;
    void Stop() override;
    void AddConnection(IConnection::Ptr conn) override;
    void RemoveConnection(IConnection::Ptr conn) override;
    void Wakeup() override;
    bool IsRunning() const override { return running_.load(std::memory_order_acquire); }
    TimerManager& TimerMgr() override {
        AssertOnLoopThread();
        return timerMgr_;
    }
    void SetMessageQueue(MessageQueue* q) override { messageQueue_ = q; }
    void SetWorkerPool(WorkerPool* pool) override { workerPool_ = pool; }
    void SetAcceptSock(SocketWrap sock) { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) { acceptCallback_ = std::move(cb); }
    void SetWorkerDispatchFn(int fd, WorkerDispatchFn fn);

protected:
    // Called after every ProcessTimers() cycle.  Override to add per-platform logic.
    virtual void ProcessHeartbeatTickAll();
    virtual void ProcessRpcPruneAll();

private:
    void AssertOnLoopThread() const {
#ifdef SNC_DEBUG
        assert(std::this_thread::get_id() == thread_id_);
        (void)thread_id_; // suppress unused warning in release builds
#endif
    }

    void* iocpHandle_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread::id   thread_id_;
    std::mutex connMutex_;
    std::unordered_map<int, IConnection::Ptr> connections_;
    TimerManager timerMgr_;

    // Accept socket wakeup pipe (write to this to wake the event loop).
    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;

    // Message queue / worker pool for IO→Worker dispatch.
    MessageQueue*          messageQueue_ = nullptr;
    WorkerPool*            workerPool_   = nullptr;
    // per-connection worker dispatch functions (guarded by connMutex_)
    std::unordered_map<int, WorkerDispatchFn> workerDispatch_;
};

#if !defined(SNC_PLATFORM_WINDOWS)
class EventLoopEpoll : public IEventLoop {
public:
    EventLoopEpoll();
    ~EventLoopEpoll() override;

    void Run() override;
    void Stop() override;
    void AddConnection(IConnection::Ptr conn) override;
    void RemoveConnection(IConnection::Ptr conn) override;
    void Wakeup() override;
    bool IsRunning() const override { return running_.load(std::memory_order_acquire); }
    TimerManager& TimerMgr() override {
        AssertOnLoopThread();
        return timerMgr_;
    }
    void SetMessageQueue(MessageQueue* q) override { messageQueue_ = q; }
    void SetWorkerPool(WorkerPool* pool) override { workerPool_ = pool; }
    void SetWorkerDispatchFn(int fd, WorkerDispatchFn fn);
    void SetAcceptSock(SocketWrap sock) { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) { acceptCallback_ = std::move(cb); }

protected:
    virtual void ProcessHeartbeatTickAll();
    virtual void ProcessRpcPruneAll();

private:
    void AssertOnLoopThread() const {
#ifdef SNC_DEBUG
        assert(std::this_thread::get_id() == thread_id_);
        (void)thread_id_;
#endif
    }

    int epollFd_ = -1;
    int wakeupFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread::id   thread_id_;
    std::mutex connMutex_;
    std::unordered_map<int, IConnection::Ptr> connections_;
    TimerManager timerMgr_;

    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;

    MessageQueue* messageQueue_ = nullptr;
    WorkerPool*   workerPool_   = nullptr;
    std::unordered_map<int, WorkerDispatchFn> workerDispatch_;
};
#endif

std::unique_ptr<IEventLoop> CreateEventLoop();

} // namespace core
} // namespace snc
