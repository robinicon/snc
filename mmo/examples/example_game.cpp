/// @file example_game.cpp
/// @brief 传奇版 Game 服单进程演示 — 场景/实体/战斗/掉落基础流程

#include "snc/mmorpg/legend/GameService.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include "snc/core/Msg.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace snc::mmorpg::legend;
using namespace snc::core;
using namespace snc::auth;
using namespace snc::rpc;

int main() {
    SNC_LOG_INFO("demo", "=== Legend MMO Game Service Demo ===");

    // 1. 创建必要的基础组件（本示例不依赖真实 Master）
    auto registry = snc::discovery::ServiceRegistry::Create();
    auth::JwtConfig jwtCfg{"legend_secret_key_2024", 7200, "snc", "HS256"};
    rpc::RpcClient::Ptr null_master; // 空 Master 引用（演示用）

    // 2. 创建 GameService
    GameService svc(null_master, registry, jwtCfg);
    svc.Init();

    // 3. 获取组件
    auto sceneMgr = svc.GetSceneMgr();
    auto combat = svc.GetCombat();
    auto dropMgr = svc.GetDropMgr();

    // 4. 创建玩家（通过 GameService）
    svc.CreatePlayer(1, "战士张三", ClassType::WARRIOR, nullptr);
    svc.CreatePlayer(2, "战士李四", ClassType::WARRIOR, nullptr);

    // 获取玩家引用
    auto p1 = svc.FindPlayer(1);
    auto p2 = svc.FindPlayer(2);

    if (!p1 || !p2) {
        SNC_LOG_ERROR("demo", "Failed to create players");
        return 1;
    }

    SNC_LOG_INFO("demo", "Player1 HP=%d MP=%d Level=%d",
                 p1->CurrentHp(), p1->CurrentMp(), p1->Level());
    SNC_LOG_INFO("demo", "Player2 HP=%d MP=%d Level=%d",
                 p2->CurrentHp(), p2->CurrentMp(), p2->Level());

    // 5. 战斗测试
    SNC_LOG_INFO("demo", "--- Combat Test ---");
    auto result = combat->PlayerAttackPlayer(p1, p2, AttackType::PHYSICAL);
    SNC_LOG_INFO("demo", "P1->P2 damage=%d crit=%d", result.damage, result.is_critical);
    SNC_LOG_INFO("demo", "P2 HP after hit: %d", p2->CurrentHp());

    // 6. 背包测试
    SNC_LOG_INFO("demo", "--- Inventory Test ---");
    int added = p1->AddItem(1001, 5);   // 添加 5 个物品 ID 1001
    int removed = p1->RemoveItem(1001, 2);
    SNC_LOG_INFO("demo", "AddItem(1001,5) -> added=%d, HasItem=%d", added, p1->HasItem(1001, 3));
    SNC_LOG_INFO("demo", "RemoveItem(1001,2) -> removed=%d", removed);

    // 7. 经验/升级测试
    SNC_LOG_INFO("demo", "--- Level Up Test ---");
    int64_t needed = PlayerActor::ExpNeeded(1);
    p1->AddExp(needed);
    SNC_LOG_INFO("demo", "P1 level now: %d", p1->Level());

    // 8. 掉落测试
    SNC_LOG_INFO("demo", "--- Drop Test ---");
    auto drops = dropMgr->RollDrops(1, 1); // 从掉落表 1 掷一次
    for (auto& [id, cnt] : drops)
        dropMgr->CreateDrop(1, 50, 50, id, cnt);
    SNC_LOG_INFO("demo", "Created drops in map 1, count=%zu", dropMgr->DropCount(1));

    // 9. 世界频道测试
    auto worldChan = svc.GetWorldChan();
    worldChan->Send(ChatChannel::WORLD, 1, "Hello World!");
    worldChan->Send(ChatChannel::WORLD, 2, "Hi Zhang San!");
    auto history = worldChan->GetWorldHistory(5);
    SNC_LOG_INFO("demo", "World chat history size: %zu", history.size());

    // 10. 行会测试
    auto guildMgr = svc.GetGuildMgr();
    int32_t gid = guildMgr->CreateGuild(p1, "铁血盟", 5, 5000);
    SNC_LOG_INFO("demo", "Guild created: id=%d name=%s", gid, p1->Guild().guild_name.c_str());
    guildMgr->JoinGuild(p2, gid);
    SNC_LOG_INFO("demo", "P2 joined guild: rank=%d", p2->Guild().guild_rank);

    // 11. 安全区 PK 禁止测试
    SNC_LOG_INFO("demo", "--- Safe Zone PK Test ---");
    bool canPK = CombatSystem::CanPK(p1->Pos(), p2->Pos(), *sceneMgr);
    auto scene = sceneMgr->GetScene(p1->Pos().mapId);
    SNC_LOG_INFO("demo", "Can PK in map %d (safe_zone=%s): %s",
                 p1->Pos().mapId,
                 scene && scene->IsSafeZone() ? "yes" : "no",
                 canPK ? "YES (bug!)" : "NO (correct)");

    // 12. 玩家状态输出
    SNC_LOG_INFO("demo", "P1 state: %s", p1->DumpState().c_str());
    SNC_LOG_INFO("demo", "=== Demo Complete ===");

    return 0;
}
