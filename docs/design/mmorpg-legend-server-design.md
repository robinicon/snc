# SNC MMORPG 传奇版本 — 服务器整体设计方案

> **版本**：v1.0
> **日期**：2026-10-13
> **基于**：传奇 (Legend of Mir) 经典 MMORPG 设计
> **框架**：snc C++17 游戏服务器网络框架

---

## 一、传奇版本核心玩法回顾

在设计方案之前，先明确传奇版本的核心系统：

### 1.1 基础玩法
| 系统 | 说明 |
|------|------|
| **职业系统** | 战士、法师、道士三种职业，各有独特技能 |
| **地图系统** | 多张地图（比奇、盟重、石头城、沃玛森林、祖玛寺庙等），含普通地图、副本地图 |
| **怪物系统** | 普通怪、精英怪、BOSS，分布在各个地图 |
| **战斗系统** | 近身（战士）、远程（法师）、召唤+毒（道士） |
| **装备系统** | 武器、盔甲、首饰、鞋子、戒指，含品质/强化 |
| **背包系统** | 玩家背包 + 仓库，物品格子管理 |
| **技能系统** | 主动技能 + 被动技能，升级获取 |
| **行会系统** | 玩家组建行会，行会战，行会仓库 |
| **PK 系统** | 红名机制，安全区不可 PK |
| **交易/拍卖** | 玩家间交易，摆摊，拍卖行 |

### 1.2 关键架构特征（传奇特有）
- **地图分服**：同一张地图有多实例（不同区服），跨服有世界频道
- **NPC 交互**：商店购买/出售、任务接取、传送服务
- **挂机自动战斗**：玩家选中怪物区域自动寻路攻击
- **掉落机制**：怪物死亡后随机掉落物品到地图，其他玩家可拾取
- **安全区**：主城有 SAFE_ZONE，出安全区即变为 PK 状态
- **红名惩罚**：杀死其他玩家获得红名值，红名到一定阈值有惩罚

---

## 二、整体服务架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                           客户端                                     │
│                    TCP / WebSocket 接入                              │
└────────────────────────┬────────────────────────────────────────────┘
                         │ TCP/WS
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        Gate 服 (网关服)                               │
│  ┌───────────┐  ┌──────────────┐  ┌──────────────────────────┐    │
│  │ TCP/WS接入  │  │ JWT本地验签   │  │ fd → player_handle 映射   │    │
│  │ 背压+心跳   │  │ (HS256)      │  │ 消息路由到 Game 服        │    │
│  └───────────┘  └──────────────┘  └──────────────────────────┘    │
│  端口: TCP 22000 / WS 22001                                         │
└──────────┬────────────────────────────┬────────────────────────────┘
           │ RPC 0x2000                │ RPC 心跳 (每5s)
           ▼                            ▼
┌───────────────────────┐   ┌──────────────────────────────────────┐
│   Game 服 #1 (RPC 23000)│   │           Master 服 (RPC 20000)       │
│                       │   │                                       │
│  ┌─────────────────┐  │   │  ┌─────────────┐  ┌──────────────┐  │
│  │ PlayerActorMgr  │  │   │  │ ServiceRegistry│ │ GM命令分发   │  │
│  │ (所有在线玩家)   │  │   │  │ (心跳统计)    │  │ HTTP 9001   │  │
│  ├─────────────────┤  │   │  └─────────────┘  └──────────────┘  │
│  │ MapManager      │  │   │                                       │
│  │ (多地图实例管理) │  │   │  GET /api/master/services             │
│  ├─────────────────┤  │   │  POST /api/master/gm                  │
│  │ NPCManager      │  │   │                                       │
│  │ (NPC 交互/商店)  │  │   └──────────────────────────────────────┘
│  ├─────────────────┤  │                ▲
│  │ MonsterMgr      │  │                │ RPC 心跳
│  │ (怪物AI/刷新)    │  │                │
│  ├─────────────────┤  │   ┌──────────────────────────────────────┐
│  │ DropManager     │  │   │          World 服 (RPC 25000)         │
│  │ (掉落/拾取)      │  │   │                                       │
│  ├─────────────────┤  │   │  ┌─────────────┐  ┌──────────────┐   │
│  │ CombatSystem    │  │   │  │ 跨服场景锁   │  │ 世界频道广播  │   │
│  │ (战斗计算)       │  │   │  └─────────────┘  └──────────────┘   │
│  └─────────────────┘  │   │                                       │
└───────────────────────┘   └──────────────────────────────────────┘
           │ RPC                     │
           └─────────────────────────┘  RPC 0x3400 跨服场景

           │
           ▼ HTTP
┌─────────────────────────────────────────────────────────────────────┐
│                      Account 服 (HTTP 18080)                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐  │
│  │ 注册 / 登录   │  │ JWT 签发      │  │ StubDatabase (内存)       │  │
│  │ POST /api/auth│  │ HS256        │  │ (开发阶段)                 │  │
│  └──────────────┘  └──────────────┘  └──────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 三、服务职责划分（传奇版增强）

### 3.1 Gate 服（网关服）— 无变化
维持现有职责：TCP/WS 接入、JWT 本地验签、消息路由、fd 映射管理。

**新增职责**：
- 维护玩家全局在线列表（fd → playerId 映射）
- 处理跨服世界频道消息广播

### 3.2 Game 服（游戏服）— 大幅增强

Game 服从"单纯 Actor 管理"升级为完整的单机游戏引擎：

```
GameService
├── PlayerActorMgr        ← 现有，增强
│   ├── 玩家登录/登出
│   ├── 玩家状态管理（HP/MP/经验/金币/等级）
│   ├── 背包管理
│   ├── 装备管理
│   └── 技能管理
├── MapManager            ← 现有，增强
│   ├── 地图实例管理（比奇、盟重、沃玛森林...）
│   ├── 地图内玩家坐标管理
│   ├── 地图内广播
│   └── 地图传送门
├── NPCManager            ← 新增
│   ├── NPC 列表（按地图管理）
│   ├── NPC 对话/任务系统
│   └── NPC 商店（买卖装备/药品）
├── MonsterManager        ← 新增
│   ├── 怪物 spawn 配置（按地图+区域）
│   ├── 怪物 AI（巡逻/追击/回血）
│   ├── 怪物复活计时器
│   └── 掉落表查询
└── CombatSystem          ← 新增
    ├── 伤害计算（武器伤害 + 防御减免）
    ├── 技能施放
    ├── 红名值管理
    └── 安全区判断
```

### 3.3 World 服（世界服）— 大幅增强

```
WorldService
├── SceneLockManager      ← 现有，增强
│   ├── 跨服战场（行会战/攻城战）
│   ├── 副本（祖玛寺庙/毒蛇山谷等）
│   └── 限时活动（血祭/BOSS刷新）
├── WorldChannel          ← 新增
│   ├── 世界频道消息广播（全服）
│   ├── 行会频道
│   └── 私人聊天（跨服限制为同 Game 服）
└── TradeSystem           ← 新增
    ├── 玩家间交易发起/确认
    ├── 拍卖行（物品上架/竞价/成交）
    └── 摆摊系统
```

### 3.4 Master 服 — 小幅增强
维持现有职责，新增：
- 行会信息缓存（行会名→成员列表）
- 全服公告管理

### 3.5 Account 服 — 无变化
维持现有 HTTP 登录/JWT 签发。

---

## 四、通信协议设计（传奇版）

### 4.1 消息 ID 分配

| MsgId 范围 | 方向 | 用途 |
|------------|------|------|
| `0x1000` | Client → Gate | 登录握手（MSG_AUTH） |
| `0x1001` | Gate → Client | 登录结果（MSG_AUTH_RSP） |
| `0x1100–0x11FF` | Client → Gate | 移动/动作（MOVE/ATTACK/INTERACT） |
| `0x1200–0x12FF` | Client → Gate | 聊天/系统消息（CHAT/SYSTEM） |
| `0x2000–0x20FF` | Gate → Game | 业务消息转发（所有 0x1xxx 透传） |
| `0x2100–0x21FF` | Game → Gate | 响应消息（所有 0x1xxx 透传反向） |
| `0x3000` | Game → Master | 心跳 |
| `0x3001` | Master → Game | GM 命令 |
| `0x3100–0x31FF` | Game ↔ Master | 服务注册/注销 |
| `0x3300` | Gate → Game | 玩家断连通知 |
| `0x3400–0x34FF` | Game ↔ World | 跨服场景 BEGIN/END |
| `0x4000–0x40FF` | World → Game | 世界频道广播/行会频道 |

### 4.2 关键消息定义

#### 移动消息（0x1100）
```
MSG_MOVE (0x1100):
  direction: int8     // 0=停止, 1=上, 2=下, 3=左, 4=右, 5-8=斜向
  target_x:  int32
  target_y:  int32
  target_map: int32   // 0=同地图传送
```

#### 战斗消息（0x1101）
```
MSG_ATTACK (0x1101):
  target_id:  int32   // 怪物ID或玩家ID
  skill_id:   int32   // 0=普通攻击，>0=技能ID
  pos_x:      int32
  pos_y:      int32
```

#### NPC 交互消息（0x1102）
```
MSG_NPC_INTERACT (0x1102):
  npc_id:  int32
  action:  int32      // 0=对话, 1=购买, 2=出售, 3=传送
  item_id: int32      // 购买/出售时携带
  count:   int32
```

#### 世界频道消息（0x1200）
```
MSG_WORLD_CHAT (0x1200):
  message:  string
  chat_type: int8     // 0=世界, 1=行会, 2=私人(附带target_id)
```

#### 背包更新（0x2100）
```
MSG_INV_UPDATE (0x2100):
  item_id:    int32
  count:      int32
  slot:       int32   // -1=新增，>=0=替换
  operation:  int8    // 0=add, 1=remove, 2=update
```

---

## 五、核心系统设计

### 5.1 职业与属性系统

```cpp
enum class ClassType : int {
    WARRIOR = 0,  // 战士：高HP高防，近战物理攻击
    MAGICIAN = 1, // 法师：低HP高攻，远程魔法攻击
    TAOIST = 2,   // 道士：中等HP，召唤兽+毒+治愈
};

struct BaseStats {
    int32_t max_hp;    // 根据职业和等级决定
    int32_t max_mp;    // 法师/道士有MP
    int32_t attack;    // 物理攻击
    int32_t magic;     // 魔法攻击
    int32_t defense;   // 物理防御
    int32_t mag_def;   // 魔法防御
};

// 传奇特色：等级驱动属性成长
// 战士：每级 +8HP +2攻击；法师：每级 +4HP +3魔法；道士：每级 +6HP +1攻击 +1魔法
```

### 5.2 地图系统设计

传奇经典地图映射到 MapManager：

```cpp
// 地图配置（来自 JSON 或配置表）
struct MapConfig {
    int32_t  mapId;          // 地图唯一ID（传奇编号：1=比奇, 2=盟重, 3=沙城...）
    std::string name;        // "比奇城", "沃玛森林", "祖玛寺庙"
    int32_t  width;          // 宽1024格
    int32_t  height;         // 高768格
    bool     is_safe_zone;   // 是否安全区（比奇/盟重主城）
    bool     is_dungeon;     // 是否为副本（有入口限制）
    int32_t  min_level;      // 最低进入等级
    int32_t  max_monster_count; // 怪物数量上限
};

// 传奇地图 ID 参考：
// 1=比奇省城, 2=盟重省城, 3=沃玛森林, 4=祖玛寺庙, 5=赤月峡谷,
// 6=魔龙城, 7=沙巴克城, 8=石头城, 9=僵尸洞, 10=蛆虫洞
```

### 5.3 怪物 AI 系统

```cpp
enum class MonsterState : int {
    IDLE,      // 原地待机
    PATROL,    // 按预设路径巡逻
    CHASE,     // 追击玩家
    ATTACK,    // 攻击目标
    RETURN,    // 受伤后退回到起点
    DEAD,      // 死亡（等待复活）
};

struct MonsterConfig {
    int32_t  monsterId;    // 怪物类型（蜈蚣/蛇/沃玛卫士/祖玛战士/触龙神）
    std::string name;
    int32_t  hp;           // 生命值
    int32_t  attack;       // 攻击力
    int32_t  defense;      // 防御力
    int32_t  speed;        // 移动速度（格子/秒）
    int32_t  attack_range; // 攻击范围（格子）
    int32_t  vision_range; // 视野范围（格子）
    float    drop_rate;    // 物品掉落率
    // 掉落表: drop_table_id → 物品ID + 数量范围 + 概率
};
```

### 5.4 掉落系统

```cpp
struct DropEntry {
    int32_t  item_id;       // 物品ID
    int32_t  min_count;     // 最少数量
    int32_t  max_count;     // 最多数量
    float    probability;   // 掉落概率（0.0~1.0）
};

// 传奇特色：死亡后物品掉落在地上，其他玩家可拾取
// DropManager 管理地图上的"掉落物"实体
struct GroundDrop {
    int64_t  drop_id;       // 唯一标识
    int32_t  map_id;
    int32_t  x, y;          // 掉落坐标
    int32_t  item_id;
    int32_t  count;
    int64_t  owner_id;      // 原拥有者（用于拾取权限）
    int64_t  expire_ts_ms;  // 过期时间（30分钟后消失）
};
```

### 5.5 行会系统

```cpp
struct GuildInfo {
    int32_t  guild_id;
    std::string name;           // 行会名称
    int32_t  leader_id;         // 会长玩家ID
    int32_t  level;             // 行会等级（影响仓库容量等）
    int64_t  create_ts;
    std::unordered_map<int32_t, int32_t> members; // playerId → 职位(0=会长,1=副会,2=成员)
    std::vector<int32_t> war_members;            // 参与攻城战的成员
};

// 行会仓库：跨登录会话持久化（存入 DB）
struct GuildWarehouse {
    std::vector<ItemSlot> items; // 固定格子数量的物品列表
};
```

### 5.6 红名 PK 系统

```cpp
// 红名值：杀死其他玩家增加，在安全区外
// 红名值达到阈值（如 200）触发惩罚
enum class PKStatus : int {
    NORMAL = 0,    // 正常
    RED_NAME   = 1, // 红名（>100红名值）
    VERY_RED   = 2, // 血红名（>200红名值，可被任意玩家攻击）
};

struct PKInfo {
    int32_t  kill_count;    // 杀人次数
    int32_t  red_name_val;  // 红名值
    int64_t  last_kill_ts;  // 上次杀人时间（用于惩罚冷却）
};
```

---

## 六、新增模块设计

### 6.1 新增文件清单

```
include/snc/mmorpg/
├── AccountService.h         ← 现有，无需修改
├── MasterService.h          ← 现有，需增加行会缓存
├── GateService.h            ← 现有，无需修改
├── GameService.h            ← 现有，需增强（见下文）
├── MapManager.h             ← 现有，需增强（支持地图配置）
├── WorldService.h           ← 现有，需增强（世界频道+行会）
├── PlayerManager.h          ← 新增：玩家 Actor 综合管理
├── NPCManager.h             ← 新增：NPC 管理
├── MonsterManager.h         ← 新增：怪物管理
├── CombatSystem.h           ← 新增：战斗计算
├── DropManager.h            ← 新增：掉落/拾取管理
├── GuildManager.h           ← 新增：行会系统
└── WorldChannel.h           ← 新增：世界频道广播

examples/
├── example_01_account.cpp   ← 现有
├── example_02_master.cpp    ← 现有，需增强
├── example_03_game.cpp      ← 现有，需大幅增强
├── example_04_gate.cpp      ← 草稿，标记保留
├── example_05_world.cpp     ← 现有，需增强
└── example_06_mmorpg.cpp    ← 现有，需增强（全链路演示）

docs/tutorials/
├── 20-player-actor.md       ← 新增：玩家 Actor 系统
├── 21-map-design.md         ← 新增：地图系统设计
├── 22-monster-ai.md         ← 新增：怪物 AI 系统
├── 23-combat-system.md      ← 新增：战斗系统
├── 24-guild-system.md       ← 新增：行会系统
└── 25-pk-redname.md         ← 新增：PK与红名系统
```

### 6.2 PlayerActor 增强设计

```cpp
// 扩展现有 PlayerActor
class PlayerActor : public Actor {
public:
    // ── 基础信息 ───────────────────────────────────────────
    int32_t         player_id;      // 数据库 user_id
    std::string     nickname;
    ClassType       class_type;     // 战士/法师/道士
    int32_t         level;          // 等级
    int64_t         exp;            // 当前经验
    int64_t         gold;           // 金币

    // ── 属性（计算得出）──────────────────────────────────────
    BaseStats       base_stats;     // 基础属性
    BaseStats       equip_stats;    // 装备附加属性
    int32_t         current_hp;
    int32_t         current_mp;

    // ── 背包 ───────────────────────────────────────────────
    static constexpr int kBagSlots = 30;
    ItemSlot        bag_[kBagSlots];  // 固定30格背包

    // ── 装备 ───────────────────────────────────────────────
    static constexpr int kEquipSlots = 9;
    EquipmentSlot   equip_[kEquipSlots]; // 武器/盔/链/甲/盾/鞋/戒/项链/手镯

    // ── 技能 ───────────────────────────────────────────────
    std::unordered_map<int32_t, int32_t> skills_; // skillId → skillLevel

    // ── 位置 ───────────────────────────────────────────────
    PlayerPos     pos_;
    PlayerPos     spawn_pos_;      // 出生点（主城传送用）

    // ── PK ───────────────────────────────────────────────
    PKInfo        pk_info_;

    // ── 行会 ───────────────────────────────────────────────
    int32_t       guild_id = 0;
    int32_t       guild_rank = 0;  // 0=非行会, 1=会长, 2=副会, 3=成员

    // ── 战斗状态 ───────────────────────────────────────────
    int32_t       attack_target_id = 0;  // 当前攻击目标
    long long     last_attack_ts = 0;    // 上次攻击时间（攻速控制）
    long long     last_skill_ts  = 0;    // 上次技能时间（冷却控制）
};
```

### 6.3 GameService 增强接口

```cpp
class GameService {
public:
    // ── 玩家管理 ─────────────────────────────────────────────
    PlayerActor::Ptr CreatePlayer(int32_t playerId, const std::string& name,
                                  ClassType classType,
                                  core::IConnection::Ptr conn);
    void RemovePlayer(int32_t playerId);
    PlayerActor::Ptr FindPlayer(int32_t playerId) const;
    PlayerActor::Ptr FindPlayerByFd(int32_t fd) const; // Gate 转发时通过 fd 查找

    // ── 移动 ─────────────────────────────────────────────────
    void HandleMove(PlayerActor::Ptr player, int8_t direction,
                    int32_t targetX, int32_t targetY, int32_t targetMap);

    // ── 战斗 ─────────────────────────────────────────────────
    void HandleAttack(PlayerActor::Ptr player, int32_t targetId,
                      int32_t skillId);

    // ── NPC 交互 ─────────────────────────────────────────────
    void HandleNpcInteract(PlayerActor::Ptr player, int32_t npcId,
                           int32_t action, int32_t itemId, int32_t count);

    // ── 聊天 ─────────────────────────────────────────────────
    void HandleChat(PlayerActor::Ptr player, int8_t chatType,
                    const std::string& message, int32_t targetId);

    // ── 地图传送 ─────────────────────────────────────────────
    void TeleportPlayer(PlayerActor::Ptr player, int32_t targetMapId,
                        int32_t x, int32_t y);

    // ── 物品操作 ─────────────────────────────────────────────
    bool UseItem(PlayerActor::Ptr player, int32_t slot);
    bool PickupItem(PlayerActor::Ptr player, int64_t dropId);

    // ── 现有接口保持不变 ─────────────────────────────────────
    void RegisterWithMaster_();
    void HeartbeatLoop_();
    void GracefulShutdown_();

private:
    // ── 新增子模块 ───────────────────────────────────────────
    std::unique_ptr<NPCManager>         npc_mgr_;
    std::unique_ptr<MonsterManager>     monster_mgr_;
    std::unique_ptr<CombatSystem>       combat_sys_;
    std::unique_ptr<DropManager>        drop_mgr_;

    // ── 现有成员 ─────────────────────────────────────────────
    rpc::RpcClient::Ptr                 master_rpc_;
    discovery::ServiceRegistry::Ptr     registry_;
    auth::JwtConfig                     jwt_cfg_;
    MapManager::Ptr                     map_mgr_;
    std::shared_ptr<rpc::RpcServer>     rpc_server_;

    std::unordered_map<int32_t, PlayerActor::Ptr> players_;
    mutable std::mutex                     players_mtx_;

    std::string                  service_name_;
    std::atomic<bool>            running_{false};
    std::atomic<int32_t>         next_player_id_{1};
    std::thread                  heartbeat_thread_;
};
```

### 6.4 NPCManager 设计

```cpp
#pragma once
#include <unordered_map>
#include <vector>
#include <mutex>

namespace snc { namespace mmorpg {

// NPC 类型
enum class NpcType : int {
    SHOP       = 0,  // 商店（买/卖物品）
    QUEST      = 1,  // 任务 NPC
    TELEPORT   = 2,  // 传送 NPC
    TRAINING   = 3,  // 训练 NPC（技能学习）
};

struct NpcConfig {
    int32_t     npc_id;
    std::string name;
    NpcType     type;
    int32_t     map_id;       // 所在地图
    int32_t     x, y;         // 坐标
    // 商店类型特有字段
    struct ShopData {
        std::vector<std::pair<int32_t, int32_t>> buy_list;   // 出售物品: itemId, price
        std::vector<std::pair<int32_t, int32_t>> sell_list;  // 收购物品: itemId, price
    } shop;
};

class NPCManager {
public:
    void AddNpc(const NpcConfig& cfg);
    NpcConfig* FindNpcAt(int32_t mapId, int32_t x, int32_t y, int range = 2);
    NpcConfig* FindNpcById(int32_t npcId);

    // NPC 交互处理（由 GameService 调用）
    std::string GetDialogue(int32_t npcId, int32_t playerId);
    bool        BuyItem(int32_t npcId, int32_t playerId, int32_t itemId, int32_t count);
    bool        SellItem(int32_t npcId, int32_t playerId, int32_t itemId, int32_t count);
    void        Teleport(int32_t npcId, int32_t playerId, int32_t targetMapId, int32_t x, int32_t y);

private:
    std::mutex mtx_;
    std::vector<NpcConfig> npcs_;
};

}} // namespace
```

### 6.5 MonsterManager 设计

```cpp
#pragma once
#include <unordered_map>
#include <vector>
#include <chrono>

namespace snc { namespace mmorpg {

struct MonsterConfig {
    int32_t  monsterId;
    std::string name;
    int32_t  hp, attack, defense;
    int32_t  speed;        // 格子/秒
    int32_t  attack_range;
    int32_t  vision_range;
    float    drop_rate;
    // 掉落表
    std::vector<std::pair<int32_t, std::pair<int32_t,int32_t>>> drops;
    // int32_t: itemId, pair<int32_t,int32_t>: (minCount, maxCount)
};

struct ActiveMonster {
    int32_t       monster_id;
    int32_t       map_id;
    int32_t       x, y;
    int32_t       hp;
    int32_t       max_hp;
    int32_t       owner_player_id = 0; // 攻击者ID（用于归属权）
    std::chrono::steady_clock::time_point respawn_ts; // 复活时间
    MonsterState  state = MonsterState::IDLE;
};

class MonsterManager {
public:
    void LoadConfig(const std::string& config_path);
    void SpawnMonsters(int32_t mapId);
    void DespawnAll(int32_t mapId);

    // 每帧更新（由 GameService Tick 驱动）
    void Tick(long long elapsedMs);

    // 怪物被攻击
    void OnMonsterHit(int32_t mapId, int32_t monsterLocalId,
                      int32_t attackerId, int32_t damage);

    // 获取地图上的怪物列表
    std::vector<ActiveMonster*> GetMonstersInMap(int32_t mapId);

private:
    std::mutex mtx_;
    std::unordered_map<int32_t, std::vector<MonsterConfig>> monster_configs_;
    // mapId → 该地图上的所有活跃怪物
    std::unordered_map<int32_t, std::vector<std::shared_ptr<ActiveMonster>>> active_monsters_;
};

}} // namespace
```

### 6.6 CombatSystem 设计

```cpp
#pragma once

namespace snc { namespace mmorpg {

struct DamageResult {
    int32_t  target_id;
    int32_t  damage;      // 最终造成的伤害
    bool     is_crit;     // 是否暴击
    bool     is_miss;     // 是否未命中
    bool     is_dead;     // 目标是否死亡
};

class CombatSystem {
public:
    // 计算玩家对目标的伤害
    DamageResult CalcPlayerAttack(PlayerActor& attacker, int32_t targetId,
                                  int32_t skillId, bool isMonster);

    // 计算怪物对玩家的伤害
    DamageResult CalcMonsterAttack(ActiveMonster& monster, PlayerActor& player);

    // 技能施放（返回是否成功）
    bool CastSkill(PlayerActor& player, int32_t skillId);

    // 检查目标是否在攻击范围内
    bool InAttackRange(const PlayerPos& attacker, const PlayerPos& target,
                       int32_t range);

    // 检查目标是否在视野范围内
    bool InVisionRange(const PlayerPos& attacker, const PlayerPos& target,
                       int32_t visionRange);

    // 检查地图坐标是否为安全区
    bool IsSafeZone(int32_t mapId, int32_t x, int32_t y);

    // 更新红名值
    void OnPlayerKill(PlayerActor& killer, PlayerActor& victim);
    int32_t GetRedNameValue(int32_t playerId);

private:
    // 伤害公式：伤害 = max(1, 攻击 * (100/(100+防御)))
    int32_t CalcDamage_(int32_t atk, int32_t def);
    bool    IsCrit_(int32_t atk, int32_t def);
};

}} // namespace
```

### 6.7 DropManager 设计

```cpp
#pragma once

namespace snc { namespace mmorpg {

// 物品结构
struct ItemSlot {
    int32_t item_id = 0;
    int32_t count   = 0;  // 装备类为1，消耗品可为多
    int32_t dur     = 0;  // 装备耐久度
};

class DropManager {
public:
    // 怪物死亡后掉落物品
    std::vector<GroundDrop> GenerateDrops(int32_t mapId, int32_t x, int32_t y,
                                           int32_t monsterId, int32_t killerId);

    // 拾取掉落物
    bool Pickup(int32_t playerId, int64_t dropId);

    // 删除过期掉落物
    void PruneExpired(long long nowMs);

    // 获取地图上某坐标附近的掉落物
    std::vector<GroundDrop*> GetDropsNear(int32_t mapId, int32_t x, int32_t y, int range);

private:
    std::mutex mtx_;
    // 地图上的所有掉落物
    std::unordered_map<int64_t, GroundDrop> drops_;
    int64_t next_drop_id_ = 1;
};

}} // namespace
```

### 6.8 WorldChannel 设计

```cpp
#pragma once

namespace snc { namespace mmorpg {

class WorldChannel {
public:
    // 世界频道消息（广播给所有 Game 服的所有在线玩家）
    void BroadcastWorldChat(int32_t senderId, const std::string& senderName,
                            const std::string& message);

    // 行会频道消息（广播给同 Game 服的所有行会成员）
    void BroadcastGuildChat(int32_t guildId, int32_t senderId,
                            const std::string& senderName, const std::string& message);

    // 私信消息
    void SendPrivateChat(int32_t senderId, int32_t targetId,
                         const std::string& message);

    // 注册消息广播回调（WorldService 持有各 Game 服的 RpcClient）
    using BroadcastFn = std::function<void(int32_t msgId, const char* data, size_t len)>;
    void RegisterBroadcaster(const std::string& gameService, BroadcastFn fn);
    void UnregisterBroadcaster(const std::string& gameService);

private:
    std::mutex mtx_;
    struct Broadcaster {
        std::string service_name;
        BroadcastFn fn;
    };
    std::vector<Broadcaster> broadcasters_;
};

}} // namespace
```

---

## 七、完整时序：玩家登录到进入游戏

```
[阶段 1] 账号登录（Account 服）
  Client ──POST /api/account/login──► Account
  Account ◄──{token, user_id}──────  Client

[阶段 2] 游戏接入（Gate 服）
  Client ──TCP connect─────────────► Gate
  Client ──MSG_AUTH(token)─────────► Gate
  Gate ──[本地 HS256 验签]────────── 通过
  Gate ──RPC MSG_SERVICE_REGISTER─► Master
  Master ◄──{game_addr:port}──────  Gate   (RoundRobin 选择 Game 服)
  Gate ──建立持久 RPC──────────────► Game
  Gate ◄──MSG_AUTH_RSP(游戏服地址)──  Game

[阶段 3] 创建玩家角色（Game 服）
  Game ──MSG_PLAYER_CREATE────────► Client
  Client ──MSG_PLAYER_CONFIRM─────► Game
  Game ──广播进入地图消息─────────► 同地图所有玩家
  Game ──发送地图内所有玩家位置───► Client（自身视角）

[阶段 4] 日常游戏循环
  Client ──MSG_MOVE/ATTACK/CHAT──► Gate ──RPC 0x2000────► Game
  Game 内部处理 ──回复 ──RPC 0x2100────► Gate ──TCP────────► Client

[阶段 5] 怪物战斗循环（Game 服内部）
  GameService.Tick() 每 100ms：
    1. MonsterManager.Tick(dt)
       → 遍历活跃怪物
       → 检测视野范围内是否有玩家
       → 如在视野内且距离≤攻击范围 → 发起攻击
       → 如不在视野内 → 继续巡逻
    2. DropManager.PruneExpired(now)
       → 移除超过 30s 的掉落物

[阶段 6] 服务心跳
  Game 每 5s ──RPC心跳────────────► Master
  Master 仅统计日志，不做超时判死
```

---

## 八、端口分配总览

| 服务 | 协议 | 端口 | 说明 |
|------|------|------|------|
| Account | HTTP | 18080 | 账号注册/登录 |
| Master | RPC | 20000 | 服务注册/心跳 |
| Master | HTTP | 21000 | Admin 面板 / GM 命令 |
| Gate | TCP | 22000 | 客户端 TCP 接入 |
| Gate | WS | 22001 | 客户端 WebSocket 接入 |
| Game #1 | RPC | 23000 | 游戏逻辑（可多实例） |
| Game #2 | RPC | 23001 | 游戏逻辑（可多实例） |
| World | RPC | 25000 | 跨服场景/世界频道 |

---

## 九、实施路线图

### 阶段一：基础框架增强（1-2 天）
- [x] 现有框架已具备：Gate/Game/Master/World/Account 骨架
- [ ] 增强 `GameService`：集成 NPCManager、MonsterManager、DropManager
- [ ] 增强 `PlayerActor`：添加背包、装备、属性系统
- [ ] 增强 `MapManager`：添加地图安全区判断、传送门

### 阶段二：核心战斗系统（2-3 天）
- [ ] `CombatSystem.h`：伤害公式、暴击、miss、技能施放
- [ ] `MonsterManager.h`：怪物 AI（巡逻/追击/攻击）、怪物配置加载
- [ ] `DropManager.h`：掉落表、拾取、过期清理
- [ ] `example_03_game.cpp`：完整演示怪物 AI + 战斗 + 掉落

### 阶段三：NPC 与任务系统（1-2 天）
- [ ] `NPCManager.h`：商店买卖、传送 NPC、对话
- [ ] 怪物配置 JSON 加载（`docs/data/monster_config.json`）
- [ ] 地图配置 JSON 加载（`docs/data/maps.json`）
- [ ] `example_03_game.cpp` 补充 NPC 交互演示

### 阶段四：社交系统（2-3 天）
- [ ] `GuildManager.h`：行会创建/加入/管理/仓库
- [ ] `WorldChannel.h`：世界频道/行会频道/私信
- [ ] 世界频道消息跨 Game 服广播（通过 World 服中转）
- [ ] `WorldService.h` 增强：集成 WorldChannel 和 GuildManager

### 阶段五：PK 与红名系统（1-2 天）
- [ ] 红名值计算（在 CombatSystem 中）
- [ ] 安全区判断（MapManager 中维护每张地图的安全区配置）
- [ ] 红名惩罚逻辑（在特定地图禁止入场等）

### 阶段六：集成测试与示例（1-2 天）
- [ ] `example_06_mmorpg.cpp` 完整多进程演示
  - 启动 Account → Master → Gate → Game → World 五个进程
  - 验证完整登录 → 进入地图 → 打怪 → 拾取 → 聊天 → 行会 流程
- [ ] 单元测试补充（MonsterAI、Drop、Combat、Guild）
- [ ] 更新 README 和教程

---

## 十、与现有框架的能力对照

| 设计需求 | 对应框架能力 | 状态 | 备注 |
|----------|-------------|------|------|
| TCP/WS 接入 + 心跳 + 背压 | `TcpConnection` | ✅ | 已有，零代码 |
| JWT 本地验签 | `auth::JwtService` | ✅ | Gate 复用 |
| 服务注册/发现 | `ServiceRegistry` | ✅ | 进程内开发用 |
| 跨服 RPC + 自动重连 | `RpcClient`（指数退避） | ✅ | Gate→Game 持久连接 |
| 服务监控 | `MonitorService` | ✅ | Master 复用 |
| 地图管理 | `MapManager` | ✅ | 需增强地图配置 |
| 玩家 Actor 管理 | `GameService` | ✅ | 需增强属性/背包 |
| 怪物 AI | — | 🔴 | 需新建 `MonsterManager` |
| 掉落系统 | — | 🔴 | 需新建 `DropManager` |
| NPC 交互 | — | 🔴 | 需新建 `NPCManager` |
| 战斗系统 | — | 🔴 | 需新建 `CombatSystem` |
| 行会系统 | — | 🔴 | 需新建 `GuildManager` |
| 世界频道 | — | 🔴 | 需新建 `WorldChannel` |
| 红名/PK | — | 🔴 | 需增强 `CombatSystem` |
| 安全区判断 | — | 🔴 | 需增强 `MapManager` |
| GM 命令分发 | `MasterService` | ✅ | 已有，需扩展命令 |
| 优雅停机 | `GameService` | ✅ | 已有 SIGTERM 处理 |

---

## 十一、关键技术决策（新增 ADR）

| ADR | 决策 | 理由 |
|-----|------|------|
| ADR-006 | 怪物 AI 在 Game 服 Tick 中统一更新（非独立线程） | Game 服已有多线程架构，怪物 AI 更新频率低（100ms），无需独立线程 |
| ADR-007 | 掉落物实体不跨服（仅在同一 Game 服内） | 简化设计，传奇原版掉落物也只在本地图 |
| ADR-008 | 世界频道消息通过 World 服中转广播 | 解耦 Game 服之间的直接通信，World 服统一转发 |
| ADR-009 | 行会数据暂存内存，登录时从 DB 加载 | 开发阶段简化，生产时加 Redis 持久化 |
| ADR-010 | 红名值仅在内存中，重启清零 | 开发阶段接受；生产版本需持久化 |

---

## 十二、待确认问题

1. **多 Game 服实例**：传奇通常每个区服一个 Game 进程，是否要支持同一世界多个 Game 服实例？（本期仅支持单实例，多实例为后续阶段）
2. **数据存储**：开发阶段使用 StubDatabase，生产阶段是否需要 MySQL 接入？何时切换？
3. **怪物寻路**：传奇使用网格地图，寻路算法（A*）是否纳入本期？
4. **技能系统深度**：本期仅实现基础攻击+1-2个技能，还是完整实现所有职业技能？

---

## 十三、架构重构方案（v2.0）— 实体统一 + 场景化 + Lua 化

> **决策依据**：用户要求 (1) 玩家角色、NPC、怪物角色、宠物均归属 Entity；(2) 核心战斗保留 C++，其余业务走 Lua 绑定；(3) 简化设计，新增 Scene/SceneMgr、Entity/EntityMgr、Item/ItemMgr。本章扩充 v1.0 设计，逐步取代前文 MapManager 中心化的旧架构。

### 13.1 重构动机与审计结论

**审计发现（针对 v1.0 代码）：**

| # | 问题 | 现有代码位置 | 影响 |
|---|------|-------------|------|
| A1 | 实体无统一基类，玩家/怪物/NPC 平行孤立 | PlayerActor.h / MonsterManager.h / NPCManager.h | 无法统一"场景内实体遍历"，GameService 需分别遍历三套容器 |
| A2 | MapManager 只管地图元数据，怪物/NPC/掉落不在实例内 | MapManager.h | 场景概念缺失，实体与地图无归属关系 |
| A3 | Item 模型不统一：背包 ItemSlot / 掉落 GroundDrop 两套结构 | PlayerActor.h / DropManager.h | 拾取时需结构转换，逻辑重复 |
| A4 | 怪物 AI 硬编码 C++ 状态机 | MonsterManager.cpp | 改怪行为需重编译 |
| A5 | 宠物系统缺失（道士"召唤神兽"技能已有定义） | CombatSystem.h (kSkillSummonDog) | 技能点了但无实体 |
| A6 | Lua 绑定框架已就绪但未启用 | libs/snc_rpc_lua（stub 实现） | SNC_LUA_AVAILABLE 宏未开启，需安装 Lua |

**审计结论**：上述问题相互关联，核心是**缺少 Entity 抽象 + 场景归属 + 统一物品模型 + 逻辑可脚本化**。方案合理，按 13.2~13.6 落地。

### 13.2 模块归属总表（C++ 核心 / Lua 业务）

| 模块 | 语言 | 理由 |
|------|------|------|
| `Entity` / `EntityMgr` | **C++** | 高频路由索引，所有实体必经 |
| `Scene` / `SceneMgr` | **C++** | 场景生命周期管理，持有实体集合 + Lua VM 引用 |
| `Item` / `ItemMgr` | **C++** | 背包/装备/地面掉落/仓库统一 CRUD |
| `PlayerActor` | **C++** | 高频访问，RPC 直连序列化 |
| `CombatSystem` | **C++** | 伤害公式每帧多次调用，必须快 |
| `MonsterDef` 数据 struct | **C++** | 定义结构稳定 |
| **MonsterAI 状态机** | **Lua** | 行为规则变化频繁，策划可调 |
| **掉落表 + 掷骰规则** | **Lua** | 掉落配置热更新 |
| **行会业务规则** | **Lua** | 权限/仓库逻辑运营可控 |
| **聊天过滤规则** | **Lua** | 禁词列表等运营可控 |
| **宠物 AI** | **Lua** | 复用 monster_ai.lua |
| NPC 管理（查找/交互） | **C++** | 简单查找，变化少，但**对话脚本走 Lua** |

### 13.3 实体体系（Entity 基类 + 六大派生）

```cpp
// Entity.h
enum class EntityType : int8_t {
    PLAYER = 0, MONSTER = 1, NPC = 2, PET = 3, ITEM = 4, DROP = 5
};

class Entity {
public:
    virtual ~Entity() = default;
    virtual EntityType Type() const = 0;      // 各派生类返回自身类型

    int32_t    EntityId()  const { return entity_id_; }
    int32_t    SceneId()   const { return scene_id_; }
    void       SetSceneId(int32_t id) { scene_id_ = id; }

    PlayerPos& Pos()        { return pos_; }
    const PlayerPos& Pos()  const { return pos_; }
    bool       Alive()      const { return alive_; }
    void       SetAlive(bool a) { alive_ = a; }

    // Lua 钩子：每帧由 SceneMgr 分发，行为实现在 Lua 脚本中
    virtual void OnTick(int64_t nowMs, int32_t dtMs) = 0;

protected:
    int32_t    entity_id_ = 0;
    int32_t    scene_id_  = 0;
    PlayerPos  pos_;
    bool       alive_     = true;
};
```

**六大派生类映射：**

| 派生类 | 继承自 | 替代 | 说明 |
|--------|--------|------|------|
| `PlayerActor` | `Entity` | 现有 PlayerActor | 新增 `PetId`（召唤物）、继承 `Entity` 的位置/场景/存活 |
| `MonsterInstance` | `Entity` | MonsterManager 内部 struct | 从 MonsterManager 中提升为独立类 |
| `NpcInstance` | `Entity` | NPCManager 内部 struct | 持有 `NPCDef`，不死（alive_ 恒 true） |
| `PetInstance` | `Entity` | **新增** | 道士召唤兽，AI 复用 monster_ai.lua，绑定 OwnerPlayerId |
| `Item` | `Entity` | PlayerActor 的 ItemSlot + DropManager 的 GroundDrop | 场景内地面物品也作为实体存在，expire_ts 区分 |
| `DropEntity` | `Entity` | DropManager 的 GroundDrop | 地面掉落实体，30 分钟过期 |

### 13.4 场景体系（Scene + SceneMgr 取代 MapManager）

```cpp
// Scene.h
class Scene : public std::enable_shared_from_this<Scene> {
public:
    using Ptr = std::shared_ptr<Scene>;
    Scene(int32_t sceneId, const SceneConfig& cfg) : config_(cfg) {}

    int32_t    SceneId()  const { return config_.scene_id; }
    const SceneConfig& Cfg() const { return config_; }

    // 实体容器（委托 EntityMgr）
    void AddEntity(Entity::Ptr e);
    void RemoveEntity(int32_t entityId);
    Entity::Ptr FindEntity(int32_t entityId);

    // 区域内查询 & 广播
    std::vector<Entity::Ptr> FindNearby(int32_t x, int32_t y, int32_t range);
    void Broadcast(int32_t msgId, const char* data, size_t len);

    // 每帧：Tick 所有实体 → 调 Lua 逻辑
    void Tick(int64_t nowMs, int32_t dtMs);

private:
    SceneConfig     config_;
    EntityMgr::Ptr  entity_mgr_;   // 本场景所有实体
};

// SceneMgr.h — 取代 MapManager，保留 kMapXXX 常量
class SceneMgr {
public:
    Scene::Ptr CreateScene(const SceneConfig& cfg);
    Scene::Ptr GetScene(int32_t sceneId);
    void       RemoveScene(int32_t sceneId);
    std::vector<Scene::Ptr> AllScenes() const;
    void TickAll(int64_t nowMs, int32_t dtMs);   // 遍历所有场景 → Entity.Tick → Lua
private:
    std::unordered_map<int32_t, Scene::Ptr> scenes_;
};
```

**值保留**：`Maps::kMapBegin/kMapBeij/kMapShabake` 等常量迁移到 `SceneConfig` 的静态工厂（在新 `Maps` 命名空间下，保持引用兼容）。

### 13.5 物品体系（Item + ItemMgr）

```cpp
// Item.h
struct Item {
    int32_t  item_id;      // 物品定义 ID（关联 ItemDef 表）
    int32_t  count;        // 数量（装备类=1）
    int32_t  uid;          // 唯一实例 ID（地面掉落标识，背包内可=0）
    int64_t  expire_ts;    // 0=永久（背包/装备/仓库），>0=过期时间戳（地面掉落）

    bool IsPermanent() const { return expire_ts == 0; }
    bool IsExpired(int64_t nowMs) const { return !IsPermanent() && expire_ts < nowMs; }
};

// ItemMgr.h/cpp — 统一管理所有物品实例
class ItemMgr {
public:
    // 背包 CRUD（PlayerActor 内部持有，委托 ItemMgr 生成 uid）
    int32_t    CreateItem(int32_t itemId, int32_t count);       // 返回 uid
    Item::Ptr  GetItem(int32_t uid) const;
    bool       DeleteItem(int32_t uid);

    // 场景地面掉落
    DropEntity::Ptr SpawnDrop(int32_t sceneId, int32_t x, int32_t y,
                               int32_t itemId, int32_t count);
    int CleanupExpired(int64_t nowMs);

private:
    std::unordered_map<int32_t, Item::Ptr>  items_;     // uid → item
    std::unordered_map<int32_t, DropEntity::Ptr> drops_; // uid → 地面掉落
    std::mutex mtx_;
    int32_t next_uid_ = 1;
};
```

### 13.6 Lua 绑定（基于 snc_rpc_lua 框架，不使用 sol2）

**snc_rpc_lua 已提供的接口（header-only + 条件编译）：**

| 类 | 职责 | 本设计如何使用 |
|----|------|---------------|
| `LuaVmManager` | VM 生命周期、脚本加载、实体推入 | 每 Scene 共享一个 VM 或全局单 VM，`LoadScript("scripts/*.lua")` |
| `EntityExport` | 将 C++ Entity* 包装为 Lua userdata | 实体进入场景时 `PushEntity(ptr)`，Lua 侧拿到实体句柄 |
| `MsgRouterLua` | 消息路由到 Lua 处理器（单线程串行） | 场景事件（玩家进入/怪物死亡/攻击）路由到对应 Lua 函数 |
| `HotUpdateMgr` | 双缓冲热更新 | Phase 4 启用，怪物 AI / 掉落表热更新不重启 |
| `LuaRouterApi` | C++ 注册 Lua 处理器 | C++ 侧注册 `OnMonsterTick`/`OnDropRoll`/`OnGuildOp` 等回调入口 |
| `LuaRpcApi` | Lua 侧发 RPC（`snc.rpc.call`） | 行会/聊天跨服广播从 Lua 发 RPC 到 World 服 |

**C++ → Lua 调用模式：**

```cpp
// LuaBinding.cpp 中封装（mmo 内部新建 LegendLuaBridge）
class LegendLuaBridge {
public:
    // 传入全局 VM，注册游戏侧 API 表 snc.game.*
    LegendLuaBridge(rpc::lua::LuaVmManager::Ptr vm);

    // 每帧调用：场景 → Lua 怪物 AI
    void TickMonster(Entity::Ptr monster, int64_t nowMs, int32_t dtMs);
    // 掉落掷骰：返回掉落物品列表
    std::vector<int32_t> RollDrops(int32_t monsterId, int32_t killerLv);
    // 行会操作：返回错误码 0=成功
    int32_t GuildOp(const std::string& op, int argc, void* argv[]);
    // 聊天过滤：返回 true=放行
    bool ChatFilter(const std::string& msg, int32_t senderId);

private:
    rpc::lua::LuaVmManager::Ptr vm_;
    rpc::lua::MsgRouterLua::Ptr router_;
    rpc::lua::HotUpdateMgr::Ptr hot_;
};
```

**Lua 脚本清单（`mmo/scripts/`）：**

| 脚本 | 暴露函数 | C++ 调用场景 |
|------|---------|-------------|
| `monster_ai.lua` | `OnTickMonster(entity, now, dt) → state` | 每帧所有怪物 |
| `drop_tables.lua` | `RollDrop(monsterId, killerLv) → {itemId, count}[]` | 怪物死亡 |
| `pet_ai.lua` | `OnTickPet(entity, now, dt)` | 每帧宠物（可复用 monster_ai） |
| `guild_logic.lua` | `CanCreate/CanKick/CanDissolve(playerId, guildId)` | 行会操作鉴权 |
| `chat_logic.lua` | `Filter(msg, senderId) → bool` | 世界/行会/私聊发送 |
| `npc_dialog.lua` | `GetDialog(npcId, playerId) → text` | NPC 对话 |
| `scenes.lua` | `InitScenes() → {}sceneId, name, w, h, safe...{}` | GameService.Init 时加载场景表 |

### 13.7 简化后的 GameService 结构

```cpp
class GameService {
public:
    bool Init();          // ① 加载 scenes.lua 建场景 ② 注册实体 → 场景 ③ 绑定 Lua 回调
    bool Start(int32_t rpcPort, const std::string& serviceName);
    void Shutdown();
    void Tick(int64_t nowMs, int32_t dtMs);   // 主循环，替代 MonsterManager.TickAll 硬编码

private:
    SceneMgr::Ptr             scene_mgr_;    // 取代 map_mgr_
    EntityMgr::Ptr            entity_mgr_;   // 全局实体索引（含离线玩家释放）
    ItemMgr::Ptr              item_mgr_;
    std::unique_ptr<CombatSystem> combat_sys_;    // C++ 保留
    std::unique_ptr<LegendLuaBridge> lua_bridge_; // Lua 业务逻辑
    std::unordered_map<int32_t, PlayerActor::Ptr> players_; // 在线玩家（entity_mgr_ 的子集视图）
};
```

### 13.8 实施路线（4 期，每期可独立编译+测试通过）

**Phase 1 — C++ 实体化 + 场景化（不引入 Lua，现有测试全绿）**
- [ ] 新建 `Entity.h`（基类 + 派生类声明）、`EntityMgr`、`Scene`、`SceneMgr`
- [ ] 改造 `PlayerActor` 继承 `Entity`，新增 `PetId`
- [ ] 改造 `MonsterManager`：`MonsterInstance` 继承 `Entity`，从管理器剥离 AI tick
- [ ] 改造 `NPCManager`：`NpcInstance` 继承 `Entity`
- [ ] 新建 `Item.h`、`ItemMgr`；`DropManager` 委托 `ItemMgr`
- [ ] `GameService` 用 `SceneMgr`/`EntityMgr` 替换 MapManager/平铺容器
- [ ] 更新 tests 适配新接口，构建 + ctest 全绿

**Phase 2 — Lua 集成框架（启用 snc_rpc_lua 真实实现）**
- [ ] 安装/配置 Lua 5.4（或 LuaJIT），令 CMake 检测到 → SNC_LUA_AVAILABLE 生效
- [ ] 新建 `LegendLuaBridge.h/cpp`，封装 LuaVmManager/MsgRouterLua/HotUpdateMgr
- [ ] 新建 `mmo/scripts/` 目录，落空壳脚本（Phase 3/4 填充）
- [ ] `GameService::Init` 时 `lua_bridge_->LoadScripts()`，验证 VM 启动

**Phase 3 — 怪物 AI + 掉落 + 宠物走 Lua**
- [ ] `monster_ai.lua` 实现状态机（IDLE/PATROL/CHASE/ATTACK/RETURN/DEAD）
- [ ] `drop_tables.lua` 配置掉落表，`RollDrop` 掷骰
- [ ] `MonsterManager::Tick` 改为 `lua_bridge_->TickMonster(entity, nowMs, dtMs)`
- [ ] 新增 `PetInstance` 类 + `pet_ai.lua`（复用怪物 AI）
- [ ] 测试：monster_ai 状态流转 / drop 掷骰 stat 校验

**Phase 4 — 行会 + 聊天 + NPC 对话走 Lua**
- [ ] `guild_logic.lua`：建会/踢人/解散/仓库权限
- [ ] `chat_logic.lua`：禁词过滤 / 长度限制 / 冷却
- [ ] `npc_dialog.lua`：NPC 对话文本
- [ ] `GuildManager`/`WorldChannel` 业务方法改为调用 Lua 回调
- [ ] 启用 `HotUpdateMgr`：运营改 Lua 配置无需重启
- [ ] 全量测试 + example_full 回归

### 13.9 风险与权衡

| 风险 | 等级 | 缓解 |
|------|------|------|
| `SNC_LUA_AVAILABLE` 未定义时 Lua 代码编译为 stub，服务退化 | 中 | Phase 1 保证 C++ 逻辑完整可运行，Lua 为增量层 |
| Lua 每帧 Tick 怪物 (几百只 × 100ms) 有性能开销 | 低 | 只有"活跃怪"被 tick；每怪单次函数调用 < 1μs |
| Entity 继承改造影响现有测试 | 中 | Phase 1 同步更新测试桩，字段访问改路径不改语义 |
| snc_rpc_lua stub → 真实实现需要 Lua 库 | 低 | CMake 已具备 find_lua 逻辑，安装 Lua 后自动启用；LUA_ROOT 可指定 |
