#pragma once

#include <memory>
#include <functional>
#include <string>
#include <mutex>
#include <unordered_map>
#include <asio.hpp>
#include "snc/core/IEventLoop.h"
#include "snc/core/IConnection.h"
#include "snc/core/MsgRouter.h"
#include "snc/core/WorkerPool.h"
#include "snc/core_asio/AsioAcceptor.h"
#include "snc/base/Log.h"

namespace snc {
namespace core {

/// AsioTcpServer — 基于 Boost.Asio 异步接受模型的高层 TCP 接入服务。
///
/// 与 TcpServer 提供完全一致的接口，但底层使用 AsioAcceptor 的 async_accept
/// 链式回调，消除了 AcceptLoop 中的 sleep_for(1ms) 忙等。
///
/// 典型用法：
///   asio::io_context ioCtx;
///   AsioEventLoop loop(ioCtx);
///   AsioTcpServer server(loop, ioCtx);
///   server.SetCallbacks(...);
///   server.Listen(9000);
///   server.Start();
///   loop.Run();
class AsioTcpServer {
public:
    using NewConnCb    = std::function<void(EntityHandle<IConnection>)>;
    using MsgCb        = std::function<void(EntityHandle<IConnection>, Msg::Ptr)>;
    using CloseCb      = std::function<void(IConnection::Ptr, DisconnectReason)>;
    using HeartbeatCb  = std::function<void(IConnection::Ptr)>;

    explicit AsioTcpServer(IEventLoop& loop, asio::io_context& ioCtx);
    ~AsioTcpServer();

    // 禁止拷贝
    AsioTcpServer(const AsioTcpServer&) = delete;
    AsioTcpServer& operator=(const AsioTcpServer&) = delete;

    /// 启动 TCP 监听。port 为监听端口，host 默认为 "0.0.0.0"。
    bool Listen(uint16_t port, const std::string& host = "0.0.0.0");

    /// 设置各回调。可在 Listen() 之前或之后调用。
    void SetCallbacks(NewConnCb onNew, MsgCb onMsg, CloseCb onClose);
    void SetHeartbeatCallback(HeartbeatCb hb);

    /// 设置消息分发目标：全局 MessageQueue（支持背压）或 WorkerPool（直接分派到业务线程）。
    /// 两者都设置时以 WorkerPool 优先。
    void SetDispatchToQueue();
    void SetDispatchToWorkerPool(WorkerPool::Ptr pool);

    /// 启动接受循环（异步，不阻塞）。在 Start() 之前必须已调用 Listen()。
    void Start();

    /// 停止监听，关闭所有活跃连接。
    void Stop();

    /// 当前活跃连接数。
    size_t ActiveCount() const;

private:
    void OnAccept(IConnection::Ptr conn);
    void OnConnMessage(EntityHandle<IConnection> conn, Msg::Ptr msg);
    void OnConnClose(IConnection::Ptr conn, DisconnectReason reason);

    IEventLoop&              loop_;
    asio::io_context&        ioCtx_;
    std::unique_ptr<AsioAcceptor> acceptor_;

    NewConnCb   onNewConn_;
    MsgCb       onMsg_;
    CloseCb     onClose_;
    HeartbeatCb onHeartbeat_;

    WorkerPool::Ptr workerPool_;

    std::mutex              connsMutex_;
    std::unordered_map<int, IConnection::Ptr> connections_;
};

} // namespace core
} // namespace snc
