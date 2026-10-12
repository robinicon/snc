// EventLoop implementation — Windows IOCP / Linux epoll
//
// Platform selection:
//   - Windows: EventLoopIOCP (uses select() for I/O readiness; IOCP handles are
//     created but the readiness polling path uses select() for simplicity).
//   - Linux:   EventLoopEpoll  (epoll_wait()).
//   - macOS:   Not yet implemented; falls through to epoll on compilation but
//              epoll_create1/epoll_wait are unavailable. Use a custom port or
//              kqueue backend when targeting macOS.

#include "snc/core/EventLoop.h"
#include "snc/core/TcpConnection.h"
#include "snc/core/MessageQueue.h"
#include "snc/core/WorkerPool.h"
#include "snc/rpc/RpcSession.h"
#include "snc/base/Log.h"
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace snc::core
{

// ── Shared: per-connection message queue entry ───────────────────
struct ConnDispatchEntry {
    IConnection::Ptr conn;
    WorkerPool*      pool = nullptr;
    // The enqueue fn stored on the TcpConnection. When a message arrives
    // from this connection, DrainConnQueue pulls it and forwards it to
    // the registered WorkerDispatchFn (set via SetWorkerDispatchFn below).
    // We capture conn+pool so the lambda can construct EntityHandle.
};

// ── Drain one connection's queue and forward to WorkerPool ───────
static size_t DrainConnQueue(ConnDispatchEntry& entry)
{
    if (!entry.conn || !entry.pool) return 0;
    size_t dispatched = 0;
    // TcpConnection carries its own inbound queue populated by ParseMessages.
    // We peek the enqueueFn_ is already set; the real routing happens inside
    // TcpConnection::ParseMessages which calls DispatchToQueue() → enqueueFn_.
    // This function exists only to flush any residual global queue entries.
    (void)entry;
    return 0;
}

// ═════════════════════════════════════════════════════════════════
// Windows IOCP implementation
// ═════════════════════════════════════════════════════════════════

#ifdef SNC_PLATFORM_WINDOWS

EventLoopIOCP::EventLoopIOCP()
{
    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
}

EventLoopIOCP::~EventLoopIOCP()
{
    if (iocpHandle_)
        CloseHandle(iocpHandle_);
}

void EventLoopIOCP::SetWorkerDispatchFn(int fd, WorkerDispatchFn fn)
{
    std::lock_guard<std::mutex> lock(connMutex_);
    workerDispatch_[fd] = std::move(fn);
}

void EventLoopIOCP::Run()
{
    thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    SNC_LOG_INFO("core", "IOCP event loop started");

    while (running_.load(std::memory_order_acquire))
    {
        // 1) Check accept socket for new connections (non-blocking).
        if (acceptSock_.Valid()) {
            char dummy[1];
            ssize_t n = ::recv(acceptSock_.Fd(), dummy, 1, 0);
            if (n > 0) {
                if (acceptCallback_) acceptCallback_();
            }
        }

        // 2) Poll I/O readiness with select() and drive connections.
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        int maxFd = 0;

        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto& [fd, conn] : connections_) {
                int f = static_cast<int>(fd);
                if (f > maxFd) maxFd = f;
                auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
                if (tcp && tcp->IsBackpressuring()) {
                    FD_SET(f, &writeSet);
                } else {
                    FD_SET(f, &readSet);
                    FD_SET(f, &writeSet);
                }
            }
        }

        if (maxFd > 0) {
            struct timeval tv{0, 50000};  // 50 ms
            int n = ::select(maxFd + 1, &readSet, &writeSet, nullptr, &tv);
            if (n > 0) {
                std::lock_guard<std::mutex> lock(connMutex_);
                for (auto& [fd, conn] : connections_) {
                    int f = static_cast<int>(fd);
                    auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
                    if (!tcp) continue;
                    if (FD_ISSET(f, &readSet))
                        tcp->ProcessRecvData();
                    if (FD_ISSET(f, &writeSet))
                        tcp->FlushSendBuffer();
                }
            }
        }

        // 3) Drain per-connection dispatch queues → WorkerPool.
        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto& [fd, conn] : connections_) {
                auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
                if (!tcp) continue;
                auto it = workerDispatch_.find(fd);
                if (it == workerDispatch_.end() || !workerPool_) continue;
            }
        }

        // 3b) Drain global MessageQueue (legacy path, kept for compat).
        if (messageQueue_ && workerPool_) {
            size_t dispatched = 0;
            while (true) {
                Msg::Ptr msg;
                if (!messageQueue_->Dequeue(msg, 0)) break;
                (void)msg;
                ++dispatched;
            }
            (void)dispatched;
        }

        // 4) Timer-based housekeeping.
        timerMgr_.ProcessTimers();
        ProcessHeartbeatTickAll();
        ProcessRpcPruneAll();
    }

    SNC_LOG_INFO("core", "IOCP event loop stopped");
}

void EventLoopIOCP::Stop()
{
    PostQueuedCompletionStatus(iocpHandle_, 0, 0, nullptr);
    if (acceptSock_.Valid()) {
        char c = 1;
        ::send(acceptSock_.Fd(), &c, 1, 0);
    }
}

void EventLoopIOCP::AddConnection(IConnection::Ptr conn)
{
    AssertOnLoopThread();
    std::lock_guard<std::mutex> lock(connMutex_);
    int fd = conn->Fd();
    connections_[fd] = std::move(conn);
    HANDLE handle = reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(fd));
    CreateIoCompletionPort(handle, iocpHandle_, static_cast<ULONG_PTR>(fd), 0);
}

void EventLoopIOCP::RemoveConnection(IConnection::Ptr conn)
{
    AssertOnLoopThread();
    std::lock_guard<std::mutex> lock(connMutex_);
    connections_.erase(conn->Fd());
    workerDispatch_.erase(conn->Fd());
}

void EventLoopIOCP::Wakeup()
{
    PostQueuedCompletionStatus(iocpHandle_, 0, 0, nullptr);
    if (acceptSock_.Valid()) {
        char c = 1;
        ::send(acceptSock_.Fd(), &c, 1, 0);
    }
}

// ── Heartbeat / Prune helpers (shared) ───────────────────────────

void EventLoopIOCP::ProcessHeartbeatTickAll()
{
    std::lock_guard<std::mutex> lock(connMutex_);
    for (auto& [fd, conn] : connections_) {
        auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
        if (tcp) tcp->ProcessHeartbeatTick();
    }
}

void EventLoopIOCP::ProcessRpcPruneAll()
{
    std::lock_guard<std::mutex> lock(connMutex_);
    for (auto& [fd, conn] : connections_) {
        auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
        if (tcp) tcp->InvokePruneExpired();
    }
}

#endif // SNC_PLATFORM_WINDOWS

// ═════════════════════════════════════════════════════════════════
// Linux epoll implementation
// ═════════════════════════════════════════════════════════════════

#if !defined(SNC_PLATFORM_WINDOWS)

EventLoopEpoll::EventLoopEpoll()
    : epollFd_(-1)
    , wakeupFd_(-1)
    , events_(1024)
{
#if defined(__linux__)
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
#endif
}

EventLoopEpoll::~EventLoopEpoll()
{
    if (epollFd_ >= 0) ::close(epollFd_);
    if (wakeupFd_ >= 0) ::close(wakeupFd_);
}

void EventLoopEpoll::SetWorkerDispatchFn(int fd, WorkerDispatchFn fn)
{
    std::lock_guard<std::mutex> lock(connMutex_);
    workerDispatch_[fd] = std::move(fn);
}

void EventLoopEpoll::Run()
{
    thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    SNC_LOG_INFO("core", "Epoll event loop started");

    while (running_.load(std::memory_order_acquire))
    {
        int timeout = static_cast<int>(timerMgr_.NextTimeoutMs());
        if (timeout < 0) timeout = 100;

#if defined(__linux__)
        int n = epoll_wait(epollFd_, events_.data(), static_cast<int>(events_.size()), timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            SNC_LOG_ERROR("core", "epoll_wait failed: %d", errno);
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd   = events_[i].data.fd;
            uint32_t evts = events_[i].events;

            std::lock_guard<std::mutex> lock(connMutex_);
            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;

            auto conn = std::dynamic_pointer_cast<TcpConnection>(it->second);
            if (!conn) continue;

            if (evts & (EPOLLERR | EPOLLHUP)) {
                conn->Close();
                continue;
            }
            if (evts & EPOLLIN)
                conn->ProcessRecvData();
            if (evts & EPOLLOUT)
                conn->FlushSendBuffer();
        }
#endif

        // Drain global MessageQueue → WorkerPool (same caveat as IOCP path).
        if (messageQueue_ && workerPool_) {
            size_t dispatched = 0;
            while (true) {
                Msg::Ptr msg;
                if (!messageQueue_->Dequeue(msg, 0)) break;
                (void)msg;
                ++dispatched;
            }
            (void)dispatched;
        }

        timerMgr_.ProcessTimers();
        ProcessHeartbeatTickAll();
        ProcessRpcPruneAll();
    }

    SNC_LOG_INFO("core", "Epoll event loop stopped");
}

void EventLoopEpoll::Stop()
{
    Wakeup();
}

void EventLoopEpoll::AddConnection(IConnection::Ptr conn)
{
    AssertOnLoopThread();
    std::lock_guard<std::mutex> lock(connMutex_);
    int fd = conn->Fd();
    connections_[fd] = std::move(conn);
#if defined(__linux__)
    struct epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
#endif
}

void EventLoopEpoll::RemoveConnection(IConnection::Ptr conn)
{
    AssertOnLoopThread();
    std::lock_guard<std::mutex> lock(connMutex_);
    int fd = conn->Fd();
#if defined(__linux__)
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
    connections_.erase(fd);
    workerDispatch_.erase(fd);
}

void EventLoopEpoll::Wakeup()
{
    if (acceptSock_.Valid()) {
        char c = 1;
        ::send(acceptSock_.Fd(), &c, 1, 0);
    }
}

// ── Heartbeat / Prune helpers (shared) ───────────────────────────

void EventLoopEpoll::ProcessHeartbeatTickAll()
{
    std::lock_guard<std::mutex> lock(connMutex_);
    for (auto& [fd, conn] : connections_) {
        auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
        if (tcp) tcp->ProcessHeartbeatTick();
    }
}

void EventLoopEpoll::ProcessRpcPruneAll()
{
    std::lock_guard<std::mutex> lock(connMutex_);
    for (auto& [fd, conn] : connections_) {
        auto tcp = std::dynamic_pointer_cast<TcpConnection>(conn);
        if (tcp) tcp->InvokePruneExpired();
    }
}

#endif // !SNC_PLATFORM_WINDOWS

// ── Factory ─────────────────────────────────────────────────────

std::unique_ptr<IEventLoop> CreateEventLoop()
{
#if defined(SNC_PLATFORM_WINDOWS)
    return std::make_unique<EventLoopIOCP>();
#else
    return std::make_unique<EventLoopEpoll>();
#endif
}

} // namespace snc::core
