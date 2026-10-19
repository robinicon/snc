// SPDX-License-Identifier: MIT
#pragma once

/// @file MasterService.h
/// @brief Master 服 — 全局服务状态管理、GM 命令分发、健康统计。
///
/// 职责：
///   - 接收各子服（Game/Gate/World）的 RPC 心跳注册
///   - 维护 ServiceRegistry，统计在线服数量
///   - 处理 GM 命令（通过 RPC 0x3001 下发）
///   - 提供 Admin HTTP 接口查询全局状态
///
/// 注意：Master 不主动检测超时判死（H1 修订），由外部监控系统负责。
///
/// 消息 ID：
///   0x3000 — 子服心跳注册（来自 Game/Gate/World）
///   0x3001 — GM 命令分发
///   0x3002 — 查询服列表
///
/// 使用方式：
///   MasterService master(registry);
///   RpcServer rpc_srv;
///   master.RegisterRpcHandlers(rpc_srv);
///   rpc_srv.Start(9000);

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <mutex>

#include "snc/discovery/ServiceRegistry.h"
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcSession.h"
#include "snc/core/IConnection.h"
#include "snc/core/Msg.h"
#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {

// ── 子服类型枚举 ─────────────────────────────────────────────────
enum class ServiceType : int {
    GAME   = 0,
    GATE   = 1,
    WORLD  = 2,
};

// ── 服心跳包结构 ──────────────────────────────────────────────────
struct HeartbeatPayload {
    std::string serviceName;   ///< "game_01", "gate_01" 等
    ServiceType type;
    int32_t     playerCount   = 0;
    int32_t     mapCount      = 0;
    int64_t     uptimeSecs    = 0;
};

// ── GM 命令结构 ──────────────────────────────────────────────────
struct GmCommand {
    std::string command;   ///< "kick_player", "give_item", "shutdown" 等
    std::string target;    ///< 目标服名称（如 "game_01"）
    std::string args;      ///< JSON 参数字符串
    std::string operator_id; ///< GM 操作者 ID
};

// ── MasterService ────────────────────────────────────────────────
class MasterService {
public:
    using Ptr = std::shared_ptr<MasterService>;

    explicit MasterService(discovery::ServiceRegistry::Ptr registry = nullptr);

    /// 注册 RPC 处理器（在 RpcServer 上调用）。
    void RegisterRpcHandlers(rpc::RpcServer& rpcServer);

    /// 注册 Admin HTTP 路由（供 GM 通过 HTTP 操作）。
    void RegisterAdminRoutes(http::Router& routes, auth::JwtService& jwtSvc);

    /// 查询全局服状态。
    std::string QueryServiceStatus();

    /// 执行 GM 命令并转发到目标服。
    std::string ExecuteGm(const GmCommand& cmd);

    /// 获取最近 N 条心跳日志（用于调试）。
    std::vector<std::string> GetHeartbeatLog(int limit = 20) const;

private:
    // ── RPC 消息处理器 ─────────────────────────────────────────
    /// 0x3000: 子服心跳注册
    void OnHeartbeat(core::IConnection::Ptr conn,
                     rpc::RpcSession::Ptr session,
                     core::Msg::Ptr msg,
                     std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond);

    /// 0x3001: GM 命令
    void OnGmCommand(core::IConnection::Ptr conn,
                     rpc::RpcSession::Ptr session,
                     core::Msg::Ptr msg,
                     std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond);

    /// 0x3002: 查询服列表
    void OnQueryServices(core::IConnection::Ptr conn,
                         rpc::RpcSession::Ptr session,
                         core::Msg::Ptr msg,
                         std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond);

    // ── 辅助方法 ──────────────────────────────────────────────
    void AppendHeartbeatLog_(const std::string& entry);

    discovery::ServiceRegistry::Ptr registry_;
    std::vector<std::string>        heartbeat_log_;
    mutable std::mutex              log_mtx_;
};

} // namespace mmorpg
} // namespace snc
