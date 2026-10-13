// EventLoop implementation — Windows select / Linux epoll / macOS kqueue
//
// Platform selection:
//   - Windows: EventLoopIOCP  (uses select() for I/O readiness polling).
//   - Linux:   EventLoopEpoll  (epoll_wait()).
//   - macOS:   EventLoopKqueue (kevent() – BSD equivalent of epoll).

#include "snc/base/Common.h"
#include "snc/core/EventLoop.h"
#include "snc/core/TcpConnection.h"
#include "snc/core/MessageQueue.h"
#include "snc/core/WorkerPool.h"
#include "snc/base/Log.h"
#include <cstring>
#include <algorithm>

namespace snc::core
{

// ═════════════════════════════════════════════════════════════════
// Windows IOCP / select implementation
// ═════════════════════════════════════════════════════════════════

#ifdef SNC_PLATFORM_WINDOWS

EventLoopIOCP::EventLoopIOCP()
{
}

EventLoopIOCP::~EventLoopIOCP()
{
}

void EventLoopIOCP::Run()
{
    thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    SNC_LOG_INFO("core", "IOCP event loop started");

    while (running_.load(std::memory_order_acquire))
    {
        // Process pending accepts before I/O polling
        if (acceptCallback_) acceptCallback_();

        // Collect open connections
        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> lock(connMutex_);
            fds.reserve(connections_.size());
            for (auto& [fd, conn] : connections_)
                fds.push_back(fd);
        }

        if (fds.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Use select() for I/O readiness polling
        fd_set readSet;
        FD_ZERO(&readSet);
        int maxFd = 0;
        for (int fd : fds)
        {
            FD_SET(fd, &readSet);
            if (fd > maxFd) maxFd = fd;
        }

        timeval tv = {0, 100000}; // 100ms timeout
        int ret = select(maxFd + 1, &readSet, nullptr, nullptr, &tv);

        if (ret > 0)
        {
            for (int fd : fds)
            {
                if (FD_ISSET(fd, &readSet))
                {
                    IConnection::Ptr conn;
                    {
                        std::lock_guard<std::mutex> lock(connMutex_);
                        auto it = connections_.find(fd);
                        if (it != connections_.end())
                            conn = it->second;
                    }
                    if (conn)
                    {
                        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(conn);
                        if (tcpConn) tcpConn->ProcessRecvData();
                    }
                }
            }
        }

        // Drain message queue
        {
            Msg::Ptr msg;
            while (messageQueue_ && messageQueue_->TryDequeue(msg))
            {
                if (messageDispatchFn_)
                    messageDispatchFn_(std::move(msg));
            }
        }
    }

    SNC_LOG_INFO("core", "IOCP event loop stopped");
}

void EventLoopIOCP::Stop()
{
    running_.store(false, std::memory_order_release);
}

void EventLoopIOCP::AddConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    std::lock_guard<std::mutex> lock(connMutex_);
    connections_[fd] = std::move(conn);
}

void EventLoopIOCP::RemoveConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    std::lock_guard<std::mutex> lock(connMutex_);
    connections_.erase(fd);
}

void EventLoopIOCP::Wakeup()
{
    // No-op for IOCP select-based loop
}

void EventLoopIOCP::ProcessHeartbeatTickAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
    {
        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(c);
        if (tcpConn) tcpConn->ProcessHeartbeatTick();
    }
}

void EventLoopIOCP::ProcessRpcPruneAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
    {
        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(c);
        if (tcpConn) tcpConn->InvokePruneExpired();
    }
}

#endif // SNC_PLATFORM_WINDOWS

// ═════════════════════════════════════════════════════════════════
// Linux epoll implementation
// ═════════════════════════════════════════════════════════════════

#ifdef SNC_PLATFORM_LINUX

EventLoopEpoll::EventLoopEpoll()
    : epollFd_(epoll_create1(0))
{
    if (epollFd_ < 0)
        throw std::runtime_error("Failed to create epoll fd");

    // Create self-pipe for cross-thread wakeup
    if (pipe(wakeupPipe_) < 0)
        throw std::runtime_error("Failed to create wakeup pipe");

    // Make wakeup read end non-blocking
    fcntl(wakeupPipe_[0], F_SETFL, O_NONBLOCK);

    // Register read event on wakeup fd
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.fd = wakeupPipe_[0];
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeupPipe_[0], &ev);
}

EventLoopEpoll::~EventLoopEpoll()
{
    if (epollFd_ >= 0) close(epollFd_);
    if (wakeupPipe_[0] >= 0) close(wakeupPipe_[0]);
    if (wakeupPipe_[1] >= 0) close(wakeupPipe_[1]);
}

void EventLoopEpoll::Run()
{
    thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    SNC_LOG_INFO("core", "epoll event loop started");

    constexpr int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (running_.load(std::memory_order_acquire))
    {
        // Process pending accepts before I/O polling
        if (acceptCallback_) acceptCallback_();

        int n = epoll_wait(epollFd_, events, MAX_EVENTS, 100); // 100ms timeout
        if (n < 0)
        {
            if (errno == EINTR) continue;
            SNC_LOG_ERROR("core", "epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            // Handle wakeup event
            if (events[i].data.fd == wakeupPipe_[0])
            {
                char buf[64];
                recv(wakeupPipe_[0], buf, sizeof(buf), MSG_DONTWAIT);
                continue;
            }

            int fd = events[i].data.fd;
            IConnection::Ptr conn;
            {
                std::lock_guard<std::mutex> lock(connMutex_);
                auto it = connections_.find(fd);
                if (it != connections_.end())
                    conn = it->second;
            }
            if (conn)
            {
                if (events[i].events & EPOLLIN)
                {
                    auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(conn);
                    if (tcpConn) tcpConn->ProcessRecvData();
                }
            }
        }

        // Drain message queue
        {
            Msg::Ptr msg;
            while (messageQueue_ && messageQueue_->TryDequeue(msg))
            {
                if (messageDispatchFn_)
                    messageDispatchFn_(std::move(msg));
            }
        }
    }

    SNC_LOG_INFO("core", "epoll event loop stopped");
}

void EventLoopEpoll::Stop()
{
    running_.store(false, std::memory_order_release);
}

void EventLoopEpoll::AddConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    std::lock_guard<std::mutex> lock(connMutex_);
    if (connections_.find(fd) != connections_.end())
        return;
    connections_[fd] = std::move(conn);

    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

void EventLoopEpoll::RemoveConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    std::lock_guard<std::mutex> lock(connMutex_);
    connections_.erase(fd);
}

void EventLoopEpoll::Wakeup()
{
    char c = 'w';
    send(wakeupPipe_[1], &c, 1, MSG_DONTWAIT);
}

void EventLoopEpoll::ProcessHeartbeatTickAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
    {
        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(c);
        if (tcpConn) tcpConn->ProcessHeartbeatTick();
    }
}

void EventLoopEpoll::ProcessRpcPruneAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
    {
        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(c);
        if (tcpConn) tcpConn->InvokePruneExpired();
    }
}

#endif // SNC_PLATFORM_LINUX

// ═════════════════════════════════════════════════════════════════
// macOS kqueue implementation
// ═════════════════════════════════════════════════════════════════

#ifdef SNC_PLATFORM_MACOS

EventLoopKqueue::EventLoopKqueue()
    : kqueueFd_(kqueue())
{
    if (kqueueFd_ < 0)
        throw std::runtime_error("Failed to create kqueue fd");

    // Create self-pipe for cross-thread wakeup
    if (pipe(wakeupPipe_) < 0)
        throw std::runtime_error("Failed to create wakeup pipe");

    // Make wakeup read end non-blocking
    fcntl(wakeupPipe_[0], F_SETFL, O_NONBLOCK);

    // Register read event on wakeup fd
    struct kevent ev = {};
    EV_SET(&ev, wakeupPipe_[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    kevent(kqueueFd_, &ev, 1, nullptr, 0, nullptr);
}

EventLoopKqueue::~EventLoopKqueue()
{
    if (kqueueFd_ >= 0) close(kqueueFd_);
    if (wakeupPipe_[0] >= 0) close(wakeupPipe_[0]);
    if (wakeupPipe_[1] >= 0) close(wakeupPipe_[1]);
}

void EventLoopKqueue::Run()
{
    thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    SNC_LOG_INFO("core", "kqueue event loop started");

    constexpr int MAX_EVENTS = 64;
    struct kevent events[MAX_EVENTS];

    while (running_.load(std::memory_order_acquire))
    {
        // Process pending accepts before I/O polling
        if (acceptCallback_) acceptCallback_();

        int n = kevent(kqueueFd_, nullptr, 0, events, MAX_EVENTS, nullptr); // blocking
        if (n < 0)
        {
            if (errno == EINTR) continue;
            SNC_LOG_ERROR("core", "kevent failed: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            // Handle wakeup event
            if (events[i].ident == wakeupPipe_[0])
            {
                char buf[64];
                recv(wakeupPipe_[0], buf, sizeof(buf), MSG_DONTWAIT);
                continue;
            }

            int fd = static_cast<int>(events[i].ident);
            IConnection::Ptr conn;
            {
                std::lock_guard<std::mutex> lock(connMutex_);
                auto it = connections_.find(fd);
                if (it != connections_.end())
                    conn = it->second;
            }
            if (conn)
            {
                if (events[i].filter == EVFILT_READ)
                {
                    auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(conn);
                    if (tcpConn) tcpConn->ProcessRecvData();
                }
            }
        }

        // Drain message queue
        {
            Msg::Ptr msg;
            while (messageQueue_ && messageQueue_->TryDequeue(msg))
            {
                if (messageDispatchFn_)
                    messageDispatchFn_(std::move(msg));
            }
        }
    }

    SNC_LOG_INFO("core", "kqueue event loop stopped");
}

void EventLoopKqueue::Stop()
{
    running_.store(false, std::memory_order_release);
}

void EventLoopKqueue::AddConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    std::lock_guard<std::mutex> lock(connMutex_);
    if (connections_.find(fd) != connections_.end())
        return;
    connections_[fd] = std::move(conn);

    // Register read event
    struct kevent ev = {};
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    kevent(kqueueFd_, &ev, 1, nullptr, 0, nullptr);
}

void EventLoopKqueue::RemoveConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    // Unregister event
    struct kevent ev = {};
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(kqueueFd_, &ev, 1, nullptr, 0, nullptr);

    std::lock_guard<std::mutex> lock(connMutex_);
    connections_.erase(fd);
}

void EventLoopKqueue::Wakeup()
{
    // Write to self-pipe to wake up the event loop
    const char* msg = "w";
    send(wakeupPipe_[1], msg, 1, 0);
}

void EventLoopKqueue::ProcessHeartbeatTickAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
    {
        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(c);
        if (tcpConn) tcpConn->ProcessHeartbeatTick();
    }
}

void EventLoopKqueue::ProcessRpcPruneAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
    {
        auto tcpConn = std::dynamic_pointer_cast<TcpConnection>(c);
        if (tcpConn) tcpConn->InvokePruneExpired();
    }
}

#endif // SNC_PLATFORM_MACOS

// ── Factory ─────────────────────────────────────────────────────
std::unique_ptr<IEventLoop> CreateEventLoop()
{
#ifdef SNC_PLATFORM_WINDOWS
    return std::make_unique<EventLoopIOCP>();
#elif defined(SNC_PLATFORM_LINUX)
    return std::make_unique<EventLoopEpoll>();
#elif defined(SNC_PLATFORM_MACOS)
    return std::make_unique<EventLoopKqueue>();
#else
    #error "Unsupported platform"
#endif
}

} // namespace snc::core
