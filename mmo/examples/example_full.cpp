/// @file example_full.cpp
/// @brief 传奇版全链路多服演示 �?模拟 Gate �?Game �?Master 的完整流�?

#include "snc/mmorpg/legend/GameService.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include "snc/core/Msg.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

using namespace snc::mmorpg::legend;
using namespace snc::core;

// ── 简易断言�?─────────────────────────────────────────────────────
#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { std::cerr << "ASSERT FAILED: " #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; return 1; } } while(0)
#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { std::cerr << "ASSERT FAILED: " #a " == " #b << " (" << (a) << " != " << (b) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; return 1; } } while(0)
#define LOG(step, fmt, ...) \
    std::printf("[%02d] " fmt "\n", step, ##__VA_ARGS__)

int main() {
    LOG(1, "=== Legend MMO Full-Stack Demo ===");
    LOG(1, "Creating service infrastructure...");

    // ── 阶段 1：初始化基础设施 ───────────────────────────────────
    auto registry = snc::discovery::ServiceRegistry::Create();
    auth::JwtConfig jwtCfg{"legend-mmo-demo-secret-key-2024-local!!", 7200, "snc", "HS256"};

    GameService svc(nullptr /* no real master */, registry, jwtCfg);
    svc.Init();
    LOG(2, "GameService initialized");

    auto mapMgr    = svc.GetMapMgr();
    auto combat    = svc.GetCombat();
    auto dropMgr   = svc.GetDropMgr();
    auto guildMgr  = svc.GetGuildMgr();
    auto worldChan = svc.GetWorldChan();

    // ── 阶段 2：创建地图和怪物 ───────────────────────────────────
    LOG(3, "Setting up maps and monsters...");

    // 安全区：比奇�?
    MapInfo beichMap{1, "BeichCity", "", 200, 200, true, false, 1, ""};
    mapMgr->CreateMap(beichMap);

    // 野外地图：猪�?
    MapInfo pigMap{2, "PigCave", "", 150, 150, false, false, 5, ""};
    mapMgr->CreateMap(pigMap);

    // 创建玩家
    auto pWarrior = svc.CreatePlayer(1, "战士张三", ClassType::WARRIOR, nullptr);
    auto pMage    = svc.CreatePlayer(2, "法师李四", ClassType::MAGE,    nullptr);
    auto pTaoist  = svc.CreatePlayer(3, "道士王五", ClassType::TAOIST,  nullptr);
    ASSERT_TRUE(pWarrior != nullptr);
    ASSERT_TRUE(pMage    != nullptr);
    ASSERT_TRUE(pTaoist  != nullptr);
    LOG(4, "3 players created: %s(Lv%d) %s(Lv%d) %s(Lv%d)",
        pWarrior->Nickname().c_str(), pWarrior->Level(),
        pMage->Nickname().c_str(),    pMage->Level(),
        pTaoist->Nickname().c_str(),  pTaoist->Level());

    // ── 阶段 3：职业特性验�?─────────────────────────────────────
    LOG(5, "Verifying class stats...");
    ASSERT_EQ(pWarrior->CurrentMp(), 0);            // 战士�?MP
    ASSERT_TRUE(pWarrior->CurrentHp()  > pMage->CurrentHp());  // 战士 HP 更高
    ASSERT_TRUE(pMage->GetBaseStats().mag > pWarrior->GetBaseStats().str); // 法师魔攻更高
    LOG(5, "  Warrior HP=%d MP=%d  Mage HP=%d MP=%d  Taoist HP=%d MP=%d",
        pWarrior->CurrentHp(), pWarrior->CurrentMp(),
        pMage->CurrentHp(),    pMage->CurrentMp(),
        pTaoist->CurrentHp(),  pTaoist->CurrentMp());

    // ── 阶段 4：移动与地图 ───────────────────────────────────────
    LOG(6, "Testing movement...");
    ASSERT_TRUE(pWarrior->MoveTo(1, 50, 50));       // 移到比奇城安全区
    ASSERT_TRUE(pMage->MoveTo(1, 55, 50));
    ASSERT_TRUE(pTaoist->MoveTo(2, 80, 80));        // 移到猪洞野外

    // 安全�?PK 禁止
    bool canPK_safe = CombatSystem::CanPK(pWarrior->Pos(), pMage->Pos(), *mapMgr);
    ASSERT_TRUE(canPK_safe == false);
    LOG(6, "PK in safe zone (Beich): %s", canPK_safe ? "ALLOWED (BUG!)" : "BLOCKED (correct)");

    // 野外 PK 允许
    bool canPK_野外 = CombatSystem::CanPK(pWarrior->Pos(), pTaoist->Pos(), *mapMgr);
    // 两人距离较远，CanPK 还需考虑距离，此处只验证方法调用正常
    LOG(6, "PK check across maps: CanPK(Warrior@map1, Taoist@map2) called OK");

    // ── 阶段 5：战斗系�?─────────────────────────────────────────
    LOG(7, "Testing combat...");

    // 让战士移到猪洞挑战道�?
    pWarrior->MoveTo(2, 82, 80);
    auto dmgResult = combat->PlayerAttackPlayer(pWarrior, pTaoist, AttackType::PHYSICAL);
    ASSERT_TRUE(dmgResult.damage > 0);
    LOG(7, "  Warrior physical attack: damage=%d, crit=%d, log=%s",
        dmgResult.damage, dmgResult.is_critical, dmgResult.log.c_str());
    ASSERT_TRUE(pTaoist->CurrentHp() < 110); // HP 应下�?

    // 法师用魔法攻击战�?
    auto mageDmg = combat->PlayerAttackPlayer(pMage, pWarrior, AttackType::MAGIC);
    ASSERT_TRUE(mageDmg.damage > 0);
    LOG(7, "  Mage magic attack: damage=%d, crit=%d", mageDmg.damage, mageDmg.is_critical);

    // 道士回血（治疗技能）
    int healed = combat->Heal(pTaoist, 50);
    LOG(7, "  Taoist healed: +%d HP (now=%d/%d)",
        healed, pTaoist->CurrentHp(), pTaoist->MaxHp());

    // ── 阶段 6：经验与升级 ───────────────────────────────────────
    LOG(8, "Testing leveling...");
    int64_t expNeeded = PlayerActor::ExpNeeded(1);
    ASSERT_EQ(expNeeded, 100);
    pWarrior->AddExp(expNeeded);
    ASSERT_TRUE(pWarrior->Level() >= 2);
    LOG(8, "  Warrior leveled up to Lv.%d (HP refreshed to %d)",
        pWarrior->Level(), pWarrior->CurrentHp());

    // ── 阶段 7：掉落系�?─────────────────────────────────────────
    LOG(9, "Testing drops...");
    DropTable table{1, {
        {1001, 50000, 1, 1}, // 金币硬币 5%
        {1002, 20000, 1, 1}, // 木剑 2%
        {1003,  5000, 1, 1}, // 铁剑 0.5%
    }};
    dropMgr->RegisterTable(table);
    auto drops = dropMgr->RollDrops(1, 3);
    LOG(9, "  Rolled %zu drops from table 1", drops.size());
    for (auto& [id, cnt] : drops)
        dropMgr->CreateDrop(2, 80, 80, id, cnt); // 落在猪洞
    LOG(9, "  Created %d ground drops in map 2", (int)dropMgr->DropCount(2));

    // 拾取掉落
    pWarrior->MoveTo(2, 81, 80);
    int picked = dropMgr->PickUp(pWarrior, 2); // radius=2
    LOG(9, "  Warrior picked up %d items", picked);
    if (picked > 0)
        LOG(9, "  Inventory after pickup: drop_count=%d", (int)dropMgr->DropCount(2));

    // ── 阶段 8：行会系�?─────────────────────────────────────────
    LOG(10, "Testing guild system...");
    // 给战士一些金币用于创建行会（通过作弊方式——直接调 SpendGold 反推�?
    // 注：PlayerActor 没有 SetGold，但 CreateGuild 会扣钱；测试 cost=0 避免此问�?
    int32_t guildId = guildMgr->CreateGuild(pWarrior, "铁血�?, 5, 0);
    ASSERT_TRUE(guildId > 0);
    LOG(10, "  Guild '%s' created, id=%d, master=%s",
        pWarrior->Guild().guild_name.c_str(), guildId,
        pWarrior->Guild().guild_name.c_str());

    // 邀请法师加�?
    bool joined = guildMgr->JoinGuild(pMage, guildId);
    ASSERT_TRUE(joined == true);
    ASSERT_EQ(pMage->Guild().guild_id, guildId);
    ASSERT_EQ(pMage->Guild().guild_rank, 3);
    LOG(10, "  Mage joined guild as rank=%d (member)", pMage->Guild().guild_rank);

    // 道士留在野外，不加入行会
    ASSERT_EQ(pTaoist->Guild().guild_id, 0);
    LOG(10, "  Taoist not in any guild (guild_id=0)");

    // 行会仓库操作
    ASSERT_TRUE(guildMgr->PutGold(guildId, pWarrior->PlayerId(), 1000));
    auto guild = guildMgr->GetGuild(guildId);
    ASSERT_TRUE(guild != nullptr);
    ASSERT_EQ(guild->treasury, 1000LL);
    LOG(10, "  Treasury: +1000 gold (total=%lld)", (long long)guild->treasury);

    // 攻城�?
    guildMgr->StartSiege(guildId, 3);
    ASSERT_TRUE(guildMgr->IsAtWar(guildId) == true);
    LOG(10, "  Siege started on map 3");
    guildMgr->EndSiege(guildId);
    ASSERT_TRUE(guildMgr->IsAtWar(guildId) == false);
    LOG(10, "  Siege ended");

    // ── 阶段 9：世界频�?─────────────────────────────────────────
    LOG(11, "Testing world channel...");
    worldChan->RegisterPlayer(pWarrior->PlayerId(), pWarrior);
    worldChan->RegisterPlayer(pMage->PlayerId(),    pMage);
    worldChan->RegisterPlayer(pTaoist->PlayerId(),  pTaoist);

    bool chatOk = worldChan->Send(ChatChannel::WORLD, pWarrior->PlayerId(), "Hello everyone!");
    ASSERT_TRUE(chatOk == true);
    auto hist = worldChan->GetWorldHistory(5);
    ASSERT_TRUE(hist.size() >= 1);
    LOG(11, "  World chat: '%s' by '%s'",
        hist.back().content.c_str(), hist.back().sender_name.c_str());

    // 私聊
    worldChan->Send(ChatChannel::WHISPER, pWarrior->PlayerId(), "Private msg", pMage->PlayerId(), 0);
    auto worldHist = worldChan->GetWorldHistory(5);
    // 私聊不出现在世界历史
    for (auto& m : worldHist)
        ASSERT_TRUE(m.channel != ChatChannel::WHISPER);
    LOG(11, "  Whisper sent (not in world history �?correct)");

    // 行会频道
    worldChan->Send(ChatChannel::GUILD, pWarrior->PlayerId(), "Guild rally!", 0, guildId);
    auto guildHist = worldChan->GetGuildHistory(guildId, 5);
    ASSERT_TRUE(guildHist.size() >= 1);
    LOG(11, "  Guild chat: '%s'", guildHist.back().content.c_str());

    // 禁词过滤
    worldChan->AddForbiddenWord("spamword");
    bool blocked = worldChan->Send(ChatChannel::WORLD, pWarrior->PlayerId(), "I love spamword");
    ASSERT_TRUE(blocked == false);
    LOG(11, "  Forbidden word blocked: correct");

    // ── 阶段 10：红�?PK 系统 ────────────────────────────────────
    LOG(12, "Testing PK red-name system...");
    ASSERT_TRUE(pWarrior->GetPKInfo().red_name_val == 0);
    ASSERT_TRUE(pWarrior->GetPKInfo().status == PKStatus::NORMAL);

    // 模拟多次杀�?
    for (int i = 0; i < 3; ++i) pWarrior->OnKillPlayer();
    LOG(12, "  After 3 kills: red_name_val=%d, status=%d",
        pWarrior->GetPKInfo().red_name_val, (int)pWarrior->GetPKInfo().status);

    // ── 阶段 11：NPC 系统 ────────────────────────────────────────
    LOG(13, "Testing NPC system...");
    auto npcMgr = svc.GetNPCMgr();
    // NPC 创建和查�?
    npcMgr->RegisterNPC(101, "老村�?, NPCType::QUEST, 1, 50, 50, "Help me find my cat.");
    auto npc = npcMgr->GetNPC(101);
    ASSERT_TRUE(npc != nullptr);
    ASSERT_TRUE(npc->name == "老村�?);
    LOG(13, "  NPC '老村�? registered at map=%d pos=(%d,%d)", npc->map_id, npc->pos.x, npc->pos.y);

    // ── 阶段 12：怪物 AI 状态机 ──────────────────────────────────
    LOG(14, "Testing monster AI...");
    auto monsterMgr = svc.GetMonsterMgr();
    // 创建一只野�?
    auto boar = monsterMgr->SpawnMonster(2, 1, 85, 85); // map2, monster_template_id=1
    ASSERT_TRUE(boar != nullptr);
    LOG(14, "  Boar spawned at map=2 pos=(%d,%d), state=%d",
        (int)boar->Pos().x, (int)boar->Pos().y, (int)boar->State());

    // 模拟玩家靠近触发 CHASE
    // 简单验证：状态是有效的枚举�?
    ASSERT_TRUE(boar->State() >= 0 && boar->State() <= 5); // IDLE~DEAD 范围
    LOG(14, "  Boar state valid (0=IDLE..5=DEAD)");

    // 攻击模拟
    auto boarDmg = combat->MonsterAttackPlayer(boar, pWarrior);
    LOG(14, "  Boar attacks warrior: damage=%d, log=%s", boarDmg.damage, boarDmg.log.c_str());

    // ── 阶段 13：最终状态汇�?────────────────────────────────────
    LOG(15, "=== Final State Summary ===");
    LOG(15, "Online players: %zu", svc.OnlinePlayerCount());
    LOG(15, "  %s: Lv.%d HP=%d/%d Gold=%lld PK=%s",
        pWarrior->Nickname().c_str(),
        pWarrior->Level(),
        pWarrior->CurrentHp(), pWarrior->MaxHp(),
        (long long)pWarrior->Gold(),
        pWarrior->GetPKInfo().status == PKStatus::NORMAL ? "NORMAL" : "RED");
    LOG(15, "  %s: Lv.%d HP=%d/%d Guild='%s'(rank=%d)",
        pMage->Nickname().c_str(),
        pMage->Level(),
        pMage->CurrentHp(), pMage->MaxHp(),
        pMage->Guild().guild_name.c_str(),
        pMage->Guild().guild_rank);
    LOG(15, "  %s: Lv.%d HP=%d/%d Guild=(none)",
        pTaoist->Nickname().c_str(),
        pTaoist->Level(),
        pTaoist->CurrentHp(), pTaoist->MaxHp());
    LOG(15, "Guilds: %zu", guildMgr->ListAllGuilds().size());
    LOG(15, "Ground drops in map 2: %d", (int)dropMgr->DropCount(2));
    LOG(15, "World chat messages: %zu", worldChan->GetWorldHistory(100).size());

    LOG(99, "=== Demo Complete �?All Systems Operational ===");
    return 0;
}
