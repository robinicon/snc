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
    // Set the global message queue dispatch handler (called inside a WorkerPool task).
    // Default: logs a warning and discards (safe no-op). Set this before starting
    // the event loop to wire in your application's message router.
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) {
        messageDispatchFn_ = std::move(fn);
    }

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
    // Global message queue dispatch callback (invoked inside a WorkerPool thread).
    std::function<void(Msg::Ptr)> messageDispatchFn_;
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
    void SetAcceptSock(SocketWrap sock) { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) { acceptCallback_ = std::move(cb); }
    void SetWorkerDispatchFn(int fd, WorkerDispatchFn fn);
    // Set the global message queue dispatch handler (called inside a WorkerPool task).
    // Default: logs a warning and discards (safe no-op). Set this before starting
    // the event loop to wire in your application's message router.
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) {
        messageDispatchFn_ = std::move(fn);
    }

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

    int    epollFd_       = -1;
    int    wakeupPipe_[2] = {-1, -1};
    std::vector<struct epoll_event> events_;
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
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};
#endif // !SNC_PLATFORM_WINDOWS

#if defined(SNC_PLATFORM_MACOS)
// ── macOS kqueue implementation ─────────────────────────────────
// kqueue is the BSD equivalent of Linux epoll and Windows IOCP.
// Supported filters: EVFILT_READ, EVFILT_WRITE, EVFILT_TIMER, EVFILT_USER.
class EventLoopKqueue : public IEventLoop {
public:
    EventLoopKqueue();
    ~EventLoopKqueue() override;

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
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) {
        messageDispatchFn_ = std::move(fn);
    }

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

    int    kqueueFd_       = -1;
    int    wakeupPipe_[2]  = {-1, -1};
    std::vector<struct kevent> changeList_;
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
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};
#endif // SNC_PLATFORM_MACOS

std::unique_ptr<IEventLoop> CreateEventLoop();

} // namespace core
} // namespace snc
