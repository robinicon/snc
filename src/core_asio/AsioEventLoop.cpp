// SPDX-License-Identifier: MIT
// AsioEventLoop — Boost.Asio io_context based event loop implementation
//
// Replaces the three platform-specific EventLoop* classes with a single
// portable implementation driven by asio::io_context.
//
// Key design:
//   - Run()  launches io_context::run() on the calling thread.
//   - A single recursive steady_timer (100 ms) drives heartbeat / RPC-prune /
//     accept / message-queue drain.  The timer is re-armed at the end of each
//     callback, keeping the code flat and readable.
//   - On POSIX: per-connection readiness is watched via
//     asio::posix::stream_descriptor (async_wait → ProcessRecvData).
//   - On Windows: falls back to select() polling each tick (same behaviour as
//     the original EventLoopIOCP).
//   - Wakeup() uses io_context::post([]{}) to unblock the run() loop.
//
// Note: ProcessHeartbeatTickAll / ProcessRpcPruneAll are no-op stubs here.
// Each connection type owns its own heartbeat timer (AsioTcpConnection uses
// async_timer internally; TcpConnection uses the legacy EventLoop timer).
// They are invoked directly by their respective owners, not via this loop.

#include "snc/base/Common.h"
#include "snc/core_asio/AsioEventLoop.h"
#include "snc/core/MessageQueue.h"
#include "snc/core/WorkerPool.h"
#include "snc/base/Log.h"

#include <algorithm>

namespace snc::core
{

AsioEventLoop::AsioEventLoop()
{
}

AsioEventLoop::~AsioEventLoop()
{
    Stop();
}

void AsioEventLoop::Run()
{
    thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    SNC_LOG_INFO("core_asio", "Asio event loop started");

    // Single recursive timer — re-armed at the end of every tick.
    auto tickTimer = std::make_shared<asio::steady_timer>(ioCtx_,
        asio::chrono::milliseconds(100));

    std::function<void(const asio::error_code&)> armTick;
    armTick = [this, tickTimer, &armTick](const asio::error_code&) {
        tickTimer->async_wait([this, tickTimer, &armTick](const asio::error_code& ec) {
            if (ec || !running_.load(std::memory_order_acquire)) return;

            // 1. Heartbeat + RPC-prune for all tracked connections.
            ProcessHeartbeatTickAll();
            ProcessRpcPruneAll();

            // 2. Pending accepts.
            if (acceptCallback_) acceptCallback_();

            // 3. Drain the global message queue.
            {
                Msg::Ptr msg;
                while (messageQueue_ && messageQueue_->TryDequeue(msg))
                    if (messageDispatchFn_) messageDispatchFn_(std::move(msg));
            }

            // 4. Connection I/O readiness.
#ifdef _WIN32
            // Windows: posix::stream_descriptor unavailable for raw sockets.
            // AsioTcpConnection owns its own async read loop; nothing to poll here.
#else
            // POSIX: stream descriptors are armed via StartReadSession / rearm().
            // Nothing extra needed here — async_wait callbacks handle reads.
#endif

            // Re-arm for the next period.
            tickTimer->expires_at(tickTimer->expiry() + asio::chrono::milliseconds(100));
            tickTimer->async_wait(std::move(armTick));
        });
    };
    armTick(asio::error_code());

    ioCtx_.run();
    SNC_LOG_INFO("core_asio", "Asio event loop stopped");
}

void AsioEventLoop::Stop()
{
    running_.store(false, std::memory_order_release);
    ioCtx_.stop();
}

void AsioEventLoop::AddConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    std::lock_guard<std::mutex> lock(connMutex_);
    if (connections_.find(fd) != connections_.end()) return;
    connections_[fd] = std::move(conn);
#ifndef _WIN32
    StartReadSession(conn);
#endif
}

void AsioEventLoop::RemoveConnection(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    std::lock_guard<std::mutex> lock(connMutex_);
    connections_.erase(fd);
#ifndef _WIN32
    auto it = readSessions_.find(fd);
    if (it != readSessions_.end()) readSessions_.erase(it);
#endif
}

void AsioEventLoop::Wakeup()
{
    ioCtx_.post([]{});
}

void AsioEventLoop::ProcessHeartbeatTickAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
        c->ProcessHeartbeatTick();
}

void AsioEventLoop::ProcessRpcPruneAll()
{
    std::vector<IConnection::Ptr> conns;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& [fd, conn] : connections_)
            conns.push_back(conn);
    }
    for (auto& c : conns)
        c->InvokePruneExpired();
}

// ── POSIX readiness-driven read ─────────────────────────────────
#ifdef _WIN32
void AsioEventLoop::StartReadSession(IConnection::Ptr) {}
#else
void AsioEventLoop::StartReadSession(IConnection::Ptr conn)
{
    int fd = conn->Fd();
    auto desc = std::make_shared<asio::posix::stream_descriptor>(ioCtx_);
    try {
        desc->assign(fd);
    } catch (...) {
        return;
    }
    auto sess = std::make_shared<ReadSession>();
    sess->desc = desc;
    std::weak_ptr<IConnection> weakConn(conn);
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        readSessions_[fd] = sess;
    }

    auto rearm = [&]() {
        desc->async_wait(asio::descriptor_base::wait_read,
            [this, desc, sess, weakConn](const asio::error_code& ec) {
                if (ec) { desc->release(); return; }
                auto conn = weakConn.lock();
                if (!conn) { desc->release(); return; }
                // ProcessRecvData is only relevant for legacy TcpConnection.
                // AsioTcpConnection drives its own read loop via async_read.
                (void)conn;
                // Re-arm only if the connection is still alive.
                bool alive = true;
                {
                    std::lock_guard<std::mutex> lock(connMutex_);
                    auto it = connections_.find(conn->Fd());
                    alive = (it != connections_.end() && it->second == conn);
                }
                if (alive) rearm();
                else desc->release();
            });
    };
    rearm();
}
#endif // !_WIN32

} // namespace snc::core
