#pragma once

/// @file GameService.h（传奇版增强�?
///
/// 集成所有传奇子模块�?
///   - PlayerActor（增强版，含职业/背包/装备/技�?PK/行会�?
///   - NPCManager
///   - MonsterManager
///   - CombatSystem
///   - DropManager
///   - GuildManager
///   - WorldChannel
///
/// 使用方式�?
///   GameService svc(master_rpc, registry, jwtCfg);
///   svc.Init();
///   svc.Start(23000, "game_01");

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
#include "snc/mmorpg/legend/SceneMgr.h"
#include "snc/mmorpg/legend/EntityMgr.h"
#include "snc/mmorpg/legend/ItemMgr.h"
#include "snc/mmorpg/legend/PlayerActor.h"
#include "snc/mmorpg/legend/NPCManager.h"
#include "snc/mmorpg/legend/MonsterManager.h"
#include "snc/mmorpg/legend/CombatSystem.h"
#include "snc/mmorpg/legend/DropManager.h"
#include "snc/mmorpg/legend/GuildManager.h"
#include "snc/mmorpg/legend/WorldChannel.h"
#include "snc/base/Log.h"
#include "snc/mmorpg/legend/LuaBridge.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── Game �?RPC 消息 ID ──────────────────────────────────────────
static constexpr int32_t kMsgGameFromGate  = 0x2000;
static constexpr int32_t kMsgGameToGate    = 0x2100;
static constexpr int32_t kMsgGameRegToMaster = 0x3000;
static constexpr int32_t kMsgGameHbToMaster  = 0x3001;

// ── 客户端→Game 服业务消息 ID（Gate 转发）───────────────────────
// 0x1100 ~ 0x11FF：移动/攻击/NPC/聊天等客户端输入
static constexpr int32_t kMsgClientMove       = 0x1100;
static constexpr int32_t kMsgClientAttack     = 0x1101;
static constexpr int32_t kMsgClientNPCDialog  = 0x1102;
static constexpr int32_t kMsgClientChat       = 0x1103;
static constexpr int32_t kMsgClientPickup     = 0x1104;
static constexpr int32_t kMsgClientSkill      = 0x1105;
static constexpr int32_t kMsgClientGuildReq   = 0x1106;

// ── Game 服→客户端业务消息 ID（推送给 Gate，由 Gate 转发给客户端）──
// 0x1200 ~ 0x12FF：服务器主动推送的响应和状态同步
static constexpr int32_t kMsgServerPlayerSync    = 0x1200; // 玩家属性全量同步
static constexpr int32_t kMsgServerPosSync       = 0x1201; // 实体位置批量同步
static constexpr int32_t kMsgServerDamageResult  = 0x1202; // 伤害结果
static constexpr int32_t kMsgServerDeathNotify   = 0x1203; // 死亡通知
static constexpr int32_t kMsgServerDropNotify    = 0x1204; // 掉落物刷新
static constexpr int32_t kMsgServerLevelUp       = 0x1205; // 升级通知
static constexpr int32_t kMsgServerChatBroadcast = 0x1206; // 聊天广播
static constexpr int32_t kMsgServerGuildNotice   = 0x1207; // 行会通知
static constexpr int32_t kMsgServerSceneEnter    = 0x1208; // 进入场景
static constexpr int32_t kMsgServerSceneLeave    = 0x1209; // 离开场景
static constexpr int32_t kMsgServerMoveResponse  = 0x120A; // 移动响应（确认/拒绝）
static constexpr int32_t kMsgServerError         = 0x120B; // 通用错误提示

// ── 认证消息 ID（客户端→服务端 / 服务端→客户端）────────────────
static constexpr int32_t kMsgAuthLogin     = 0x1000; // 登录请求（含 JWT）
static constexpr int32_t kMsgAuthResponse  = 0x1001; // 登录结果（含玩家初始数据）
static constexpr int32_t kMsgKeepAlive     = 0x1002; // 应用层心跳（备用）

// ══════════════════════════════════════════════════════════════════
// GameService（传奇版�?
// ══════════════════════════════════════════════════════════════════
class GameService {
public:
    using Ptr       = std::shared_ptr<GameService>;
    using PlayerPtr = PlayerActor::Ptr;

    GameService(rpc::RpcClient::Ptr master_rpc,
                discovery::ServiceRegistry::Ptr registry,
                const auth::JwtConfig& jwtCfg);

    ~GameService() = default;

    // ── 生命周期 ────────────────────────────────────────────────
    /// 初始化所有子系统（地�?NPC/怪物/掉落/行会/世界频道�?
    void Init();
    /// 启动 Game 服（阻塞调用�?
    void Start(uint16_t rpcPort, const std::string& serviceName = "game_01");
    /// 优雅停机
    void Shutdown();

    // ── 子系统访问器 ────────────────────────────────────────────
    SceneMgr::Ptr          GetSceneMgr()    const { return scene_mgr_; }
    EntityMgr::Ptr         GetEntityMgr()   const { return entity_mgr_; }
    ItemMgr::Ptr           GetItemMgr()     const { return item_mgr_; }
    NPCManager::Ptr        GetNPCMgr()      const { return npc_mgr_; }
    MonsterManager::Ptr    GetMonsterMgr()  const { return monster_mgr_; }
    CombatSystem::Ptr      GetCombat()      const { return combat_; }
    DropManager::Ptr       GetDropMgr()     const { return drop_mgr_; }
    GuildManager::Ptr      GetGuildMgr()    const { return guild_mgr_; }
    WorldChannel::Ptr      GetWorldChan()   const { return world_chan_; }

    // ── 玩家管理 ────────────────────────────────────────────────
    size_t OnlinePlayerCount() const { return players_.size(); }
    PlayerPtr FindPlayer(int32_t player_id) const;
    PlayerPtr CreatePlayer(int32_t playerId, const std::string& name,
                            ClassType cls, core::IConnection::Ptr conn);
    void RemovePlayer(int32_t playerId);

    // ── RPC 处理器（�?RpcServer 注册）──────────────────────────
    void OnFromGate(core::IConnection::Ptr conn,
                    rpc::RpcSession::Ptr session,
                    core::Msg::Ptr msg,
                    std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond);

private:
    // ── 主循�?─────────────────────────────────────────────────
    void MainLoop_();
    void HeartbeatLoop_();
    void ProcessMessages_();

    // ── 消息分发 ───────────────────────────────────────────────
    void DispatchMove_(PlayerActor::Ptr plr, const char* payload, size_t len);
    void DispatchAttack_(PlayerActor::Ptr plr, const char* payload, size_t len);
    void DispatchNPC_(PlayerActor::Ptr plr, const char* payload, size_t len);
    void DispatchChat_(PlayerActor::Ptr plr, const char* payload, size_t len);
    void DispatchPickup_(PlayerActor::Ptr plr, const char* payload, size_t len);
    void DispatchSkill_(PlayerActor::Ptr plr, const char* payload, size_t len);
    void DispatchGuild_(PlayerActor::Ptr plr, const char* payload, size_t len);

    // ── 成员 ───────────────────────────────────────────────────
    rpc::RpcClient::Ptr                 master_rpc_;
    discovery::ServiceRegistry::Ptr     registry_;
    auth::JwtConfig                     jwt_cfg_;

    SceneMgr::Ptr       scene_mgr_;
    EntityMgr::Ptr      entity_mgr_;
    ItemMgr::Ptr        item_mgr_;
    NPCManager::Ptr     npc_mgr_;
    MonsterManager::Ptr monster_mgr_;
    CombatSystem::Ptr   combat_;
    DropManager::Ptr    drop_mgr_;
    GuildManager::Ptr   guild_mgr_;
    WorldChannel::Ptr   world_chan_;

    rpc::RpcServer::Ptr   rpc_server_;

    std::unordered_map<int32_t, PlayerPtr> players_;
    mutable std::mutex players_mtx_;
    std::deque<std::pair<int32_t, core::Msg::Ptr>> pending_msgs_;
    mutable std::mutex pending_mtx_;

    std::string service_name_;
    std::atomic<bool> running_{false};
    std::atomic<int32_t> next_player_id_{1};
    std::thread main_loop_thread_;
    std::thread heartbeat_thread_;

    // ── LuaBridge（Lua 可用时持有 lua_State*，不可用时为 nullptr）──
    LuaBridge lua_bridge_{nullptr};
#ifdef SNC_LUA_AVAILABLE
    // Lua VM 生命周期：Init 中创建，Shutdown 中关闭
    void* lua_state_ = nullptr;
#endif
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
