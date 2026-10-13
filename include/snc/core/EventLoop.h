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

// ═══════════════════════════════════════════════════════════════
// Windows IOCP / select 事件循环实现
// ═══════════════════════════════════════════════════════════════
/// EventLoopIOCP：Windows 平台事件循环。
/// 底层使用 select() 进行 I/O 就绪轮询（非原生 IOCP，IOCP 需额外线程池配合）。
/// 每条连接注册在 connections_ map 中，按 fd 索引。
class EventLoopIOCP : public IEventLoop {
public:
    EventLoopIOCP();
    ~EventLoopIOCP() override;

    /// 启动事件循环（阻塞调用，应在独立线程中运行）。
    void Run() override;
    /// 请求停止事件循环（设置 running_ = false）。
    void Stop() override;
    /// 注册新连接，添加到 connections_ map。
    void AddConnection(IConnection::Ptr conn) override;
    /// 移除连接，从 connections_ map 中删除。
    void RemoveConnection(IConnection::Ptr conn) override;
    /// 唤醒事件循环（Windows select 版无需操作）。
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

    /// 设置全局消息队列分发回调（在 WorkerPool 线程内执行）。
    /// 默认为安全空操作（打印警告并丢弃）。启动事件循环前调用以接入业务路由。
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) {
        messageDispatchFn_ = std::move(fn);
    }

protected:
    /// 每个 ProcessTimers() 周期结束后调用，供子类扩展心跳/熔断检测逻辑。
    virtual void ProcessHeartbeatTickAll();
    virtual void ProcessRpcPruneAll();

private:
    /// 断言当前线程即为事件循环线程（SNC_DEBUG 模式下检查）。
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
    /// fd → 连接的映射，所有访问需在 connMutex_ 保护下进行。
    std::unordered_map<int, IConnection::Ptr> connections_;
    TimerManager timerMgr_;

    /// 唤醒管道写端（向此 fd 写入字节可中断 select() 阻塞）。
    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;

    /// IO→Worker 分派目标（消息队列或 WorkerPool）。
    MessageQueue*          messageQueue_ = nullptr;
    WorkerPool*            workerPool_   = nullptr;
    /// 全局消息队列分发回调（在 WorkerPool 线程内执行）。
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};

// ═══════════════════════════════════════════════════════════════
// Linux epoll 事件循环实现
// ═══════════════════════════════════════════════════════════════
/// EventLoopEpoll：Linux 平台事件循环。
/// 使用 epoll_wait() 高效等待 I/O 事件（水平触发 + ET 混合模式）。
/// wakeupPipe_ 自绘管道用于跨线程唤醒（其他线程向其写端写字节即可）。
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

    int    epollFd_       = -1;         /// epoll 实例 fd
    int    wakeupPipe_[2] = {-1, -1};   /// 自绘唤醒管道 [0]=读端, [1]=写端
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
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};
#endif // !SNC_PLATFORM_WINDOWS

// ═══════════════════════════════════════════════════════════════
// macOS kqueue 事件循环实现
// ═══════════════════════════════════════════════════════════════
/// EventLoopKqueue：macOS/BSD 平台事件循环。
/// kqueue 是 BSD 系的 epoll 等价物，支持 EVFILT_READ / EVFILT_WRITE / EVFILT_TIMER / EVFILT_USER。
#if defined(SNC_PLATFORM_MACOS)
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

    int    kqueueFd_       = -1;        /// kqueue 实例 fd
    int    wakeupPipe_[2]  = {-1, -1};  /// 自绘唤醒管道
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
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};
#endif // SNC_PLATFORM_MACOS

/// 工厂函数：根据当前平台创建对应的事件循环实例。
/// Windows  → EventLoopIOCP（select 轮询）
/// Linux    → EventLoopEpoll
/// macOS    → EventLoopKqueue
std::unique_ptr<IEventLoop> CreateEventLoop();

} // namespace core
} // namespace snc
