#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include "snc/core/IEventLoop.h"
#include "snc/core/Acceptor.h"
#include "snc/core/TcpConnection.h"
#include "snc/core/MsgRouter.h"
#include "snc/core/WorkerPool.h"
#include "snc/core/MessageQueue.h"
#include "snc/base/Log.h"

namespace snc {
namespace core {

/// TcpServer — 高层 TCP 接入服务抽象。
///
/// 封装了 Acceptor + EventLoop + TcpConnection 生命周期的完整组装链路，
/// 使上层业务只需关注消息处理与业务回调，无需手动管理底层组件。
///
/// 典型用法：
///   TcpServer server(eventLoop);
///   server.SetOnNewConnection([](EntityHandle<IConnection> conn) {
///       // 新连接建立，可绑定消息路由
///   });
///   server.SetOnMessage([](EntityHandle<IConnection> conn, Msg::Ptr msg) {
///       // 收到消息
///   });
///   server.SetOnClose([](IConnection::Ptr conn, DisconnectReason reason) {
///       // 连接断开
///   });
///   server.Listen(9000);
///   server.Start();
class TcpServer {
public:
    using NewConnCb    = std::function<void(EntityHandle<IConnection>)>;
    using MsgCb        = std::function<void(EntityHandle<IConnection>, Msg::Ptr)>;
    using CloseCb      = std::function<void(IConnection::Ptr, DisconnectReason)>;
    using HeartbeatCb  = std::function<void(IConnection::Ptr)>;

    explicit TcpServer(IEventLoop& loop);
    ~TcpServer();

    // 禁止拷贝
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /// 启动 TCP 监听。port 为监听端口，host 默认为 "0.0.0.0"。
    /// 返回 true 表示绑定成功，false 表示端口被占用或权限不足。
    bool Listen(uint16_t port, const std::string& host = "0.0.0.0");

    /// 设置各回调。可在 Listen() 之前或之后调用。
    void SetCallbacks(NewConnCb onNew, MsgCb onMsg, CloseCb onClose);
    void SetHeartbeatCallback(HeartbeatCb hb);

    /// 设置消息分发目标：全局 MessageQueue（支持背压）或 WorkerPool（直接分派到业务线程）。
    /// 两者都设置时以 WorkerPool 优先。
    void SetDispatchToQueue();
    void SetDispatchToWorkerPool(WorkerPool::Ptr pool);

    /// 启动（非阻塞）：将 accept 回调注册到事件循环并立即返回。
    /// 调用方应在独立线程中调用 IEventLoop::Run() 驱动循环。
    void Start();

    /// 停止监听，关闭所有活跃连接。
    void Stop();

    /// 当前活跃连接数。
    size_t ActiveCount() const;

private:
    void OnAccept(TcpConnection::Ptr conn);
    void OnConnMessage(EntityHandle<IConnection> conn, Msg::Ptr msg);
    void OnConnClose(IConnection::Ptr conn, DisconnectReason reason);
    void AcceptLoop();

    IEventLoop&        loop_;
    std::unique_ptr<Acceptor> acceptor_;

    NewConnCb   onNewConn_;
    MsgCb       onMsg_;
    CloseCb     onClose_;
    HeartbeatCb onHeartbeat_;

    WorkerPool::Ptr workerPool_;

    std::mutex              connsMutex_;
    std::unordered_map<int, TcpConnection::Ptr> connections_;
    std::atomic<bool>       accepting_{false};
    std::unique_ptr<std::thread> acceptThread_;
};

} // namespace core
} // namespace snc
