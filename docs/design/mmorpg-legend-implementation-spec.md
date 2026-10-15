# SNC MMORPG 传奇版本 — 实施规格文档

> **版本**：v1.0
> **日期**：2026-10-13
> **基于**：`mmorpg-legend-server-design.md`（服务器整体设计方案）
> **框架**：snc C++17 游戏服务器网络框架
> **作者**：Agnes

---

## 目录

1. [文档概述](#一文档概述)
2. [新增模块详细设计](#二新增模块详细设计)
3. [现有模块增强说明](#三现有模块增强说明)
4. [消息 ID 完整分配表](#四消息-id-完整分配表)
5. [关键数据结构详述](#五关键数据结构详述)
6. [各模块伪代码实现](#六各模块伪代码实现)
7. [单元测试规格](#七单元测试规格)
8. [实施检查清单](#八实施检查清单)
9. [附录：传奇经典数据参考](#九附录传奇经典数据参考)

---

## 一、文档概述

本文档是基于 `mmorpg-legend-server-design.md` 生成的详细实施规格，用于指导 Legend 传奇版本的服务器代码开发。文档涵盖以下范围：

| 类别 | 内容 |
|------|------|
| 新增模块 | PlayerActor.h、NPCManager.h、MonsterManager.h、CombatSystem.h、DropManager.h、GuildManager.h、WorldChannel.h |
| 现有模块增强 | GameService.h、MapManager.h、WorldService.h、MasterService.h |
| 协议规范 | 消息 ID 分配表（0x1000~0x40FF） |
| 数据结构 | ItemSlot、EquipmentSlot、SkillDef、PlayerPos 等核心结构 |
| 参考实现 | 核心逻辑伪代码、单元测试规格、实施检查清单 |

**目标读者**：负责传奇版本实现的 C++ 开发工程师。
**前置知识**：阅读过 `mmorpg-legend-server-design.md`，熟悉 snc 框架基础组件。

---

## 二、新增模块详细设计

### 2.1 PlayerActor.h（增强版完整头文件）

**文件路径**：`include/snc/mmorpg/PlayerActor.h`

**设计要点**：
- 继承自现有 `Actor` 基类（拥有 OnMessage/Tick 虚函数）
- 新增职业（ClassType）、背包（ItemSlot[30]）、装备（EquipmentSlot[10]）
- 新增技能（skills_ unordered_map）、PK 状态（red_name_val）、行会信息
- 属性分两层：base_stats_（基础属性）+ equip_stats_（装备附加），计算总属性用 GetTotalStats()
- SpawnPos 记录出生点，便于死亡复活或传送回城

```cpp
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <chrono>
#include "snc/mmorpg/MapManager.h"

namespace snc {
namespace mmorpg {

// 职业枚举
enum class ClassType : int {
    WARRIOR  = 0,  // 战士：高HP高防，近战物理
    MAGICIAN = 1,  // 法师：低HP高攻，远程魔法
    TAOIST   = 2,  // 道士：中等HP，召唤+毒+治愈
};

// 基础属性
struct BaseStats {
    int32_t max_hp  = 100;
    int32_t max_mp  = 0;
    int32_t attack  = 10;
    int32_t magic   = 0;
    int32_t defense = 5;
    int32_t mag_def = 0;
};

// 职业初始属性配置
struct ClassConfig {
    BaseStats base;
    int32_t   exp_per_level;
};

// 背包格子
struct ItemSlot {
    int32_t item_id = 0;
    int32_t count   = 0;
    int32_t dur     = 0;  // 装备耐久度，0 表示非装备
};

// 装备槽位
enum class EquipSlot : int {
    WEAPON   = 0,  // 武器
    HELM     = 1,  // 头盔
    CHAIN    = 2,  // 项链
    ARMOR    = 3,  // 盔甲
    SHIELD   = 4,  // 盾牌
    GLOVES   = 5,  // 手套
    BOOTS    = 6,  // 鞋子
    RING_L   = 7,  // 左手戒指
    RING_R   = 8,  // 右手戒指
    BRACELET = 9,  // 手镯
};

struct EquipmentSlot {
    int32_t item_id = 0;
    int32_t dur     = 0;
};

// PK 状态
enum class PKStatus : int {
    NORMAL   = 0,
    RED_NAME = 1,       // 红名 (>100 红名值)
    VERY_RED = 2,       // 血红名 (>200 红名值)
};

struct PKInfo {
    int32_t  kill_count    = 0;
    int32_t  red_name_val  = 0;
    int64_t  last_kill_ts  = 0;
    PKStatus status        = PKStatus::NORMAL;
};

// 玩家位置
struct PlayerPos {
    int32_t map_id = 0;
    int32_t x      = 0;
    int32_t y      = 0;
};

// 玩家 Actor（增强版）
class PlayerActor : public Actor {
public:
    using Ptr = std::shared_ptr<PlayerActor>;

    static constexpr int kBagSlots  = 30;
    static constexpr int kEquipSlots = 10;

    PlayerActor(int32_t id, const std::string& name, ClassType cls,
                core::IConnection::Ptr conn, MapManager::Ptr mapMgr)
        : Actor(id), player_id_(id), nickname_(name), class_type_(cls),
          conn_(std::move(conn)), map_mgr_(std::move(mapMgr)),
          login_time_(std::chrono::steady_clock::now()) {
        InitBaseStats();
    }

    // ── 基础信息 ───────────────────────────────────────────
    int32_t         player_id_;
    std::string     nickname_;
    ClassType       class_type_;
    int32_t         level_           = 1;
    int64_t         exp_             = 0;
    int64_t         gold_            = 0;

    // ── 属性 ───────────────────────────────────────────────
    BaseStats       base_stats_;
    BaseStats       equip_stats_;
    int32_t         current_hp_      = 100;
    int32_t         current_mp_      = 0;

    // ── 背包 ───────────────────────────────────────────────
    ItemSlot        bag_[kBagSlots] = {};

    // ── 装备 ───────────────────────────────────────────────
    EquipmentSlot   equip_[kEquipSlots] = {};

    // ── 技能 ───────────────────────────────────────────────
    std::unordered_map<int32_t, int32_t> skills_;  // skillId → level

    // ── 位置 ───────────────────────────────────────────────
    PlayerPos   pos_;
    PlayerPos   spawn_pos_;

    // ── PK ───────────────────────────────────────────────
    PKInfo      pk_info_;

    // ── 行会 ───────────────────────────────────────────────
    int32_t     guild_id_     = 0;
    int32_t     guild_rank_   = 0;  // 1=会长 2=副会 3=成员 0=无行会

    // ── 战斗状态 ───────────────────────────────────────────
    int32_t       attack_target_id_ = 0;
    int64_t       last_attack_ts_   = 0;
    int64_t       last_skill_ts_    = 0;

    // ── 接口 ───────────────────────────────────────────────
    void OnMessage(int32_t msgId, const char* payload, size_t len) override;
    void Tick(long long elapsedMs) override;

    core::IConnection::Ptr GetConn() const { return conn_.lock(); }
    const std::string&     GetName() const { return nickname_; }
    const BaseStats&       GetBaseStats() const { return base_stats_; }
    const BaseStats&       GetEquipStats() const { return equip_stats_; }
    PlayerPos              GetPos()    const { return pos_; }
    ClassType              GetClass()  const { return class_type_; }

    void Send(int32_t msgId, const char* data, size_t len) {
        auto c = conn_.lock();
        if (c) c->Send(core::Msg::Create(msgId, data, len));
    }

    // 计算总属性（基础 + 装备）
    BaseStats GetTotalStats() const {
        BaseStats total = base_stats_;
        total.max_hp  += equip_stats_.max_hp;
        total.max_mp  += equip_stats_.max_mp;
        total.attack  += equip_stats_.attack;
        total.magic   += equip_stats_.magic;
        total.defense += equip_stats_.defense;
        total.mag_def += equip_stats_.mag_def;
        return total;
    }

private:
    void InitBaseStats();
    int32_t CalcMaxHp() const;
    int32_t CalcMaxMp() const;

    std::weak_ptr<core::IConnection> conn_;
    MapManager::Ptr                  map_mgr_;
    std::chrono::steady_clock::time_point login_time_;
};

} // namespace mmorpg
} // namespace snc
```

**InitBaseStats() 实现要点**：
- 根据 ClassType 设置不同的初始属性
- 战士：max_hp=150, attack=15, defense=10；法师：max_hp=100, magic=20；道士：max_hp=130, attack=10, magic=10

---

### 2.2 NPCManager.h（完整头文件 + 实现要点）

**文件路径**：`include/snc/mmorpg/NPCManager.h`

**设计要点**：
- NPC 按地图分组管理，支持类型分发（商店/任务/传送/训练/交易）
- 使用空间索引（无序 map key=mapId）快速定位地图上的 NPC
- 商店买卖逻辑由 GameService 调用，NPCManager 只做配置查询和合法性校验

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "snc/mmorpg/PlayerActor.h"

namespace snc {
namespace mmorpg {

// NPC 类型
enum class NpcType : int {
    SHOP       = 0,  // 商店（买/卖物品）
    QUEST      = 1,  // 任务 NPC
    TELEPORT   = 2,  // 传送 NPC
    TRAINING   = 3,  // 训练 NPC（技能学习）
    TRADE      = 4,  // 交易 NPC（拍卖行）
};

// NPC 商店数据
struct ShopData {
    struct ItemPrice {
        int32_t item_id;
        int32_t price;
    };
    std::vector<ItemPrice> buy_list;   // NPC 出售的物品
    std::vector<ItemPrice> sell_list;  // NPC 收购物品
};

// NPC 任务数据
struct QuestData {
    int32_t quest_id;
    std::string quest_name;
    int32_t required_items;  // 所需道具数量（简化版：只有一种）
    int32_t reward_exp;
    int32_t reward_gold;
    std::vector<std::pair<int32_t, int32_t>> reward_items;  // itemId, count
};

// NPC 传送数据
struct TeleportData {
    int32_t target_map_id;
    int32_t target_x, target_y;
    int32_t cost_gold;
};

// NPC 配置
struct NpcConfig {
    int32_t     npc_id;
    std::string name;
    NpcType     type;
    int32_t     map_id;
    int32_t     x, y;
    std::string dialogue;

    ShopData    shop;
    QuestData   quest;
    TeleportData teleport;
};

class NPCManager {
public:
    void AddNpc(const NpcConfig& cfg);
    NpcConfig* FindNpcAt(int32_t mapId, int32_t x, int32_t y, int range = 2);
    NpcConfig* FindNpcById(int32_t npcId);

    // NPC 交互（GameService 调用）
    std::string GetDialogue(int32_t npcId, int32_t playerId);
    bool        BuyItem(int32_t npcId, int32_t playerId, int32_t itemId, int32_t count);
    bool        SellItem(int32_t npcId, int32_t playerId, int32_t itemId, int32_t count);
    bool        Teleport(int32_t npcId, PlayerActor::Ptr player);
    bool        AcceptQuest(int32_t npcId, PlayerActor::Ptr player);
    bool        CompleteQuest(int32_t npcId, PlayerActor::Ptr player);
    void        LoadConfigs(const std::string& config_path);

private:
    struct SpatialIndex {
        int32_t x, y;
        size_t  npc_index;
    };

    std::mutex mtx_;
    std::vector<NpcConfig> npcs_;
    std::unordered_map<int32_t, size_t> npc_id_index_;
    std::unordered_map<int32_t, std::vector<SpatialIndex>> map_npc_list_;
};

} // namespace mmorpg
} // namespace snc
```

**实现要点**：
- `FindNpcAt()`：遍历指定地图内的 NPC 列表，计算 Manhattan 距离，找到距离最近的 NPC
- `BuyItem()`：检查金币是否足够，扣除金币，将物品加入背包，发送背包更新消息
- `SellItem()`：检查背包是否有物品，扣除物品，增加金币
- `Teleport()`：直接调用 GameService 的 TeleportPlayer()，传送前检查金币
- 配置文件加载：从 JSON 文件读取 NPC 列表（`docs/data/npc_config.json`）

---

### 2.3 MonsterManager.h（完整头文件 + AI 状态机）

**文件路径**：`include/snc/mmorpg/MonsterManager.h`

**设计要点**：
- 怪物 AI 采用有限状态机（FSM）：IDLE→PATROL→CHASE→ATTACK→RETURN→DEAD
- 每个地图独立管理活跃怪物，Tick 时遍历并更新
- 死亡后进入 DEAD 状态，记录复活时间，计时到期后自动回到 IDLE

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <cstdint>
#include "snc/mmorpg/PlayerActor.h"

namespace snc {
namespace mmorpg {

// 怪物 AI 状态
enum class MonsterState : int {
    IDLE,      // 原地待机
    PATROL,    // 按预设路径巡逻
    CHASE,     // 追击玩家
    ATTACK,    // 攻击目标
    RETURN,    // 受伤后退回到起点
    DEAD,      // 死亡（等待复活）
};

// 怪物配置（来自配置文件）
struct MonsterConfig {
    int32_t  monsterId;
    std::string name;
    int32_t  hp, attack, defense;
    int32_t  speed;           // 格子/秒
    int32_t  attack_range;    // 攻击范围
    int32_t  vision_range;    // 视野范围
    float    drop_rate;       // 基础掉落率
    struct DropEntry {
        int32_t item_id;
        std::pair<int32_t, int32_t> count_range;  // min, max
        float probability;
    };
    std::vector<DropEntry> drops;
    struct PatrolPoint { int32_t x, y; };
    std::vector<PatrolPoint> patrol_path;
};

// 活跃怪物实例
struct ActiveMonster {
    int32_t       monster_id;
    int32_t       map_id;
    int32_t       x, y;
    int32_t       hp;
    int32_t       max_hp;
    int32_t       owner_player_id = 0;  // 上次攻击者（归属权）
    std::chrono::steady_clock::time_point respawn_ts;
    MonsterState  state = MonsterState::IDLE;
    size_t        patrol_index = 0;       // 当前巡逻点索引
    int32_t       chase_target_id = 0;    // 追击目标玩家 ID
    int32_t       last_attack_ms = 0;     // 上次攻击时间（毫秒）
};

class MonsterManager {
public:
    void LoadConfig(const std::string& config_path);
    void SpawnMonsters(int32_t mapId);
    void DespawnAll(int32_t mapId);
    void Tick(long long elapsedMs);
    void OnMonsterHit(int32_t mapId, int32_t monsterLocalId,
                      int32_t attackerId, int32_t damage);
    std::vector<ActiveMonster*> GetMonstersInMap(int32_t mapId);
    ActiveMonster* FindMonster(int32_t mapId, int32_t localId);

private:
    void UpdateState_(ActiveMonster& m, long long elapsedMs);
    void UpdatePatrol_(ActiveMonster& m, long long elapsedMs);
    void UpdateChase_(ActiveMonster& m, long long elapsedMs);
    void UpdateAttack_(ActiveMonster& m, long long elapsedMs);
    void UpdateReturn_(ActiveMonster& m, long long elapsedMs);
    bool IsPlayerInVision_(const ActiveMonster& m, int32_t playerId,
                           const PlayerPos& pos);
    int32_t CalcDistance_(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

    std::mutex mtx_;
    std::unordered_map<int32_t, std::vector<MonsterConfig>> monster_configs_;
    std::unordered_map<int32_t, std::vector<std::shared_ptr<ActiveMonster>>> active_monsters_;
    int32_t next_local_id_ = 1;
};

} // namespace mmorpg
} // namespace snc
```

**AI 状态机转换图**：
```
IDLE ──(有玩家在视野内)──► CHASE
CHASE ──(进入攻击范围)──► ATTACK
CHASE ──(玩家超出视野)──► PATROL
ATTACK ──(目标死亡)──► IDLE
ATTACK ──(目标超出范围)──► CHASE
ATTACK ──(受到重伤害)──► RETURN
RETURN ──(回到起点且HP>30%)──► IDLE
RETURN ──(仍在危险区域)──► CHASE
DEAD ──(计时到期)──► IDLE
```

---

### 2.4 CombatSystem.h（完整头文件 + 伤害公式）

**文件路径**：`include/snc/mmorpg/CombatSystem.h`

**设计要点**：
- 伤害公式采用传奇经典公式：`damage = max(1, atk * 100 / (100 + def))`
- 暴击判定：随机数 < 暴击率时触发，伤害 × 1.5
- 未命中判定：随机数 < 命中闪避差值时触发 miss
- 安全区判断：通过 MapManager 查询地图的 is_safe_zone 标志
- 红名值更新：杀死玩家时增加 red_name_val，跨安全区无效

```cpp
#pragma once

#include <cstdint>
#include <random>
#include "snc/mmorpg/PlayerActor.h"
#include "snc/mmorpg/MonsterManager.h"

namespace snc {
namespace mmorpg {

// 伤害结果
struct DamageResult {
    int32_t  target_id    = 0;
    int32_t  damage       = 0;
    bool     is_crit      = false;
    bool     is_miss      = false;
    bool     is_dead      = false;
};

class CombatSystem {
public:
    CombatSystem(MapManager::Ptr mapMgr) : map_mgr_(std::move(mapMgr)) {}

    // 玩家攻击怪物
    DamageResult CalcPlayerAttack(PlayerActor& attacker,
                                  ActiveMonster& monster,
                                  int32_t skillId = 0);

    // 怪物攻击玩家
    DamageResult CalcMonsterAttack(ActiveMonster& monster,
                                   PlayerActor& player);

    // 玩家攻击玩家（PK）
    DamageResult CalcPlayerVsPlayer(PlayerActor& attacker,
                                    PlayerActor& victim,
                                    int32_t skillId = 0);

    // 检查是否在攻击范围内
    bool InAttackRange(const PlayerPos& atk, const PlayerPos& def,
                       int32_t range);

    // 检查是否在视野范围内
    bool InVisionRange(const PlayerPos& atk, const PlayerPos& def,
                       int32_t visionRange);

    // 检查是否为安全区
    bool IsSafeZone(int32_t mapId, int32_t x, int32_t y);

    // 更新红名值（杀死玩家后调用）
    void OnPlayerKill(PlayerActor& killer, PlayerActor& victim);
    int32_t GetRedNameValue(int32_t playerId) const;

private:
    // 核心伤害公式：damage = max(1, atk * 100 / (100 + def))
    int32_t CalcDamage_(int32_t atk, int32_t def);

    // 暴击判定（5% 基础暴击率）
    bool IsCrit_(int32_t atk, int32_t def);

    // 命中判定（基于攻速和防御差）
    bool IsHit_(int32_t atk, int32_t def);

    MapManager::Ptr map_mgr_;
    mutable std::unordered_map<int32_t, int32_t> red_name_cache_;
    std::mt19937 rng_;
};

} // namespace mmorpg
} // namespace snc
```

**伤害公式详解**：
```
// 传奇经典伤害公式
damage = max(1, atk * 100 / (100 + def))

示例：
  攻击方 atk=50，防御方 def=20 → damage = max(1, 50*100/120) = 41
  攻击方 atk=50，防御方 def=80 → damage = max(1, 50*100/180) = 27
  攻击方 atk=50，防御方 def=200 → damage = max(1, 50*100/300) = 16

暴击：
  base_crit_rate = 0.05（5%）
  if (rng() < base_crit_rate) { damage = damage * 1.5; is_crit = true; }

未命中：
  hit_chance = max(0.1, 1.0 - (def - atk) * 0.002)
  if (rng() > hit_chance) { is_miss = true; damage = 0; }
```

---

### 2.5 DropManager.h（完整头文件 + 掉落表格式）

**文件路径**：`include/snc/mmorpg/DropManager.h`

**设计要点**：
- 掉落物存储在地图上（不跨服），过期时间 30 分钟
- 掉落表为 JSON 文件，每个怪物类型对应一组掉落条目
- 拾取时检查背包空间，空间不足则掉落物保留在原地

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace snc {
namespace mmorpg {

// 地上掉落物实体
struct GroundDrop {
    int64_t  drop_id;       // 唯一标识（全局递增）
    int32_t  map_id;
    int32_t  x, y;
    int32_t  item_id;
    int32_t  count;
    int64_t  owner_id;      // 原拥有者（防止他人拾取）
    int64_t  expire_ts_ms;  // 过期时间戳（毫秒）
};

// 掉落表条目
struct DropEntry {
    int32_t  item_id;
    int32_t  min_count;
    int32_t  max_count;
    float    probability;   // 0.0 ~ 1.0
};

// 掉落表（按怪物ID索引）
using DropTable = std::vector<DropEntry>;

class DropManager {
public:
    DropManager() = default;

    // 怪物死亡后生成掉落物
    std::vector<GroundDrop> GenerateDrops(int32_t mapId, int32_t x, int32_t y,
                                           int32_t monsterId, int32_t killerId);

    // 拾取掉落物，返回是否成功
    bool Pickup(int32_t playerId, int64_t dropId);

    // 删除过期掉落物（由 Tick 定期调用）
    void PruneExpired(long long nowMs);

    // 获取地图上某坐标附近的掉落物
    std::vector<GroundDrop*> GetDropsNear(int32_t mapId, int32_t x, int32_t y, int range);

    // 加载掉落表配置
    void LoadDropTables(const std::string& config_path);

private:
    std::mutex mtx_;
    // 地图上所有存活掉落物
    std::unordered_map<int32_t, std::vector<GroundDrop>> drops_by_map_;
    // 掉落表：monsterId → 掉落条目列表
    std::unordered_map<int32_t, DropTable> drop_tables_;
    int64_t next_drop_id_ = 1;
    static constexpr int64_t kExpireMs = 30 * 60 * 1000;  // 30分钟
};

} // namespace mmorpg
} // namespace snc
```

**掉落表 JSON 格式示例**（`docs/data/drop_tables.json`）：
```json
{
  "1": [
    {"item_id": 1001, "min_count": 1, "max_count": 3, "probability": 0.5},
    {"item_id": 1002, "min_count": 1, "max_count": 1, "probability": 0.1}
  ],
  "2": [
    {"item_id": 1003, "min_count": 1, "max_count": 2, "probability": 0.3}
  ]
}
```

---

### 2.6 GuildManager.h（完整头文件）

**文件路径**：`include/snc/mmorpg/GuildManager.h`

**设计要点**：
- 行会数据暂存内存（ADR-009），生产阶段接入 Redis
- 仓库物品跨登录会话持久化（Level 影响仓库容量）
- 行会等级通过经验值提升，经验来自攻城战胜利

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "snc/mmorpg/PlayerActor.h"

namespace snc {
namespace mmorpg {

enum class GuildRank : int {
    LEADER   = 1,
    VICE     = 2,
    MEMBER   = 3,
};

struct GuildInfo {
    int32_t  guild_id;
    std::string name;
    int32_t  leader_id;
    int32_t  level = 1;
    int64_t  exp = 0;               // 行会经验
    int64_t  create_ts;
    std::unordered_map<int32_t, int32_t> members;  // playerId → rank
    std::vector<int32_t> war_members;             // 参与攻城战成员
};

struct GuildWarehouse {
    std::vector<ItemSlot> items;
    int32_t max_slots = 100;  // level * 100
};

class GuildManager {
public:
    int32_t CreateGuild(int32_t leaderId, const std::string& name);
    bool RequestJoin(int32_t playerId, int32_t guildId);
    bool ApproveJoin(int32_t guildId, int32_t playerId, bool approve);
    bool LeaveGuild(int32_t playerId);
    bool KickMember(int32_t guildId, int32_t playerId, int32_t requesterId);
    bool SetGuildName(int32_t guildId, const std::string& newName);
    bool SetGuildLevel(int32_t guildId, int32_t newLevel);
    GuildInfo* GetGuild(int32_t guildId);
    int32_t GetPlayerGuild(int32_t playerId);
    int32_t GetPlayerRank(int32_t playerId);
    bool WarehouseDeposit(int32_t guildId, int32_t playerId, const ItemSlot& item);
    bool WarehouseWithdraw(int32_t guildId, int32_t playerId, int32_t slotIndex);
    bool CanMemberAct(int32_t guildId, int32_t playerId, GuildRank minRank);

private:
    std::mutex mtx_;
    std::unordered_map<int32_t, GuildInfo> guilds_;
    std::unordered_map<int32_t, int32_t> player_guild_;  // playerId → guildId
    std::unordered_map<int32_t, GuildWarehouse> warehouses_;
    int32_t next_guild_id_ = 1;
};

} // namespace mmorpg
} // namespace snc
```

---

### 2.7 WorldChannel.h（完整头文件）

**文件路径**：`include/snc/mmorpg/WorldChannel.h`

**设计要点**：
- 世界频道通过 WorldService 中转广播到所有 Game 服（ADR-008）
- 行会频道在同一 Game 服内广播，跨服需通过 WorldService 转发
- 私信限制在同 Game 服内（否则返回错误）

```cpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <cstdint>

namespace snc {
namespace mmorpg {

enum class ChatType : int {
    WORLD   = 0,
    GUILD   = 1,
    PRIVATE = 2,
};

class WorldChannel {
public:
    using BroadcastFn = std::function<void(int32_t msgId, const char* data, size_t len)>;

    void RegisterBroadcaster(const std::string& gameService, BroadcastFn fn);
    void UnregisterBroadcaster(const std::string& gameService);

    void BroadcastWorldChat(int32_t senderId, const std::string& senderName,
                            int32_t senderMapId, const std::string& message);
    void BroadcastGuildChat(int32_t guildId, int32_t senderId,
                            const std::string& senderName, const std::string& message);
    void SendPrivateChat(int32_t senderId, const std::string& senderName,
                         int32_t targetId, const std::string& message);
    void BroadcastSystemMessage(const std::string& message);

private:
    std::mutex mtx_;
    struct Broadcaster {
        std::string service_name;
        BroadcastFn fn;
    };
    std::vector<Broadcaster> broadcasters_;
};

} // namespace mmorpg
} // namespace snc
```

---

## 三、现有模块增强说明

### 3.1 GameService.h 需要添加的接口

**文件路径**：`include/snc/mmorpg/GameService.h`（现有文件，需增强）

| 新增方法 | 说明 |
|----------|------|
| `void InitNPCs()` | 加载并初始化所有 NPC |
| `void InitMonsters()` | 加载怪物配置并 spawn 到各地图 |
| `void InitDrops()` | 初始化掉落管理器 |
| `void HandleMove(PlayerActor::Ptr, int8_t dir, int32_t x, int32_t y, int32_t targetMap)` | 处理移动消息 |
| `void HandleAttack(PlayerActor::Ptr, int32_t targetId, int32_t skillId)` | 处理攻击消息 |
| `void HandleNpcInteract(PlayerActor::Ptr, int32_t npcId, int32_t action, int32_t itemId, int32_t count)` | 处理 NPC 交互 |
| `void HandleChat(PlayerActor::Ptr, int8_t chatType, const std::string& msg, int32_t targetId)` | 处理聊天消息 |
| `void HandlePickup(PlayerActor::Ptr, int64_t dropId)` | 处理拾取掉落物 |
| `void HandleUseItem(PlayerActor::Ptr, int32_t slot)` | 处理使用道具 |
| `void HandleCastSkill(PlayerActor::Ptr, int32_t skillId)` | 处理施放技能 |
| `void UpdateTick(long long elapsedMs)` | 每帧更新（怪物AI、掉落清理） |

**新增私有成员**：
```cpp
std::unique_ptr<NPCManager>         npc_mgr_;
std::unique_ptr<MonsterManager>     monster_mgr_;
std::unique_ptr<CombatSystem>       combat_sys_;
std::unique_ptr<DropManager>        drop_mgr_;
std::unique_ptr<GuildManager>       guild_mgr_;
std::unique_ptr<WorldChannel>       world_channel_;
```

### 3.2 MapManager.h 需要增强

**文件路径**：`include/snc/mmorpg/MapManager.h`（现有文件）

| 新增字段/方法 | 说明 |
|--------------|------|
| `bool is_safe_zone = false` | MapInfo 新增字段，标记主城安全区 |
| `bool is_dungeon = false` | 是否为副本（有入口限制） |
| `int32_t min_level = 1` | 最低进入等级 |
| `bool IsSafeZone(int32_t mapId)` | 查询地图是否为安全区 |
| `bool CanEnter(int32_t mapId, int32_t playerLevel)` | 检查玩家是否可以进入 |
| `void LoadMapConfigs(const std::string& path)` | 从 JSON 加载地图配置 |

传奇地图安全区示例：
- 地图 1（比奇省城）：is_safe_zone = true
- 地图 2（盟重省城）：is_safe_zone = true
- 地图 3（沃玛森林）：is_safe_zone = false
- 地图 7（沙巴克城）：is_safe_zone = true（城内 safe，城外可 PK）

### 3.3 WorldService.h 需要增强

**文件路径**：`include/snc/mmorpg/WorldService.h`（现有文件）

| 新增内容 | 说明 |
|----------|------|
| `std::unique_ptr<WorldChannel> world_channel_` | 集成世界频道子系统 |
| `std::unique_ptr<GuildManager> guild_mgr_` | 集成行会系统（跨服） |
| `void RegisterGameService(const std::string& name, ...)` | Game 服注册时通知 World 服 |
| `void OnWorldChatReceived(int32_t senderId, ...)` | 处理来自 Game 服的世界频道消息 |
| `void OnGuildChatReceived(int32_t guildId, ...)` | 处理来自 Game 服的行会频道消息 |

### 3.4 MasterService.h 需要增强

**文件路径**：`include/snc/mmorpg/MasterService.h`（现有文件）

| 新增内容 | 说明 |
|----------|------|
| `std::unordered_map<int32_t, std::string> guild_name_cache_` | 行会名缓存（guildId → name） |
| `std::vector<std::string> system_announcements_` | 全服公告列表 |
| `void BroadcastAnnouncement(const std::string& msg)` | 向所有 Game 服广播系统公告 |
| `std::string GetGuildName(int32_t guildId)` | 查询行会名称 |

---

## 四、消息 ID 完整分配表

| MsgId 范围 | 方向 | 消息名 | 用途 | 状态 |
|------------|------|--------|------|------|
| 0x1000 | Client→Gate | MSG_AUTH | 登录握手（JWT token） | 已有 |
| 0x1001 | Gate→Client | MSG_AUTH_RSP | 登录结果 | 已有 |
| 0x1100 | Client→Gate | MSG_MOVE | 移动指令 | 已有 |
| 0x1101 | Client→Gate | MSG_ATTACK | 攻击指令 | 已有 |
| 0x1102 | Client→Gate | MSG_NPC_INTERACT | NPC 交互 | 已有 |
| 0x1103 | Client→Gate | MSG_PICKUP | 拾取掉落物 | **新增** |
| 0x1104 | Client→Gate | MSG_USE_ITEM | 使用道具 | **新增** |
| 0x1105 | Client→Gate | MSG_CAST_SKILL | 施放技能 | **新增** |
| 0x1110–0x11FF | Client→Gate | — | 移动/动作扩展 | 保留 |
| 0x1200 | Client→Gate | MSG_WORLD_CHAT | 世界频道聊天 | 已有 |
| 0x1201 | Client→Gate | MSG_GUILD_CHAT | 行会频道聊天 | **新增** |
| 0x1202 | Client→Gate | MSG_PRIVATE_CHAT | 私人聊天 | **新增** |
| 0x1210–0x12FF | Client→Gate | — | 聊天/系统扩展 | 保留 |
| 0x2000–0x20FF | Gate→Game | — | 业务消息透传 | 已有 |
| 0x2100 | Game→Gate | MSG_INV_UPDATE | 背包更新 | 已有 |
| 0x2101 | Game→Gate | MSG_STATS_UPDATE | 属性更新 | **新增** |
| 0x2102 | Game→Gate | MSG_POS_UPDATE | 位置更新 | **新增** |
| 0x2103 | Game→Gate | MSG_DAMAGE_RESULT | 战斗伤害结果 | **新增** |
| 0x2104 | Game→Gate | MSG_DROP_NOTIF | 掉落通知 | **新增** |
| 0x2105 | Game→Gate | MSG_NPC_DIALOG | NPC 对话 | **新增** |
| 0x2106 | Game→Gate | MSG_CHAT_MSG | 聊天消息 | **新增** |
| 0x2110–0x21FF | Game→Gate | — | 响应扩展 | 保留 |
| 0x3000 | Game→Master | MSG_HEARTBEAT | 心跳 | 已有 |
| 0x3001 | Master→Game | MSG_GM_COMMAND | GM 命令 | 已有 |
| 0x3100–0x31FF | Game↔Master | — | 服务注册/注销 | 已有 |
| 0x3300 | Gate→Game | MSG_PLAYER_DISCONNECT | 玩家断连 | 已有 |
| 0x3400–0x34FF | Game↔World | — | 跨服场景 BEGIN/END | 已有 |
| 0x4000 | World→Game | MSG_WORLD_CHAT_BROADCAST | 世界频道广播 | **新增** |
| 0x4001 | World→Game | MSG_GUILD_CHAT_BROADCAST | 行会频道广播 | **新增** |
| 0x4002 | World→Game | MSG_SYSTEM_ANNOUNCE | 系统公告 | **新增** |
| 0x4010–0x40FF | World→Game | — | 世界频道扩展 | 保留 |

---

## 五、关键数据结构详述

### 5.1 ItemSlot（背包格子）

```cpp
struct ItemSlot {
    int32_t item_id = 0;  // 物品 ID（0 表示空）
    int32_t count   = 0;  // 数量（装备固定为1，消耗品可堆叠）
    int32_t dur     = 0;  // 装备耐久度（0 表示非装备或无耐久）
};
```

**物品分类**：
| 类型 | item_id 范围 | 说明 |
|------|-------------|------|
| 武器 | 1000–1099 | 长剑、弯刀、铁扇等 |
| 盔甲 | 1100–1199 | 布衣、轻甲、重甲等 |
| 首饰 | 1200–1299 | 手镯、戒指、项链 |
| 鞋子 | 1300–1399 | 皮皮鞋、加速鞋等 |
| 药品 | 2000–2099 | 金创药、魔法药等 |
| 材料 | 3000–3099 | 矿石、皮革等 |
| 任务 | 4000–4099 | 任务道具（绑定） |

### 5.2 EquipmentSlot（装备槽位）

```cpp
enum class EquipSlot : int {
    WEAPON   = 0,  // 武器
    HELM     = 1,  // 头盔
    CHAIN    = 2,  // 项链
    ARMOR    = 3,  // 盔甲
    SHIELD   = 4,  // 盾牌
    GLOVES   = 5,  // 手套
    BOOTS    = 6,  // 鞋子
    RING_L   = 7,  // 左手戒指
    RING_R   = 8,  // 右手戒指
    BRACELET = 9,  // 手镯
};

struct EquipmentSlot {
    int32_t item_id = 0;  // 装备 ID
    int32_t dur     = 0;  // 当前耐久度
};
```

### 5.3 SkillDef（技能定义）

```cpp
struct SkillDef {
    int32_t skill_id;
    std::string name;
    int32_t  class_required;      // 所需职业
    int32_t  level_required;      // 解锁等级
    int32_t  mp_cost;             // MP 消耗
    int32_t  cooldown_ms;         // 冷却时间（毫秒）
    int32_t  range;               // 施法范围
    int32_t  damage_base;         // 基础伤害
    int32_t  damage_scaling;      // 伤害缩放系数
};
```

### 5.4 PlayerPos（玩家位置）

```cpp
struct PlayerPos {
    int32_t map_id = 0;
    int32_t x      = 0;
    int32_t y      = 0;
};
```

**传奇经典地图坐标范围**：
| 地图 | mapId | 宽度 | 高度 | 出生点 |
|------|-------|------|------|--------|
| 比奇省城 | 1 | 1024 | 768 | (350, 330) |
| 盟重省城 | 2 | 1024 | 768 | (1550, 1450) |
| 沃玛森林 | 3 | 1024 | 768 | (512, 384) |
| 祖玛寺庙 | 4 | 1024 | 768 | (512, 384) |
| 沙巴克城 | 7 | 1024 | 768 | (512, 384) |

---

## 六、各模块伪代码实现

### 6.1 GameService 主循环

```cpp
void GameService::Start(uint16_t rpcPort, const std::string& name) {
    service_name_ = name;
    running_.store(true);

    // 初始化子模块
    InitMaps();
    npc_mgr_  = std::make_unique<NPCManager>();
    monster_mgr_ = std::make_unique<MonsterManager>();
    combat_sys_ = std::make_unique<CombatSystem>(map_mgr_);
    drop_mgr_ = std::make_unique<DropManager>();
    guild_mgr_ = std::make_unique<GuildManager>();
    world_channel_ = std::make_unique<WorldChannel>();

    npc_mgr_->LoadConfigs("docs/data/npc_config.json");
    monster_mgr_->LoadConfig("docs/data/monster_config.json");
    drop_mgr_->LoadDropTables("docs/data/drop_tables.json");

    // 注册 RPC 处理器
    rpc_server_ = std::make_shared<rpc::RpcServer>(rpcPort);
    rpc_server_->Register(kMsgGameFromGate,
        [this](conn, session, msg, respond) { OnFromGate_(conn, session, msg, respond); });

    // 启动心跳线程
    heartbeat_thread_ = std::thread(&GameService::HeartbeatLoop_, this);

    // 启动主循环（驱动怪物 AI 和掉落清理）
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        long long dt = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_tick_).count();
        last_tick_ = now;

        // 更新所有怪物 AI
        monster_mgr_->Tick(dt);
        // 清理过期掉落物
        drop_mgr_->PruneExpired(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
        // 更新所有玩家（技能冷却等）
        std::lock_guard<std::mutex> lk(players_mtx_);
        for (auto& [pid, player] : players_) {
            player->Tick(dt);
        }
    }

    rpc_server_->Stop();
    heartbeat_thread_.join();
}
```

### 6.2 PlayerActor::OnMessage 消息分发

```cpp
void PlayerActor::OnMessage(int32_t msgId, const char* payload, size_t len) {
    switch (msgId) {
        case 0x1100: { // MSG_MOVE
            // payload: direction(int8), x(int32), y(int32), target_map(int32)
            auto* p = reinterpret_cast<const int8_t*>(payload);
            int8_t dir   = *p;
            int32_t tx   = *reinterpret_cast<const int32_t*>(p + 1);
            int32_t ty   = *reinterpret_cast<const int32_t*>(p + 5);
            int32_t tmap = *reinterpret_cast<const int32_t*>(p + 9);
            // 委托给 GameService
            // ... (由 GameService::HandleMove 处理)
            break;
        }
        case 0x1101: { // MSG_ATTACK
            int32_t targetId = *reinterpret_cast<const int32_t*>(payload);
            int32_t skillId  = *reinterpret_cast<const int32_t*>(payload + 4);
            // 委托给 GameService::HandleAttack
            break;
        }
        case 0x1102: { // MSG_NPC_INTERACT
            int32_t npcId  = *reinterpret_cast<const int32_t*>(payload);
            int32_t action = *reinterpret_cast<const int32_t*>(payload + 4);
            int32_t itemId = *reinterpret_cast<const int32_t*>(payload + 8);
            int32_t count  = *reinterpret_cast<const int32_t*>(payload + 12);
            // 委托给 GameService::HandleNpcInteract
            break;
        }
        case 0x1103: { // MSG_PICKUP
            int64_t dropId = *reinterpret_cast<const int64_t*>(payload);
            // 委托给 GameService::HandlePickup
            break;
        }
        case 0x1200: { // MSG_WORLD_CHAT
            // payload: chat_type(int8), message(string)
            // 委托给 GameService::HandleChat
            break;
        }
        default:
            LOG_WARN("Unknown message %d from player %d", msgId, id_);
            break;
    }
}
```

### 6.3 CombatSystem::CalcPlayerAttack 伤害计算

```cpp
DamageResult CombatSystem::CalcPlayerAttack(PlayerActor& attacker,
                                             ActiveMonster& monster,
                                             int32_t skillId) {
    DamageResult result{};
    result.target_id = monster.monster_id;

    // 检查是否在攻击范围内
    PlayerPos atk_pos = {monster.map_id, monster.x, monster.y};
    PlayerPos def_pos = attacker.GetPos();
    if (!InAttackRange(atk_pos, def_pos, 1)) {  // 近战范围=1
        result.is_miss = true;
        return result;
    }

    // 检查攻击冷却
    auto now = std::chrono::steady_clock::now();
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - attacker.last_attack_ts_).count();
    if (elapsed < 500) {  // 0.5秒攻速
        result.is_miss = true;
        return result;
    }
    attacker.last_attack_ts_ = now;

    // 计算伤害
    BaseStats atk_stats = attacker.GetTotalStats();
    int32_t raw_atk = skillId > 0 ? atk_stats.magic : atk_stats.attack;
    int32_t def = monster.defense;  // 怪物防御（简化）

    result.damage = CalcDamage_(raw_atk, def);
    result.is_crit = IsCrit_(raw_atk, def);
    if (result.is_crit) result.damage = (int32_t)(result.damage * 1.5);

    // 更新怪物 HP
    monster.hp -= result.damage;
    result.is_dead = (monster.hp <= 0);

    return result;
}

// 核心伤害公式
int32_t CombatSystem::CalcDamage_(int32_t atk, int32_t def) {
    if (def <= 0) return atk;
    return std::max(1, atk * 100 / (100 + def));
}

bool CombatSystem::IsCrit_(int32_t atk, int32_t def) {
    (void)atk; (void)def;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_) < 0.05f;  // 5% 暴击
}

bool CombatSystem::IsHit_(int32_t atk, int32_t def) {
    float hit_chance = std::max(0.1f, 1.0f - (float)(def - atk) * 0.002f);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_) < hit_chance;
}
```

### 6.4 MonsterManager::UpdateState_ 状态机更新

```cpp
void MonsterManager::UpdateState_(ActiveMonster& m, long long elapsedMs) {
    switch (m.state) {
        case MonsterState::IDLE:
            // 待机：随机 chance 进入巡逻
            if (std::rand() % 100 < 30) {  // 30% 概率
                m.state = MonsterState::PATROL;
            }
            break;

        case MonsterState::PATROL:
            UpdatePatrol_(m, elapsedMs);
            // 检测视野内玩家
            for (auto& [pid, player] : /* 遍历地图玩家 */) {
                if (IsPlayerInVision_(m, pid, player->GetPos())) {
                    m.state = MonsterState::CHASE;
                    m.chase_target_id = pid;
                    break;
                }
            }
            break;

        case MonsterState::CHASE:
            UpdateChase_(m, elapsedMs);
            if (m.chase_target_id == 0) {
                m.state = MonsterState::PATROL;
            }
            break;

        case MonsterState::ATTACK:
            UpdateAttack_(m, elapsedMs);
            // 目标死亡或超出范围
            if (/* 目标不在范围内 */) {
                m.state = MonsterState::CHASE;
            }
            break;

        case MonsterState::RETURN:
            UpdateReturn_(m, elapsedMs);
            if (/* 已回到起点且 HP > 30% */) {
                m.state = MonsterState::IDLE;
            }
            break;

        case MonsterState::DEAD: {
            auto now = std::chrono::steady_clock::now();
            if (now >= m.respawn_ts) {
                m.hp = m.max_hp;
                m.state = MonsterState::IDLE;
                m.patrol_index = 0;
            }
            break;
        }
    }
}
```

---

## 七、单元测试规格

### 7.1 PlayerActor 测试

**文件**：`tests/test_player_actor.cpp`

| 测试用例 | 输入 | 预期输出 |
|----------|------|----------|
| test_init_warrior_stats | 创建战士角色 | level=1, max_hp=150, attack=15, defense=10 |
| test_init_magnsian_stats | 创建法师角色 | level=1, max_hp=100, magic=20 |
| test_init_taoist_stats | 创建道士角色 | level=1, max_hp=130, attack=10, magic=10 |
| test_equip_weapon | 装备攻击+5 的剑 | GetTotalStats().attack = 20 |
| test_bag_full | 背包已满时添加物品 | 返回 false |
| test_use_health_potion | HP=50, 使用金创药(+30) | current_hp_ = 80 |
| test_pk_red_name_normal | 在安全区杀死玩家 | red_name_val 不变 |
| test_pk_red_name_outside | 在非安全区杀死玩家 | red_name_val += 50 |
| test_guild_join | 加入行会 guild_id=5 | guild_id_=5, guild_rank_=3 |
| test_level_up | exp 达到升级阈值 | level_+1, max_hp 增加 |

### 7.2 NPCManager 测试

**文件**：`tests/test_npc_manager.cpp`

| 测试用例 | 输入 | 预期输出 |
|----------|------|----------|
| test_add_npc | 添加商店 NPC map=1, x=100, y=100 | FindNpcAt(100,100) 返回正确指针 |
| test_find_nearby_npc | 查找坐标 (101,101) 附近 NPC | 能找到一个距离≤2 的 NPC |
| test_buy_item_enough_gold | 金币=1000, 购买价格 500 物品 | 金币变 500，背包增加物品 |
| test_buy_item_insufficient_gold | 金币=100, 购买价格 500 物品 | 返回 false，金币不变 |
| test_sell_item | 出售背包中物品 | 金币增加，背包清空 |
| test_teleport_npc | 传送 NPC，目标地图=2, x=200, y=200 | 玩家坐标更新 |
| test_quest_accept | 接取任务（无道具需求） | 任务状态变为 ACCEPTED |

### 7.3 MonsterManager 测试

**文件**：`tests/test_monster_manager.cpp`

| 测试用例 | 输入 | 预期输出 |
|----------|------|----------|
| test_spawn_monsters | 地图 3 有 5 个蜈蚣配置 | SpawnMonsters(3) 后 GetMonstersInMap 返回 5 个 |
| test_monster_patrol | 怪物在 PATROL 状态，dt=1000ms | 移动到下一个巡逻点 |
| test_monster_chase | 玩家进入视野范围 | 状态转为 CHASE，chase_target_id 设置 |
| test_monster_attack | 玩家在攻击范围内 | 对玩家造成伤害 |
| test_monster_dead_respawn | 怪物 HP=0，30s 后 Tick | 状态回到 IDLE，HP 恢复满值 |
| test_despawn_all | DespawnAll(mapId=3) | 地图 3 无活跃怪物 |

### 7.4 CombatSystem 测试

**文件**：`tests/test_combat_system.cpp`

| 测试用例 | 输入 | 预期输出 |
|----------|------|----------|
| test_damage_formula_basic | atk=50, def=20 | damage = 41 |
| test_damage_formula_high_def | atk=50, def=200 | damage = 16 |
| test_damage_minimum | atk=10, def=999 | damage = 1（保底） |
| test_crit_hit | 触发暴击 | damage *= 1.5，is_crit=true |
| test_miss_hit | 防御远高于攻击 | is_miss=true，damage=0 |
| test_in_attack_range | 距离=1 | true |
| test_out_of_attack_range | 距离=3 | false |
| test_is_safe_zone | 比奇城 (map=1) | true |
| test_not_safe_zone | 沃玛森林 (map=3) | false |
| test_on_player_kill_red_name | 非安全区杀玩家 | red_name_val += 50 |
| test_on_player_kill_safe | 安全区杀玩家 | red_name_val 不变 |

### 7.5 DropManager 测试

**文件**：`tests/test_drop_manager.cpp`

| 测试用例 | 输入 | 预期输出 |
|----------|------|----------|
| test_generate_drops | 击杀蜈蚣，drop_rate=0.5 | 生成 1-2 个掉落物 |
| test_pickup_success | 背包有空间 | 掉落物消失，背包增加物品 |
| test_pickup_bag_full | 背包已满 | 返回 false，掉落物保留 |
| test_pickup_wrong_owner | 非击杀者拾取 | 返回 false |
| test_prune_expired | 过期掉落物 | 过期物被移除 |
| test_get_drops_near | 坐标 (100,100) 附近有掉落 | 返回正确的掉落物列表 |

### 7.6 GuildManager 测试

**文件**：`tests/test_guild_manager.cpp`

| 测试用例 | 输入 | 预期输出 |
|----------|------|----------|
| test_create_guild | leaderId=1, name="传奇盟" | guildId=1，返回 true |
| test_request_join | playerId=2, guildId=1 | 加入请求成功 |
| test_approve_join | approve=true | 玩家 guild_id_=1, rank=3 |
| test_kick_member | leader 踢出成员 | 成员 guild_id_ 清零 |
| test_warehouse_deposit | 存入物品 | 仓库物品增加 |
| test_warehouse_withdraw | 取出物品 | 仓库物品减少，背包增加 |
| test_cannot_create_dup_name | 同名行会 | 返回 false |

---

## 八、实施检查清单

### 阶段一：基础框架增强（预计 1-2 天）

- [ ] 增强 `PlayerActor.h`：添加职业、背包、装备、技能、PK、行会字段
- [ ] 增强 `GameService.h`：添加子模块成员变量
- [ ] 增强 `MapManager.h`：添加 is_safe_zone、is_dungeon、min_level 字段
- [ ] 创建 `PlayerActor.cpp`：实现 InitBaseStats、GetTotalStats
- [ ] 创建 `GameService.cpp` 增强：集成 NPCManager/MonsterManager/DropManager 初始化

### 阶段二：核心战斗系统（预计 2-3 天）

- [ ] 创建 `CombatSystem.h/.cpp`：实现伤害公式、暴击、miss、安全区判断
- [ ] 创建 `MonsterManager.h/.cpp`：实现 AI 状态机、怪物配置加载
- [ ] 创建 `DropManager.h/.cpp`：实现掉落表、拾取、过期清理
- [ ] `GameService::HandleAttack`：整合 CombatSystem 和 MonsterManager
- [ ] `GameService::Tick`：驱动怪物 AI 和掉落清理
- [ ] 编写 `test_combat_system.cpp`
- [ ] 编写 `test_monster_manager.cpp`
- [ ] 编写 `test_drop_manager.cpp`

### 阶段三：NPC 与任务系统（预计 1-2 天）

- [ ] 创建 `NPCManager.h/.cpp`：实现 NPC 交互、商店买卖
- [ ] 创建 `docs/data/npc_config.json`：至少包含比奇城 5 个 NPC
- [ ] 创建 `docs/data/monster_config.json`：蜈蚣/蛇/沃玛卫士配置
- [ ] 创建 `docs/data/drop_tables.json`：怪物掉落表
- [ ] `GameService::HandleNpcInteract`：整合 NPCManager
- [ ] `GameService::HandleMove`：整合 MapManager 移动逻辑
- [ ] 编写 `test_npc_manager.cpp`

### 阶段四：社交系统（预计 2-3 天）

- [ ] 创建 `GuildManager.h/.cpp`：实现行会 CRUD、仓库操作
- [ ] 创建 `WorldChannel.h/.cpp`：实现世界/行会/私信广播
- [ ] 增强 `WorldService.h`：集成 WorldChannel 和 GuildManager
- [ ] `GameService::HandleChat`：整合 WorldChannel
- [ ] `GameService::HandlePickup`：整合 DropManager
- [ ] 编写 `test_guild_manager.cpp`

### 阶段五：PK 与红名系统（预计 1-2 天）

- [ ] `CombatSystem::OnPlayerKill`：实现红名值计算
- [ ] `CombatSystem::IsSafeZone`：安全区判断
- [ ] `GameService::HandleAttack`：PK 逻辑（安全区禁止攻击玩家）
- [ ] 红名惩罚逻辑（降低属性、禁止进入某些地图）
- [ ] 编写 `test_player_actor.cpp` 中的 PK 相关用例

### 阶段六：集成测试与示例（预计 1-2 天）

- [ ] 增强 `example_03_game.cpp`：演示完整战斗循环
- [ ] 增强 `example_06_mmorpg.cpp`：多进程全链路演示
- [ ] 验证：登录 → 进入地图 → 打怪 → 掉落 → 拾取 → 聊天 → 行会
- [ ] 所有单元测试通过
- [ ] 更新 README 和教程文档

---

## 九、附录：传奇经典数据参考

### A. 职业基础属性（等级 1）

| 职业 | HP | MP | 攻击 | 魔法 | 防御 | 魔法防 |
|------|-----|-----|------|------|------|--------|
| 战士 | 150 | 0 | 15 | 0 | 10 | 0 |
| 法师 | 100 | 50 | 5 | 20 | 5 | 5 |
| 道士 | 130 | 30 | 10 | 10 | 8 | 3 |

**每级属性成长**：
- 战士：+8HP, +2攻击, +1防御
- 法师：+4HP, +3魔法, +1魔防
- 道士：+6HP, +1攻击, +1魔法, +1防御

### B. 经典地图 ID 参考

| mapId | 地图名 | is_safe_zone | min_level | 说明 |
|-------|--------|--------------|-----------|------|
| 1 | 比奇省城 | true | 1 | 新手出生地 |
| 2 | 盟重省城 | true | 10 | 高级城镇 |
| 3 | 沃玛森林 | false | 1 | 新手练级 |
| 4 | 祖玛寺庙 | false | 15 | 中级练级 |
| 5 | 赤月峡谷 | false | 30 | 高级练级 |
| 6 | 魔龙城 | false | 40 | 顶级练级 |
| 7 | 沙巴克城 | true | 20 | 行会主城 |
| 8 | 石头城 | true | 1 | 新手村 |
| 9 | 僵尸洞 | false | 10 | 经验副本 |
| 10 | 蛆虫洞 | false | 20 | 中级副本 |

### C. 经典怪物 ID 参考

| monsterId | 名称 | HP | 攻击 | 防御 | 速度 | 掉落 |
|-----------|------|-----|------|------|------|------|
| 1 | 蜈蚣 | 80 | 12 | 5 | 2 | 白剑、铁锭 |
| 2 | 蛇 | 60 | 10 | 3 | 3 | 蛇鳞、金创药 |
| 3 | 沃玛卫士 | 200 | 25 | 15 | 1 | 沃玛号角、黑铁 |
| 4 | 祖玛战士 | 350 | 40 | 25 | 2 | 祖玛装备 |
| 5 | 触龙神 | 2000 | 80 | 50 | 1 | 屠龙刀（稀有） |
| 6 | 白野猪 | 1500 | 60 | 40 | 3 | 裁决之杖（稀有） |

### D. 经典装备 ID 参考

| itemId | 名称 | 类型 | 攻击 | 防御 | 说明 |
|--------|------|------|------|------|------|
| 1001 | 木剑 | 武器 | 1-3 | - | 新手武器 |
| 1002 | 铁剑 | 武器 | 3-7 | - | 基础武器 |
| 1003 | 长刀 | 武器 | 5-10 | - | 中级武器 |
| 1100 | 布衣 | 盔甲 | - | 0-2 | 新手防具 |
| 1101 | 轻盔甲 | 盔甲 | - | 2-5 | 基础防具 |
| 1102 | 重盔甲 | 盔甲 | - | 5-10 | 高级防具 |
| 1200 | 铜戒指 | 戒指 | +1攻 | +1防 | 低级首饰 |
| 2001 | 金创药(小) | 药品 | - | - | 恢复 30HP |
| 2002 | 金创药(中) | 药品 | - | - | 恢复 60HP |
| 2003 | 魔法药(小) | 药品 | - | - | 恢复 20MP |

---

*文档结束*
