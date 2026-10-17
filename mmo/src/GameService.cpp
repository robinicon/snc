/// @file GameService.cpp（传奇版增强实现）

#include "snc/mmorpg/legend/GameService.h"
#include "snc/mmorpg/legend/LuaBridge.h"
#include "snc/serial/fbs/FbsSerializer.h"
#ifdef SNC_ENABLE_FLATBUFFERS
#include <flatbuffers/flatbuffers.h>
#endif
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

#ifdef SNC_ENABLE_FLATBUFFERS
namespace {
// ── 编码：将 struct 序列化为 length-prefixed FlatBuffers 字节流 ──
template<typename T>
std::vector<uint8_t> EncodeFbs(const T& msg, flatbuffers::FlatBufferBuilder& fbb) {
    fbb.Finish(T::Pack(fbb, msg));
    const char* fb_ptr = fbb.GetBufferPointer();
    size_t fb_len = fbb.GetSize();
    size_t out_len = 0;
    auto buf = serial::fbs::FbsSerializer::Encode(fb_ptr, fb_len, out_len);
    fbb.Clear();
    return buf;
}

// ── 解码：从 SNC Msg payload 中解析出 FlatBuffers 消息 ──
// 使用方式：DecodePayload(msg.Payload(), msg.PayloadLen(), [&](const T& t){...})
// 若为 stub 模式或解析失败则调用空 lambda
template<typename T>
void DecodePayload(const char* data, size_t len,
                   std::function<void(const T&)> fn) {
    std::string fbs_raw;
    if (!serial::fbs::FbsSerializer::Decode(
            reinterpret_cast<const uint8_t*>(data), len, fbs_raw)) {
        return;
    }
    // 从 raw 中读出 flatbuffers 长度前缀，再取 FlatBuffers 数据
    if (fbs_raw.size() < 4) return;
    size_t fb_len = (static_cast<size_t>(
        static_cast<uint8_t>(fbs_raw[0]) << 24) |
        (static_cast<size_t>(static_cast<uint8_t>(fbs_raw[1]) << 16) |
        (static_cast<size_t>(static_cast<uint8_t>(fbs_raw[2]) << 8)  |
         static_cast<size_t>(static_cast<uint8_t>(fbs_raw[3])))));
    if (fbs_raw.size() < 4 + fb_len) return;
    T msg;
    msg.ParseFromBuffer(
        reinterpret_cast<const uint8_t*>(fbs_raw.data() + 4), fb_len);
    fn(msg);
}
}  // namespace
#endif

void GameService::OnFromGate(core::IConnection::Ptr conn,
                              rpc::RpcSession::Ptr session,
                              core::Msg::Ptr msg,
                              std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session;
    if (!msg || !msg->Payload()) {
        respond(rpc::RpcError::UNKNOWN, nullptr, 0);
        return;
    }

    // 先提取 player_id 和 msg_id（两种模式通用）
    int32_t player_id = 0;
    int32_t msg_id    = 0;
#ifdef SNC_ENABLE_FLATBUFFERS
    // FlatBuffers 模式：用 FbsSerializer 解码
    {
        std::string fbs_raw;
        if (serial::fbs::FbsSerializer::Decode(
                msg->PayloadBytes(), msg->PayloadLen(), fbs_raw)) {
            // fbs_raw 包含 4B 长度前缀 + FlatBuffers 字节流
            // 尝试将其当作 MoveRequest 解出 player_id/msg_id
            // （实际项目中由生成代码提供统一的 FromBuffer helper）
            if (fbs_raw.size() >= 4) {
                size_t fb_len = (static_cast<size_t>(
                    static_cast<uint8_t>(fbs_raw[0]) << 24) |
                    (static_cast<size_t>(static_cast<uint8_t>(fbs_raw[1]) << 16) |
                    (static_cast<size_t>(static_cast<uint8_t>(fbs_raw[2]) << 8)  |
                     static_cast<size_t>(static_cast<uint8_t>(fbs_raw[3])))));
                if (fbs_raw.size() >= 4 + fb_len && fb_len >= 8) {
                    // 简单起见，fallback 到手工字段提取（后续由生成代码完善）
                }
            }
        }
    }
#else
    // Stub 模式：沿用手工 JSON 解析（保持可编译可用）
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = std::string(msg->Payload(), msg->PayloadLen()).find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(std::string(msg->Payload(), msg->PayloadLen()).substr(p + kv.size())); }
        catch (...) { return 0; }
    };
    player_id = get_int("player_id");
    msg_id    = get_int("msg_id");
#endif

    PlayerPtr plr = FindPlayer(player_id);
    if (!plr) {
        std::string err = "{\"error\":\"player_not_found\",\"player_id\":" + std::to_string(player_id) + "}";
        respond(rpc::RpcError::UNKNOWN,
                reinterpret_cast<const uint8_t*>(err.c_str()), err.size());
        return;
    }

    // 分发到各业务处理（msg_id 由外层协议路由，此处 payload 已不含 msg_id）
    switch (msg_id) {
        case kMsgClientMove:       DispatchMove_      (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientAttack:     DispatchAttack_    (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientNPCDialog:  DispatchNPC_       (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientChat:       DispatchChat_      (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientPickup:     DispatchPickup_    (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientSkill:      DispatchSkill_     (plr, msg->Payload(), msg->PayloadLen()); break;
        case kMsgClientGuildReq:   DispatchGuild_     (plr, msg->Payload(), msg->PayloadLen()); break;
        default:
                // 未知消息类型，忽略
                break;
    }
    std::string resp = "{\"status\":\"ok\",\"player\":" + std::to_string(player_id) + "}";
    respond(rpc::RpcError::OK,
            reinterpret_cast<const uint8_t*>(resp.c_str()), resp.size());
}

void GameService::DispatchMove_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    int32_t map_id = 0, x = 50, y = 50;
#ifdef SNC_ENABLE_FLATBUFFERS
    DecodePayload<MoveRequest>(payload, len, [&](const MoveRequest& r) {
        map_id = r.map_id(); x = r.x(); y = r.y();
    });
#else
    (void)len;
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(std::string(payload, len).substr(p + kv.size())); }
        catch (...) { return 0; }
    };
    map_id = get_int("map_id");
    x = get_int("x");
    y = get_int("y");
#endif
    bool ok = plr->MoveTo(map_id, x, y);
    // 发送 MoveResponse
    std::string resp_data = ok
        ? ("{\"type\":\"move\",\"success\":true,\"x\":" + std::to_string(x) +
           ",\"y\":" + std::to_string(y) + ",\"map_id\":" + std::to_string(map_id) + "}")
        : ("{\"type\":\"move\",\"success\":false,\"error_code\":1}");
    plr->Send(kMsgServerMoveResponse, resp_data);
}

void GameService::DispatchAttack_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)len;
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(std::string(payload, len).substr(p + kv.size())); }
        catch (...) { return 0; }
    };
    int32_t target_id = get_int("target_id");
    int32_t attack_type = get_int("attack_type");

    CombatResult result{};
    bool ok = false;

    if (target_id <= 0) {
        // 攻击怪物：获取玩家周围范围内的存活怪物，找第一个非死亡状态
        auto pos = plr->Pos();
        auto monsters = monster_mgr_->GetMonstersInRadius(pos.mapId, pos.x, pos.y, 5);
        for (auto& m : monsters) {
            if (m && m->state != MonsterState::DEAD) {
                result = combat_->PlayerAttackMonster(plr, m,
                    static_cast<AttackType>(attack_type));
                ok = true;
                break;
            }
        }
        if (!ok) {
            plr->Send(kMsgServerError, "{\"error\":\"no_target\"}");
            return;
        }
    } else {
        // 攻击玩家（PK 需过安全区判断）
        auto target = FindPlayer(target_id);
        if (!target) {
            plr->Send(kMsgServerError, "{\"error\":\"target_not_found\"}");
            return;
        }
        if (!CombatSystem::CanPK(plr->Pos(), target->Pos(), *scene_mgr_)) {
            plr->Send(kMsgServerError, "{\"error\":\"safe_zone_no_pk\"}");
            return;
        }
        result = combat_->PlayerAttackPlayer(plr, target,
            static_cast<AttackType>(attack_type));
        ok = true;
    }

    std::string resp = "{\"type\":\"attack\",\"target_id\":"
        + std::to_string(target_id)
        + ",\"damage\":" + std::to_string(result.damage)
        + ",\"critical\":" + (result.is_critical ? "true" : "false")
        + "}";
    plr->Send(kMsgServerDamageResult, resp);
}

void GameService::DispatchNPC_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)len;
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(std::string(payload, len).substr(p + kv.size())); }
        catch (...) { return 0; }
    };
    int32_t npc_id  = get_int("npc_id");
    int32_t action  = get_int("action");   // 0=Talk 1=Buy 2=Sell 3=Teleport

    if (npc_id <= 0) {
        plr->Send(kMsgServerError, "{\"error\":\"invalid_npc_id\"}");
        return;
    }
    std::string result;
    switch (action) {
        case 0: result = npc_mgr_->Talk(plr, npc_id);              break;
        case 1: result = npc_mgr_->Buy(plr, npc_id, get_int("item_id"), get_int("count")); break;
        case 2: result = npc_mgr_->Sell(plr, npc_id, get_int("item_id"), get_int("count")); break;
        case 3: result = npc_mgr_->Teleport(plr, npc_id, get_int("dest_index")); break;
        default: result = "{\"error\":\"unknown_action\"}";          break;
    }
    plr->Send(kMsgServerPlayerSync, result);
}

void GameService::DispatchChat_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)len;
    auto get_str = [&](const char* key) -> std::string {
        std::string kv = "\"" + std::string(key) + "\":\"";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return {};
        p += kv.size();
        auto end = std::string(payload, len).find('"', p);
        if (end == std::string::npos) return {};
        return std::string(payload + p, end - p);
    };
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(std::string(payload, len).substr(p + kv.size())); }
        catch (...) { return 0; }
    };
    ChatChannel channel = static_cast<ChatChannel>(get_int("channel"));
    std::string content = get_str("content");
    if (content.empty() || channel < ChatChannel::WORLD || channel > ChatChannel::WHISPER) {
        plr->Send(kMsgServerError, "{\"error\":\"invalid_chat\"}");
        return;
    }
    bool sent = world_chan_->Send(channel, plr->PlayerId(), content,
                                   get_int("target_id"), 0);
    if (!sent) {
        plr->Send(kMsgServerError, "{\"error\":\"chat_failed\"}");
        return;
    }
}

void GameService::DispatchGuild_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)len;
    auto get_str = [&](const char* key) -> std::string {
        std::string kv = "\"" + std::string(key) + "\":\"";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return {};
        p += kv.size();
        auto end = std::string(payload, len).find('"', p);
        if (end == std::string::npos) return {};
        return std::string(payload + p, end - p);
    };
    auto get_int = [&](const char* key) -> int32_t {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = std::string(payload, len).find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(std::string(payload, len).substr(p + kv.size())); }
        catch (...) { return 0; }
    };
    int32_t action = get_int("action"); // 0=Create 1=Join 2=Leave 3=List
    std::string result;
    switch (action) {
        case 0: { // Create guild
            std::string name = get_str("name");
            if (name.empty()) { result = "{\"error\":\"empty_name\"}"; break; }
            int32_t gid = guild_mgr_->CreateGuild(plr, name);
            if (gid <= 0) { result = "{\"error\":\"create_failed\"}"; break; }
            result = ("{\"action\":\"create\",\"guild_id\":" + std::to_string(gid) +
                      ",\"name\":\"" + name + "\"}");
            break;
        }
        case 1: { // Join guild
            int32_t guild_id = get_int("guild_id");
            if (guild_id <= 0) { result = "{\"error\":\"invalid_guild_id\"}"; break; }
            bool ok = guild_mgr_->JoinGuild(plr, guild_id);
            result = ok ? "{\"action\":\"join\",\"guild_id\":" + std::to_string(guild_id) + "}"
                        : "{\"error\":\"join_failed\"}";
            break;
        }
        case 2: { // Leave guild
            guild_mgr_->LeaveGuild(plr);
            result = "{\"action\":\"leave\"}";
            break;
        }
        case 3: { // List all guilds
            auto list = guild_mgr_->ListAllGuilds();
            std::string parts;
            for (auto& g : list)
                parts += ("{\"id\":" + std::to_string(g->guild_id) +
                          ",\"name\":\"" + g->name + "\"},");
            if (!parts.empty()) parts.pop_back();
            result = ("{\"action\":\"list\",\"guilds\":[") + parts + "]}";
            break;
        }
        default:
            result = "{\"error\":\"unknown_guild_action\"}";
            break;
    }
    plr->Send(kMsgServerGuildNotice, result);
}

void GameService::DispatchPickup_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)payload; (void)len;
    int picked = drop_mgr_->PickUp(plr, 2);
    plr->Send(kMsgServerDropNotify,
        ("{\"picked\":" + std::to_string(picked) + "}").c_str(), 24);
}

void GameService::DispatchSkill_(PlayerActor::Ptr plr, const char* payload, size_t len) {
    (void)plr; (void)payload; (void)len;
    plr->Send(kMsgServerDamageResult, "{\"type\":\"skill\",\"status\":\"ok\"}", 33);
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
