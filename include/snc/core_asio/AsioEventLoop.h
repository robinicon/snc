#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <thread>
#include <asio.hpp>
#include "snc/core/IEventLoop.h"
#include "snc/core/IConnection.h"
#include "snc/core/SocketWrap.h"
#include "snc/core/TimerManager.h"

namespace snc {
namespace core {

class MessageQueue;
class WorkerPool;

// ═══════════════════════════════════════════════════════════════
// Asio 事件循环实现
// ═══════════════════════════════════════════════════════════════
/// AsioEventLoop：基于 Boost.Asio io_context 的平台无关事件循环。
/// 底层使用 asio::io_context 作为调度器：
///   - Run() 线程中调用 io_context::run()
///   - Wakeup() 通过 io_context::post() 触发
///   - 连接可读性通过 asio::posix::stream_descriptor::wait_read 监听
///     （Windows 无 posix 套接字，回退到 select 轮询）
/// connections_ map 由 std::mutex 保护，所有访问均持锁。
class AsioEventLoop : public IEventLoop {
public:
    AsioEventLoop();
    ~AsioEventLoop() override;

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
    void SetAcceptSock(SocketWrap sock) override { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) override { acceptCallback_ = std::move(cb); }
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) override {
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

    /// 驱动单条连接的可读就绪检测（POSIX）。在 ReadSession 析构时释放 fd 所有权，避免 double-close。
    void StartReadSession(IConnection::Ptr conn);

    asio::io_context ioCtx_;
    std::atomic<bool> running_{false};
    std::thread::id   thread_id_;
    std::mutex        connMutex_;
    /// fd → 连接的映射，所有访问需在 connMutex_ 保护下进行。
    std::unordered_map<int, IConnection::Ptr> connections_;
    TimerManager timerMgr_;

    /// 唤醒管道写端（跨线程唤醒时使用；POSIX 下可省略，但保留以与现有语义对齐）。
    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;

    MessageQueue*          messageQueue_ = nullptr;
    WorkerPool*            workerPool_   = nullptr;
    std::function<void(Msg::Ptr)> messageDispatchFn_;

    /// per-connection read session（异步可读等待），按 fd 索引，受 connMutex_ 保护。
    struct ReadSession {
        std::shared_ptr<asio::posix::stream_descriptor> desc;
    };
    std::unordered_map<int, std::shared_ptr<ReadSession>> readSessions_;
};

} // namespace core
} // namespace snc
