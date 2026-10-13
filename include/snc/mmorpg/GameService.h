#pragma once

/// @file GameService.h
/// @brief Game 服 — 游戏核心逻辑：Actor 管理、地图实例、心跳、优雅停机。
///
/// 职责：
///   - 管理玩家 Actor（登录/登出/移动/战斗）
///   - 管理 NPC/Monster Actor
///   - 管理 MapInstance（通过 MapManager）
///   - 处理来自 Gate 转发的客户端消息（RPC 0x2000~0x20FF）
///   - 向 Master 服注册自身，接收心跳
///   - 优雅停机：SIGTERM → 停止新连接 → 等待玩家退出（30s）→ 注销 → exit(0)
///
/// Actor 基类：
///   Actor — 虚函数 OnMessage/Tick，所有实体继承。
///   PlayerActor — 含位置、状态、背包等。
///
/// 使用方式：
///   GameService svc(master_rpc, registry, jwtCfg);
///   svc.Start(8001, "game_01");

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <thread>

#include "snc/discovery/ServiceRegistry.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcSession.h"
#include "snc/core/IConnection.h"
#include "snc/core/Msg.h"
#include "snc/core/DisconnectReason.h"
#include "snc/auth/AuthService.h"
#include "snc/mmorpg/MapManager.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {

// ── Game 服消息 ID ───────────────────────────────────────────────
static constexpr int32_t kMsgGameFromGate  = 0x2000;  ///< Gate→Game 业务消息起始
static constexpr int32_t kMsgGameToGate    = 0x2100;  ///< Game→Gate 响应起始
static constexpr int32_t kMsgGameRegToMaster = 0x3000; ///< Game→Master 注册
static constexpr int32_t kMsgGameHbToMaster  = 0x3001; ///< Game→Master 心跳

// ── Actor 基类 ───────────────────────────────────────────────────
class Actor {
public:
    virtual ~Actor() = default;
    virtual void OnMessage(int32_t msgId, const char* payload, size_t len) {}
    virtual void Tick(long long elapsedMs) {}
    int32_t Id() const { return id_; }
protected:
    explicit Actor(int32_t id) : id_(id) {}
    int32_t id_;
};

// ── 玩家 Actor ───────────────────────────────────────────────────
class PlayerActor : public Actor {
public:
    using Ptr = std::shared_ptr<PlayerActor>;

    struct Stats {
        int32_t level  = 1;
        int64_t gold   = 0;
        int64_t exp    = 0;
        int32_t hp     = 100;
        int32_t max_hp = 100;
    };

    PlayerActor(int32_t id, const std::string& name,
                core::IConnection::Ptr conn, MapManager::Ptr mapMgr)
        : Actor(id), name_(name), conn_(std::move(conn)), map_mgr_(std::move(mapMgr)),
          login_time_(std::chrono::steady_clock::now()) {}

    void OnMessage(int32_t msgId, const char* payload, size_t len) override;
    void Tick(long long elapsedMs) override;

    core::IConnection::Ptr GetConn() const { return conn_.lock(); }
    const std::string&     GetName() const { return name_; }
    const Stats&           GetStats()  const { return stats_; }
    PlayerPos              GetPos()    const { return pos_; }

    /// 发送消息给该玩家。
    void Send(int32_t msgId, const char* data, size_t len) {
        auto c = conn_.lock();
        if (c) c->Send(core::Msg::Create(msgId, data, len));
    }

private:
    std::string               name_;
    std::weak_ptr<core::IConnection> conn_;
    MapManager::Ptr           map_mgr_;
    Stats                     stats_;
    PlayerPos                 pos_;
    std::chrono::steady_clock::time_point login_time_;
};

// ── GameService ──────────────────────────────────────────────────
class GameService {
public:
    using Ptr       = std::shared_ptr<GameService>;
    using PlayerPtr = std::shared_ptr<PlayerActor>;

    /// master_rpc  — 与 Master 的 RPC 客户端（用于注册+心跳）
    /// registry    — 本地 ServiceRegistry（自我注册，供 Gate 发现）
    /// jwtCfg      — JWT 密钥（用于验证 Gate 传来的 token）
    GameService(rpc::RpcClient::Ptr master_rpc,
                discovery::ServiceRegistry::Ptr registry,
                const auth::JwtConfig& jwtCfg);

    ~GameService() = default;

    /// 启动 Game 服（阻塞调用）。
    void Start(uint16_t rpcPort, const std::string& serviceName = "game_01");

    /// 优雅停机。
    void Shutdown();

    /// 注册所有地图（示例：新建几张地图）。
    void InitMaps();

    /// 获取 MapManager 引用。
    MapManager::Ptr GetMapManager() const { return map_mgr_; }

    /// 统计在线玩家数。
    size_t OnlinePlayerCount() const { return players_.size(); }

    /// 根据 player_id 查找玩家。
    PlayerPtr FindPlayer(int32_t player_id) const;

private:
    // ── RPC 处理器 ─────────────────────────────────────────────
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
