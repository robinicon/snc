/// @file GameService.cpp（传奇版增强实现）

#include "snc/mmorpg/legend/GameService.h"
#include "snc/mmorpg/legend/LuaBridge.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>

namespace snc {
namespace mmorpg {
namespace legend {

// ── 构造/析构 ─────────────────────────────────────────────────────

GameService::GameService(rpc::RpcClient::Ptr master_rpc,
                         discovery::ServiceRegistry::Ptr registry,
                         const auth::JwtConfig& jwtCfg)
    : master_rpc_(std::move(master_rpc))
    , registry_(std::move(registry))
    , jwt_cfg_(jwtCfg)
    , scene_mgr_(std::make_shared<SceneMgr>())
    , entity_mgr_(std::make_shared<EntityMgr>())
    , item_mgr_(std::make_shared<ItemMgr>())
    , npc_mgr_(std::make_shared<NPCManager>())
    , monster_mgr_(std::make_shared<MonsterManager>())
    , combat_(std::make_shared<CombatSystem>())
    , drop_mgr_(std::make_shared<DropManager>())
    , guild_mgr_(std::make_shared<GuildManager>())
    , world_chan_(std::make_shared<WorldChannel>()) {}

// ── 初始化 ────────────────────────────────────────────────────────

void GameService::Init() {
    // 1. 创建场景（传奇经典）
    std::vector<SceneConfig> sceneDefs = {
        {Maps::kMapBegin,      "新手村",   100, 100, true,  false, 1, SyncMode::HIGH},
        {Maps::kMapBeij,       "比奇城",   200, 200, true,  false, 1, SyncMode::HIGH},
        {Maps::kMapAlmaty,     "盟重省",   200, 200, true,  false, 1, SyncMode::HIGH},
        {Maps::kMapShabake,    "沙巴克",   300, 300, false, false, 1, SyncMode::HIGH},
        {Maps::kMapWolfMountain, "沃玛森林", 150, 150, false, false, 5, SyncMode::LOW},
        {Maps::kMapCowMac,     "牛魔寺庙", 150, 150, false, false, 10, SyncMode::LOW},
        {Maps::kMapThrone,     "祖玛寺庙", 150, 150, false, false, 15, SyncMode::LOW},
        {Maps::kMapIronRoom,   "石墓阵",   120, 120, false, false, 20, SyncMode::LOW},
        {Maps::kMapDragonNest, "龙墓",     200, 200, false, true,  25, SyncMode::LOW},
    };
    for (auto& cfg : sceneDefs) scene_mgr_->CreateScene(cfg);
    SNC_LOG_INFO("legend.game", "Initialized %zu scenes", sceneDefs.size());

    // 2. 注册怪物
    std::vector<MonsterDef> monsterDefs = {
        {1, "稻草人",    30,  3,  1,  5,  2, 1, 5, 20000},
        {2, "野狗",     50,  8,  2, 10,  2, 1, 6, 25000},
        {3, "沃玛战士", 150, 20,  8, 30,  3, 2, 8, 30000},
        {4, "祖玛雕像", 300, 40, 15, 80,  3, 2, 8, 40000},
        {5, "牛魔战士", 500, 60, 25,150,  4, 3,10, 50000},
    };
    for (auto& m : monsterDefs) monster_mgr_->RegisterMonster(m);
    SNC_LOG_INFO("legend.game", "Registered %zu monster types", monsterDefs.size());

    // 3. 放置初始怪物
    monster_mgr_->InitMapMonsters(Maps::kMapBegin, {
        {1, {30, 30}}, {1, {35, 32}}, {1, {40, 30}},
        {2, {50, 50}}, {2, {55, 52}},
    });
    monster_mgr_->InitMapMonsters(Maps::kMapWolfMountain, {
        {3, {60, 60}}, {3, {70, 65}}, {4, {80, 70}},
    });

    // 4. 注册 NPC（比奇城传送员）
    NPCDef teleportNPC;
    teleportNPC.npc_id   = 1001;
    teleportNPC.name     = "传送员·比奇";
    teleportNPC.type     = NPCType::TELEPORT;
    teleportNPC.map_id   = Maps::kMapBeij;
    teleportNPC.x        = 100;
    teleportNPC.y        = 100;
    teleportNPC.teleports.push_back(NPCDef::TeleportDest{Maps::kMapBegin, 50, 50, 0,    "新手村"});
    teleportNPC.teleports.push_back(NPCDef::TeleportDest{Maps::kMapShabake, 150, 150, 500, "沙巴克"});
    npc_mgr_->RegisterNPC(teleportNPC);
    npc_mgr_->PlaceNPC(Maps::kMapBeij, 1001, 100, 100);

    // 5. 注册传奇经典技能
    std::vector<SkillDef> skills = {
        {Skills::kSkillCrazyKick,   "野蛮冲撞", ClassType::WARRIOR, 1,  5, 3000, 1, 1.5f, 0, false},
        {Skills::kSkillWhirlwind,   "半月弯刀", ClassType::WARRIOR, 5, 10, 5000, 2, 1.2f, 0, true },
        {Skills::kSkillFireSlash,   "烈火剑法", ClassType::WARRIOR,10, 15, 8000, 1, 2.0f, 0, false},
        {Skills::kSkillThunderbolt, "雷电术",   ClassType::MAGE,    1,  8, 3000, 3, 0, 1.5f, false},
        {Skills::kSkillFireWall,    "火墙",     ClassType::MAGE,    5, 12, 6000, 5, 0, 1.0f, true },
        {Skills::kSkillHeal,        "治愈术",   ClassType::TAOIST,  1,  5, 2000, 1, 0, 0,   false},
        {Skills::kSkillSummonDog,   "召唤神兽", ClassType::TAOIST,  5, 20,10000, 0, 0, 0,   false},
    };
    for (auto& s : skills) combat_->RegisterSkill(s);

    // 6. 注册掉落表（简单版）
    DropTable cowDrop{1, {
        {1001, 30000, 1, 1}, // 牛魔手册
        {1002, 20000, 1, 2}, // 太阳金针
        {0,    50000, 0, 0}, // 空气兜底
    }};
    drop_mgr_->RegisterTable(cowDrop);

    // 7. LuaBridge 初始化（Lua 可用时加载脚本，不可用时为 no-op stub）
#ifdef SNC_LUA_AVAILABLE
    lua_state_ = luaL_newstate();
    if (lua_state_) {
        lua_bridge_ = LuaBridge(static_cast<LuaBridge::LuaState>(lua_state_));
        LuaBridge::RegisterAllStatics(lua_bridge_.L());

        // 将 C++ 子系统推送为 Lua 全局变量
        lua_pushlightuserdata(lua_bridge_.L(),
            static_cast<void*>(combat_.get()));
        lua_setglobal(lua_bridge_.L(), "combat");

        lua_pushlightuserdata(lua_bridge_.L(),
            static_cast<void*>(drop_mgr_.get()));
        lua_setglobal(lua_bridge_.L(), "drop_mgr");

        lua_pushlightuserdata(lua_bridge_.L(),
            static_cast<void*>(monster_mgr_.get()));
        lua_setglobal(lua_bridge_.L(), "monster_mgr");

        lua_pushlightuserdata(lua_bridge_.L(),
            static_cast<void*>(guild_mgr_.get()));
        lua_setglobal(lua_bridge_.L(), "guild_mgr");

        lua_pushlightuserdata(lua_bridge_.L(),
            static_cast<void*>(world_chan_.get()));
        lua_setglobal(lua_bridge_.L(), "world_chan");

        lua_pushlightuserdata(lua_bridge_.L(),
            static_cast<void*>(npc_mgr_.get()));
        lua_setglobal(lua_bridge_.L(), "npc_mgr");

        // 加载各模块 Lua 脚本（路径相对于项目根目录）
        std::string base = std::filesystem::current_path().string();
        auto Load = [&](const char* rel, const char* label) -> bool {
            std::string path = base + "/" + rel;
            bool ok = lua_bridge_.LoadScript(path);
            SNC_LOG_INFO("legend.lua", "%s: %s", label, ok ? "OK" : "skip");
            return ok;
        };
        Load(LuaScriptPath::COMBAT,   "combat");
        Load(LuaScriptPath::DROP,     "drop");
        Load(LuaScriptPath::MONSTER,  "monster");
        Load(LuaScriptPath::GUILD,    "guild");
        Load(LuaScriptPath::WORLD_CHAN,"world_channel");
        Load(LuaScriptPath::NPC,      "npc");
    } else {
        SNC_LOG_WARN("legend.lua", "Failed to create Lua state");
    }
#else
    (void)lua_bridge_; // stub mode: no-op
#endif

    // 将 LuaBridge 注入 MonsterManager（使 AI 可用 Lua 决策）
    monster_mgr_->SetLuaBridge(&lua_bridge_);
    // 将 LuaBridge 注入 CombatSystem（使技能可用 Lua 驱动）
    combat_->SetLuaBridge(&lua_bridge_);

    SNC_LOG_INFO("legend.game", "GameService Init complete");
}

// ── 启动 ──────────────────────────────────────────────────────────

void GameService::Start(uint16_t rpcPort, const std::string& serviceName) {
    service_name_ = serviceName;
    running_ = true;
    if (registry_)
        registry_->Register(serviceName, "127.0.0.1", rpcPort, 1);

    rpc_server_ = std::make_shared<rpc::RpcServer>();
    rpc_server_->RegisterHandler(kMsgGameFromGate,
        [this](core::IConnection::Ptr conn, rpc::RpcSession::Ptr session,
               core::Msg::Ptr msg,
               std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
            OnFromGate(conn, session, msg, respond);
        });
    (void)rpc_server_; // 实际网络接入由上层 TcpServer 完成，RpcServer 仅负责路由

    main_loop_thread_ = std::thread([this]{ MainLoop_(); });
    HeartbeatLoop_();

    SNC_LOG_INFO("legend.game", "Game service '%s' started on port %d",
                 serviceName.c_str(), rpcPort);
}

void GameService::Shutdown() {
    running_ = false;
    if (main_loop_thread_.joinable()) main_loop_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
#ifdef SNC_LUA_AVAILABLE
    if (lua_state_) {
        lua_close(static_cast<lua_State*>(lua_state_));
        lua_state_ = nullptr;
    }
#endif
    SNC_LOG_INFO("legend.game", "Game service '%s' shut down. players=%zu",
                 service_name_.c_str(), players_.size());
}

// ── 主循环 ────────────────────────────────────────────────────────

void GameService::MainLoop_() {
    auto lastTick = std::chrono::steady_clock::now();
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
        lastTick = now;

        // 1. 处理玩家消息队列
        ProcessMessages_();

        // 2. 场景主循环（含玩家/怪物/PET 的 AccumAndTick）
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        scene_mgr_->TickAll(nowMs, elapsedMs);
        monster_mgr_->TickAll(elapsedMs,
            [this](PlayerActor::Ptr attacker, PlayerActor::Ptr victim, int32_t dmg) {
                (void)attacker; (void)victim; (void)dmg;
                // 怪物攻击玩家回调，实际伤害由 CombatSystem 在 Attack 阶段计算
            },
            [this](MonsterManager::MonsterPtr monster, PlayerActor::Ptr killer) {
                if (!monster || !killer) return;
                // 怪物死亡 → 给经验 + 掉落
                killer->AddExp(monster->def.exp);
                if (monster->def.drop_table_id > 0) {
                    auto drops = drop_mgr_->RollDrops(monster->def.drop_table_id);
                    drop_mgr_->CreateDrops(monster->pos.mapId, monster->pos.x, monster->pos.y, drops);
                }
            });

        // 4. 清理过期掉落
        drop_mgr_->CleanupExpired(nowMs);
    }
}

void GameService::ProcessMessages_() {
    std::deque<std::pair<int32_t, core::Msg::Ptr>> batch;
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        batch.swap(pending_msgs_);
    }
    for (auto& [pid, msg] : batch) {
        PlayerPtr plr = FindPlayer(pid);
        if (plr) {
            // 分发到各业务处理（简化版，直接丢弃未知消息）
            (void)plr; (void)msg;
        }
    }
}

void GameService::HeartbeatLoop_() {
    heartbeat_thread_ = std::thread([this]() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (!running_.load()) break;
            // 心跳逻辑由 Master RPC 处理
        }
    });
}

// ── RPC 消息分发 ──────────────────────────────────────────────────

void GameService::OnFromGate(core::IConnection::Ptr conn,
                              rpc::RpcSession::Ptr session,
                              core::Msg::Ptr msg,
                              std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session;
    if (!msg || !msg->Payload()) {
        respond(rpc::RpcError::UNKNOWN, nullptr, 0);
        return;
    }
    std::string s(msg->Payload(), msg->PayloadLen());
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = s.find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(s.substr(p + kv.size())); } catch (...) { return 0; }
    };
    int32_t player_id = get_int("player_id");
    int32_t msg_id    = get_int("msg_id");

    PlayerPtr plr = FindPlayer(player_id);
    if (!plr) {
        std::string err = "{\"error\":\"player_not_found\",\"player_id\":" + std::to_string(player_id) + "}";
        respond(rpc::RpcError::UNKNOWN,
                reinterpret_cast<const uint8_t*>(err.c_str()), err.size());
        return;
    }
    // 将业务消息分发到对应处理函数
    switch (msg_id) {
        case kMsgClientMove:       DispatchMove_      (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientAttack:     DispatchAttack_    (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientNPCDialog:  DispatchNPC_       (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientChat:       DispatchChat_      (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientPickup:     DispatchPickup_    (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientSkill:      DispatchSkill_     (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientGuildReq:   /* TODO */         break;
        default:
                // 未知消息类型，忽略
                break;
    }
    std::string resp = "{\"status\":\"ok\",\"player\":" + std::to_string(player_id) + "}";
    respond(rpc::RpcError::OK,
            reinterpret_cast<const uint8_t*>(resp.c_str()), resp.size());
}

void GameService::DispatchMove_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)payload; (void)len;
    // 解析 JSON: {"map_id":X,"x":Y,"y":Z}
    // 简化：直接返回成功（完整解析见 example）
    plr->Send(kMsgGameToGate, "{\"type\":\"move\",\"status\":\"ok\"}", 30);
}

void GameService::DispatchAttack_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)payload; (void)len;
    // 简化：攻击逻辑由主循环怪物 Tick 处理
    plr->Send(kMsgGameToGate, "{\"type\":\"attack\",\"status\":\"ok\"}", 33);
}

void GameService::DispatchNPC_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)payload; (void)len;
    // TODO: 解析 npc_id 并调用 npc_mgr_->Talk/Buy/Sell/Teleport
    plr->Send(kMsgGameToGate, "{\"type\":\"npc\",\"status\":\"ok\"}", 31);
}

void GameService::DispatchChat_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)payload; (void)len;
    // TODO: 解析 channel + content 并调用 world_chan_->Send
    plr->Send(kMsgGameToGate, "{\"type\":\"chat\",\"status\":\"ok\"}", 32);
}

void GameService::DispatchPickup_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)payload; (void)len;
    int picked = drop_mgr_->PickUp(plr, 2);
    plr->Send(kMsgGameToGate,
        ("{\"type\":\"pickup\",\"picked\":" + std::to_string(picked) + "}").c_str(),
        40);
}

void GameService::DispatchSkill_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)plr; (void)payload; (void)len;
    plr->Send(kMsgGameToGate, "{\"type\":\"skill\",\"status\":\"ok\"}", 33);
}

// ── 玩家管理 ──────────────────────────────────────────────────────

GameService::PlayerPtr GameService::FindPlayer(int32_t player_id) const {
    std::lock_guard<std::mutex> lk(players_mtx_);
    auto it = players_.find(player_id);
    return (it != players_.end()) ? it->second : nullptr;
}

GameService::PlayerPtr GameService::CreatePlayer(int32_t playerId, const std::string& name,
                                                   ClassType cls, core::IConnection::Ptr conn) {
    auto plr = std::make_shared<PlayerActor>(playerId, name, cls, conn);
    entity_mgr_->Register(plr);
    auto scene = scene_mgr_->GetScene(Maps::kMapBegin);
    if (scene) scene->AddEntity(plr);
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        players_[playerId] = plr;
    }
    world_chan_->RegisterPlayer(playerId, plr);
    SNC_LOG_INFO("legend.game", "Player %s (cls=%s, id=%d) logged in",
                 name.c_str(), ClassTypeName(cls), playerId);
    return plr;
}

void GameService::RemovePlayer(int32_t playerId) {
    PlayerPtr plr;
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        auto it = players_.find(playerId);
        if (it == players_.end()) return;
        plr = it->second;
        players_.erase(it);
    }
    entity_mgr_->Unregister(playerId);
    world_chan_->UnregisterPlayer(playerId);
    if (plr) plr->SetAlive(false);
    SNC_LOG_INFO("legend.game", "Player %d (%s) logged out", playerId, plr ? plr->Nickname().c_str() : "?");
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
