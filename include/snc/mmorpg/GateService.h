#pragma once

/// @file GateService.h
/// @brief Gate 服 — TCP/WebSocket 接入、JWT 本地验签、消息路由。
///
/// 职责：
///   1. 接收客户端 TCP 连接，启动心跳
///   2. 本地 HS256 验签，建立 fd → userId 映射
///   3. 将客户端消息按 Game 服 ServiceRegistry 轮询转发（0x2000 范围）
///   4. 优雅清理 fd（OnDisconnected_ 回调中清除映射并通知 Game 服）
///
/// 消息流：
///   Client --0x1001(JWT)--> Gate --[验签]--> Gate
///   Client --0x1002(msg)--> Gate --RPC 0x2000--> Game --业务处理--> Gate --TCP--> Client
///
/// 使用方式：
///   GateService gate(registry, jwtCfg);
///   gate.Start(7777, "127.0.0.1", 8001);

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>

#include "snc/core/IEventLoop.h"
#include "snc/core/EventLoop.h"
#include "snc/core/Acceptor.h"
#include "snc/core/TcpConnection.h"
#include "snc/core/IConnection.h"
#include "snc/core/Msg.h"
#include "snc/core/DisconnectReason.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcSession.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {

// ── Gate 服消息 ID ───────────────────────────────────────────────
static constexpr int32_t kMsgGateAuth      = 0x1001;  ///< 客户端认证（携带 JWT）
static constexpr int32_t kMsgGateForward   = 0x1002;  ///< 客户端转发业务消息
static constexpr int32_t kMsgGateToGame    = 0x2000;  ///< Gate→Game 业务消息 ID

// ── GateService ──────────────────────────────────────────────────
class GateService {
public:
    using Ptr       = std::shared_ptr<GateService>;
    using Conn      = core::IConnection;
    using Msg       = core::Msg;
    using EventLoop = core::EventLoopIOCP;   // Windows; Linux/macOS 可替换
    using Acceptor  = core::Acceptor;

    /// port       — Gate 监听端口
    /// registry   — Game 服 ServiceRegistry（用于路由）
    /// jwtCfg     — 与 Account 服相同的密钥，用于本地验签
    GateService(discovery::ServiceRegistry::Ptr registry,
                const auth::JwtConfig& jwtCfg);

    ~GateService() = default;

    /// 启动 Gate 服（阻塞调用）。
    /// host/port — 监听地址和端口。
    bool Start(const std::string& host, uint16_t port);

    /// 停止 Gate 服。
    void Stop();

    /// 当前在线玩家数。
    size_t OnlinePlayerCount() const { return online_players_.load(); }

private:
    // ── 连接管理 ───────────────────────────────────────────────
    void OnNewConnection_(Conn::Ptr conn);
    void OnClientMessage_(Conn::Ptr conn, Msg::Ptr msg);
    void OnClientDisconnected_(Conn::Ptr conn, core::DisconnectReason reason);

    // ── 消息处理 ───────────────────────────────────────────────
    void HandleAuth_(Conn::Ptr conn, const char* payload, size_t len);
    void HandleForward_(Conn::Ptr conn, const char* payload, size_t len);

    // ── 路由到 Game 服 ─────────────────────────────────────────
    void ForwardToGame_(Conn::Ptr conn, int32_t msgId, const char* payload, size_t len);

    // ── 成员 ───────────────────────────────────────────────────
    discovery::ServiceRegistry::Ptr  registry_;
    auth::JwtService                 jwt_service_;
    std::atomic<size_t>              online_players_{0};
    std::unique_ptr<EventLoop>       event_loop_;
    std::unique_ptr<Acceptor>        acceptor_;
    std::atomic<bool>                running_{false};

    // fd → PlayerInfo（OnDisconnect 清理用）
    struct PlayerInfo {
        std::string user_id;
    };
    std::unordered_map<int, std::shared_ptr<PlayerInfo>> fd_to_player_;
    mutable std::mutex fd_mtx_;
};

} // namespace mmorpg
} // namespace snc
