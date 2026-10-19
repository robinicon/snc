// SPDX-License-Identifier: MIT
#pragma once

/// @file GameService.h
/// @brief Game 服 — 游戏逻辑、Actor 管理、地图、心跳、优雅停机。
///
/// 职责：
///   - 管理 PlayerActor（通过 players_ map）
///   - 管理 MapManager（地图实例）
///   - 向 Master 服发送心跳（通过 master_rpc_）
///   - 提供 RPC 服务接收 Gate 转发的消息

#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <sstream>

#include "snc/core/IConnection.h"
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcClient.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include "snc/mmorpg/PlayerPos.h"
#include "snc/mmorpg/MapManager.h"

namespace snc {
namespace mmorpg {

// ── Actor（基础实体）─────────────────────────────────────────────
class Actor {
public:
    explicit Actor(int32_t id) : id_(id) {}
    virtual ~Actor() = default;
    int32_t Id() const { return id_; }
private:
    int32_t id_;
};

// ── PlayerActor ──────────────────────────────────────────────────
/// @brief 玩家角色实体（框架层 stub 实现，传奇版在 snc/legend/PlayerActor.h）
///
/// 注意：此为框架层基础实现，不含战斗、背包、任务等传奇玩法逻辑。
/// 完整传奇实现请使用 snc_mmorpg::legend::PlayerActor。
class PlayerActor : public Actor {
public:
    using Ptr = std::shared_ptr<PlayerActor>;

    PlayerActor(int32_t id, const std::string& name,
                core::IConnection::Ptr conn, MapManager::Ptr mapMgr)
        : Actor(id), name_(name), conn_(std::move(conn)), map_mgr_(std::move(mapMgr)),
          last_pos_({0, 0, 0}) {}

    ~PlayerActor() override = default;

    const std::string& Name() const { return name_; }
    int32_t Id() const { return Actor::Id(); }
    PlayerPos LastPos() const { return last_pos_; }

    void OnMessage(int32_t msgId, const char* payload, size_t len);
    void Tick(long long elapsedMs);
    void Send(int32_t msgId, const char* data, size_t len) {
        if (conn_) conn_->Send(core::Msg::Create(msgId, data, len));
    }

private:
    std::string              name_;
    core::IConnection::Ptr   conn_;
    MapManager::Ptr          map_mgr_;
    PlayerPos                last_pos_;
};

// ── GameService ──────────────────────────────────────────────────
/// @brief Game 服主类，管理玩家、地图、RPC 服务。
///
/// 使用方式：
///   GameService svc(master_rpc, registry, jwtCfg);
///   svc.Start(rpcPort, "game");
///   svc.Shutdown();
class GameService {
public:
    using Ptr       = std::shared_ptr<GameService>;
    using Conn      = core::IConnection;
    using PlayerPtr = PlayerActor::Ptr;

    explicit GameService(rpc::RpcClient::Ptr master_rpc,
                         discovery::ServiceRegistry::Ptr registry,
                         const auth::JwtConfig& jwtCfg);

    ~GameService() = default;

    /// 启动 Game 服（注册到 ServiceRegistry，启动心跳线程）。
    void Start(uint16_t rpcPort, const std::string& serviceName);

    /// 停止并等待心跳线程退出。
    void Shutdown();

    /// 初始化地图（创建默认地图）。
    void InitMaps();

    /// 查找在线玩家。
    PlayerPtr FindPlayer(int32_t player_id) const;

    /// 获取 MapManager 引用。
    MapManager::Ptr GetMapManager() const { return map_mgr_; }

    /// 获取注册的服务名（可用于调试）。
    const std::string& ServiceName() const { return service_name_; }

private:
    // ── RPC 消息处理（由 Gate 转发）────────────────────────────
    void OnFromGate_(core::IConnection::Ptr conn,
                     rpc::RpcSession::Ptr session,
                     core::Msg::Ptr msg,
                     std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond);

    // ── 玩家管理 ───────────────────────────────────────────────
    PlayerPtr CreatePlayer_(int32_t playerId, const std::string& name,
                            core::IConnection::Ptr conn);
    void      RemovePlayer_(int32_t playerId);

    // ── 心跳线程 ───────────────────────────────────────────────
    void HeartbeatLoop_();

    // ── 成员 ───────────────────────────────────────────────────
    rpc::RpcClient::Ptr                 master_rpc_;
    discovery::ServiceRegistry::Ptr     registry_;
    auth::JwtConfig                     jwt_cfg_;
    MapManager::Ptr                     map_mgr_;
    std::shared_ptr<rpc::RpcServer>     rpc_server_;

    std::unordered_map<int32_t, PlayerPtr> players_;
    mutable std::mutex                     players_mtx_;

    std::string                  service_name_;
    std::atomic<bool>            running_{false};
    std::atomic<int32_t>         next_player_id_{1};
    std::thread                  heartbeat_thread_;
};

} // namespace mmorpg
} // namespace snc
