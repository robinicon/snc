// SPDX-License-Identifier: MIT
#include "snc/core/TcpServer.h"
#include "snc/base/Log.h"

namespace snc {
namespace core {

TcpServer::TcpServer(IEventLoop& loop)
    : loop_(loop)
    , acceptor_(std::make_unique<Acceptor>())
{
}

TcpServer::~TcpServer()
{
    Stop();
}

bool TcpServer::Listen(uint16_t port, const std::string& host)
{
    bool ok = acceptor_->Listen(port, host);
    if (!ok) {
        SNC_LOG_ERROR("TcpServer", "Listen failed on " + host + ":" + std::to_string(port));
        return false;
    }
    SNC_LOG_INFO("TcpServer", "Listening on " + host + ":" + std::to_string(port));
    return true;
}

void TcpServer::SetCallbacks(NewConnCb onNew, MsgCb onMsg, CloseCb onClose)
{
    onNewConn_  = std::move(onNew);
    onMsg_      = std::move(onMsg);
    onClose_    = std::move(onClose);
}

void TcpServer::SetHeartbeatCallback(HeartbeatCb hb)
{
    onHeartbeat_ = std::move(hb);
}

void TcpServer::SetDispatchToQueue()
{
    workerPool_ = nullptr;
}

void TcpServer::SetDispatchToWorkerPool(WorkerPool::Ptr pool)
{
    workerPool_ = std::move(pool);
}

void TcpServer::Start()
{
    if (accepting_.load()) return;
    accepting_.store(true);
    acceptThread_ = std::make_unique<std::thread>(&TcpServer::AcceptLoop, this);
    SNC_LOG_INFO("TcpServer", "Accept loop started in background thread");
}

void TcpServer::Stop()
{
    accepting_.store(false);
    if (acceptThread_ && acceptThread_->joinable()) {
        acceptThread_->join();
    }
    acceptor_->Shutdown();

    std::vector<TcpConnection::Ptr> toClose;
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        toClose.reserve(connections_.size());
        for (auto& [fd, conn] : connections_) {
            toClose.push_back(conn);
        }
        connections_.clear();
    }
    for (auto& conn : toClose) {
        conn->Close();
    }
    SNC_LOG_INFO("TcpServer", "TcpServer stopped");
}

size_t TcpServer::ActiveCount() const
{
    std::lock_guard<std::mutex> lock(connsMutex_);
    return connections_.size();
}

void TcpServer::AcceptLoop()
{
    while (accepting_.load(std::memory_order_acquire)) {
        auto conn = acceptor_->Accept();
        if (!conn) {
            // No pending connections, yield briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        OnAccept(conn);
    }
}

void TcpServer::OnAccept(TcpConnection::Ptr conn)
{
    int fd = conn->Fd();

    // Register with event loop
    loop_.AddConnection(conn);

    // Bind per-connection callbacks
    EntityHandle<IConnection> handle = conn->GetConnHandle();

    conn->SetMessageCallback([this, handle](EntityHandle<IConnection> c, Msg::Ptr msg) {
        OnConnMessage(c, std::move(msg));
    });
    conn->SetCloseCallback([this, conn](IConnection::Ptr c, DisconnectReason reason) {
        OnConnClose(c, reason);
    });

    // Dispatch strategy
    if (workerPool_) {
        conn->SetWorkerDispatchFn([this](EntityHandle<IConnection> c, Msg::Ptr msg) {
            workerPool_->Submit([this, c = std::move(c), msg = std::move(msg)]() {
                if (onMsg_) onMsg_(c, std::move(msg));
            });
        }, workerPool_.get());
    } else {
        conn->SetEnqueueFn([this, handle](Msg::Ptr msg) {
            if (onMsg_) onMsg_(handle, std::move(msg));
            return true;
        });
    }

    // Store connection
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        connections_[fd] = std::static_pointer_cast<TcpConnection>(conn);
    }

    if (onNewConn_) onNewConn_(handle);
    SNC_LOG_INFO("TcpServer", "New connection accepted, fd=" + std::to_string(fd));
}

void TcpServer::OnConnMessage(EntityHandle<IConnection> conn, Msg::Ptr msg)
{
    if (onMsg_) onMsg_(conn, std::move(msg));
}

void TcpServer::OnConnClose(IConnection::Ptr conn, DisconnectReason reason)
{
    int fd = conn->Fd();

    loop_.RemoveConnection(conn);
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        connections_.erase(fd);
    }

    if (onClose_) onClose_(conn, reason);
    SNC_LOG_INFO("TcpServer", "Connection closed, fd=" + std::to_string(fd)
                 + ", reason=" + std::to_string(static_cast<int>(reason)));
}

} // namespace core
} // namespace snc
