// SPDX-License-Identifier: MIT
// AsioTcpServer — Boost.Asio async_accept based TCP server implementation.
//
// Mirrors the interface of TcpServer but replaces the polling AcceptLoop
// (which used sleep_for(1ms)) with a true async_accept chain provided by
// AsioAcceptor. Connections are registered with the event loop and managed
// through the same callback model.

#include "snc/core_asio/AsioTcpServer.h"
#include "snc/base/Log.h"

namespace snc {
namespace core {

AsioTcpServer::AsioTcpServer(IEventLoop& loop, asio::io_context& ioCtx)
    : loop_(loop)
    , ioCtx_(ioCtx)
    , acceptor_(std::make_unique<AsioAcceptor>(ioCtx_))
{
}

AsioTcpServer::~AsioTcpServer()
{
    Stop();
}

bool AsioTcpServer::Listen(uint16_t port, const std::string& host)
{
    bool ok = acceptor_->Listen(port, host);
    if (!ok) {
        SNC_LOG_ERROR("core_asio", "AsioTcpServer::Listen failed on %s:%d",
                      host.c_str(), port);
        return false;
    }
    SNC_LOG_INFO("core_asio", "AsioTcpServer listening on %s:%d",
                 host.c_str(), port);
    return true;
}

void AsioTcpServer::SetCallbacks(NewConnCb onNew, MsgCb onMsg, CloseCb onClose)
{
    onNewConn_  = std::move(onNew);
    onMsg_      = std::move(onMsg);
    onClose_    = std::move(onClose);
}

void AsioTcpServer::SetHeartbeatCallback(HeartbeatCb hb)
{
    onHeartbeat_ = std::move(hb);
}

void AsioTcpServer::SetDispatchToQueue()
{
    workerPool_ = nullptr;
}

void AsioTcpServer::SetDispatchToWorkerPool(WorkerPool::Ptr pool)
{
    workerPool_ = std::move(pool);
}

void AsioTcpServer::Start()
{
    if (acceptor_->IsListening()) {
        acceptor_->SetNewConnectionCallback(
            [this](IConnection::Ptr conn) { OnAccept(std::move(conn)); });
        acceptor_->StartAccept(ioCtx_);
        SNC_LOG_INFO("core_asio", "AsioTcpServer accept loop started");
    }
}

void AsioTcpServer::Stop()
{
    acceptor_->Shutdown();

    std::vector<IConnection::Ptr> toClose;
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
    SNC_LOG_INFO("core_asio", "AsioTcpServer stopped");
}

size_t AsioTcpServer::ActiveCount() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(connsMutex_));
    return connections_.size();
}

void AsioTcpServer::OnAccept(IConnection::Ptr conn)
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
        connections_[fd] = conn;
    }

    if (onNewConn_) onNewConn_(handle);
    SNC_LOG_INFO("core_asio", "AsioTcpServer new connection accepted, fd=%d", fd);
}

void AsioTcpServer::OnConnMessage(EntityHandle<IConnection> conn, Msg::Ptr msg)
{
    if (onMsg_) onMsg_(conn, std::move(msg));
}

void AsioTcpServer::OnConnClose(IConnection::Ptr conn, DisconnectReason reason)
{
    int fd = conn->Fd();

    loop_.RemoveConnection(conn);
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        connections_.erase(fd);
    }

    if (onClose_) onClose_(conn, reason);
    SNC_LOG_INFO("core_asio", "AsioTcpServer connection closed, fd=%d, reason=%d",
                 fd, static_cast<int>(reason));
}

} // namespace core
} // namespace snc
