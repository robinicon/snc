# MMORPG 传奇 — 重新规划落地方案

> 版本：v2.0
> 日期：2026-10-13
> 前置：架构审计报告 + v1.0 设计文档 + 当前 mmo/ 代码库

---

## 一、现状总览

### 1.1 现有文件清单

**mmo/ 当前结构：**

```
mmo/
├── CMakeLists.txt              ✅ 构建配置
├── README.md                   ✅ 项目说明
├── include/snc/mmorpg/legend/
│   ├── PlayerActor.h           ✅ 玩家（职业/背包/装备/PK/行会）
│   ├── CombatSystem.h          ✅ 战斗计算（伤害公式/暴击/技能）
│   ├── DropManager.h           ✅ 掉落/拾取
│   ├── MonsterManager.h        ✅ 怪物（含AI状态机）
│   ├── NPCManager.h            ✅ NPC（商店/传送/对话）
│   ├── GameService.h           ✅ 服务集成（RPC入口）
│   ├── GuildManager.h          ✅ 行会（创建/权限/仓库/攻城战）
│   ├── WorldChannel.h          ✅ 聊天（世界/行会/私聊）
│   ├── MapManager.h            ⚠️  即将被 SceneMgr 取代
│   ├── PlayerActor.h.bak       ❌ 废弃备份，待删除
│   └── _tmp_PlayerActor.h      ❌ 临时文件，待删除
├── src/                        对应上述9个源文件
├── tests/                      6个测试组
└── examples/                   2个示例程序
```

**snc_rpc_lua 框架（已有能力）：**

| 头文件 | 职责 | 状态 |
|--------|------|------|
| `LuaBinding.h` | LuaVmManager / EntityExport / MsgRouterLua / HotUpdateMgr | stub（Lua未安装） |
| `LuaRouterApi.h` | C++ 注册 Lua 处理器（注册 msgId → handler） | stub |
| `LuaRpcApi.h` | Lua 侧调用 snc.rpc.call/notify/register | stub |

**CMake 配置：**
- 顶层 `CMakeLists.txt`：`SNC_RPC_ENABLE_LUA=ON`，`SNC_BUILD_MMO_LEGEND=ON`
- `lua/CMakeLists.txt`：支持 Lua 5.4/5.3/5.2/LuaJIT，未找到退化为 stub
- `LUA_ROOT` 环境变量可覆盖搜索路径

---

## 二、问题审计

### 2.1 架构问题（来自 v1.0 设计 + 代码审计）

| # | 问题 | 严重程度 | 修复方案 |
|---|------|---------|---------|
| P1 | `MapManager` 只管地图元数据，怪物/NPC/掉落游离于地图之外 | 高 | 引入 `Scene` 抽象，地图+实体合并为场景 |
| P2 | 玩家/怪物/NPC 无统一 Entity 基类，无法统一场景内遍历 | 高 | 新增 `Entity` 基类，三者均继承 |
| P3 | Item 模型分裂：`ItemSlot`(背包) / `GroundDrop`(地面) 两套结构 | 中 | 引入 `Item` 统一模型，地面掉落也作为场景内实体 |
| P4 | 宠物系统缺失（道士召唤神兽技能已定义但无实体） | 中 | 新增 `PetInstance` 继承 Entity |
| P5 | 怪物AI硬编码C++状态机，不可热更新 | 中 | AI逻辑迁移到Lua脚本 |
| P6 | `PlayerActor.h.bak` / `_tmp_PlayerActor.h` 临时文件未清理 | 低 | 删除 |
| P7 | NPCManager.h 注释有乱码（Unicode编码问题） | 低 | 修复注释 |

### 2.2 已知占位实现（不影响架构，后续补充）

| 位置 | 状态 |
|------|------|
| `CombatSystem::UseSkill()` | stub，返回空结果 |
| `CombatSystem::Poison()` | stub，返回空结果 |
| `MonsterManager` CHASE/ATTACK状态 | 空转，未实现实际追击/攻击逻辑 |
| `GameService::DispatchXxx_()` | 消息分发均为简化版，仅回"ok" |
| `RespawnIfNeeded` | 依赖 `alive=false` 外部设置，当前无触发路径 |

---

## 三、新架构设计

### 3.1 整体分层

```
┌─────────────────────────────────────────────────────────┐
│  Gate 服（TCP 接入层）                                     │
│  └── 持有 IConnection* → 按 fd 映射到玩家（已有）          │
└────────────────────┬────────────────────────────────────┘
                     │ RPC
┌────────────────────▼────────────────────────────────────┐
│  Game 服（业务层）                                         │
│                                                          │
│  GameService                                             │
│  ├── SceneMgr   (C++)    ← 场景容器，替代 MapManager     │
│  ├── EntityMgr  (C++)    ← 全局实体索引                  │
│  ├── ItemMgr    (C++)    ← 物品统一管理                  │
│  ├── CombatSystem(C++)   ← 伤害公式（性能敏感保留C++）    │
│  │                                                      │
│  └── LuaBridge (C++→Lua)  ← 业务逻辑（AI/掉落/行会/聊天）│
│       ├── monster_ai.lua                                │
│       ├── drop_tables.lua                               │
│       ├── guild_logic.lua                               │
│       ├── chat_logic.lua                                │
│       └── scenes.lua                                    │
│                                                          │
│  实体（全部继承 Entity）                                   │
│  ├── PlayerActor       (C++) 高频访问                   │
│  ├── MonsterInstance   (C++) 数据层，AI走Lua            │
│  ├── NpcInstance       (C++)                           │
│  └── PetInstance       (C++) 新增，道士召唤兽            │
└─────────────────────────────────────────────────────────┘
```

### 3.2 Entity 基类设计

```cpp
// include/snc/mmorpg/legend/Entity.h
enum class EntityType : int8_t {
    PLAYER = 0, MONSTER = 1, NPC = 2, PET = 3
};

class Entity {
public:
    virtual ~Entity() = default;
    virtual EntityType Type() const = 0;

    int32_t    EntityId()  const { return entity_id_; }
    int32_t    SceneId()   const { return scene_id_; }
    void       SetSceneId(int32_t id) { scene_id_ = id; }
    PlayerPos& Pos()        { return pos_; }
    const PlayerPos& Pos()  const { return pos_; }
    bool       Alive()      const { return alive_; }
    void       SetAlive(bool a) { alive_ = a; }

protected:
    int32_t    entity_id_ = 0;
    int32_t    scene_id_  = 0;
    PlayerPos  pos_;
    bool       alive_     = true;
};
```

**派生关系：**

```
Entity
├── PlayerActor    (职业/背包/装备/技能/行会/PK)
├── MonsterInstance (原 MonsterManager::MonsterInstance 提升为独立类)
├── NpcInstance    (原 NPCManager::NPCInstance 提升为独立类)
└── PetInstance    (新增，道士召唤兽，AI走monster_ai.lua)
```

### 3.3 Scene 与 SceneMgr（替代 MapManager）

```cpp
// Scene.h
struct SceneConfig {
    int32_t  scene_id;        // 对应原 kMapBegin 等常量
    string   name;
    int32_t  width, height;
    bool     is_safe_zone;
    bool     is_dungeon;
    int32_t  min_level;
};

class Scene : public enable_shared_from_this<Scene> {
public:
    void              AddEntity(Entity::Ptr e);
    void              RemoveEntity(int32_t id);
    Entity::Ptr       FindEntity(int32_t id);
    std::vector<Entity::Ptr> FindNearby(int32_t x, int32_t y, int32_t range);
    void              Broadcast(int32_t msgId, const char* data, size_t len);
    void              Tick(int64_t nowMs, int32_t dtMs);  // 委托 Lua 钩子
private:
    SceneConfig config_;
    EntityMgr::Ptr entity_mgr_;  // 持有本场景所有实体
};

// SceneMgr.h — 替代 MapManager，保留所有 kMapXXX 常量
class SceneMgr {
public:
    Scene::Ptr    CreateScene(const SceneConfig& cfg);
    Scene::Ptr    GetScene(int32_t id);
    void          RemoveScene(int32_t id);
    std::vector<Scene::Ptr> AllScenes();
    void          TickAll(int64_t nowMs, int32_t dtMs);
};
```

### 3.4 Item 统一模型

```cpp
// Item.h
struct Item {
    int32_t  item_id;     // 物品定义 ID
    int32_t  count;       // 数量
    int32_t  uid;         // 唯一实例 ID
    int64_t  expire_ts;   // 0=永久(背包)，>0=过期时间戳(地面掉落)
};

// ItemMgr.h — 统一管理背包/仓库/地面掉落
class ItemMgr {
public:
    Item::Ptr CreateItem(int32_t item_id, int32_t count, int64_t expire_ts = 0);
    Item::Ptr GetItem(int32_t uid) const;
    bool      DeleteItem(int32_t uid);
    // 地面掉落专用
    int       CleanupExpired(int64_t nowMs);
};
```

### 3.5 Lua 集成设计（基于 snc_rpc_lua）

```
LuaVmManager   ← 管理 Lua VM 生命周期
    ↓
LuaRouterApi   ← C++ 注册 msgId → Lua handler
    ↓
LuaRpcApi      ← Lua 侧发 snc.rpc.call/notify
```

**snc_rpc_lua 提供的接口（无需新增封装）：**

| 接口 | 用途 | 在本设计中 |
|------|------|-----------|
| `LuaVmManager::LoadScript(path)` | 加载 Lua 脚本 | GameService.Init 时加载所有 scripts/*.lua |
| `LuaVmManager::PushEntity<T>(ptr)` | C++ 实体→Lua userdata | 怪物/玩家进入场景时推入 Lua |
| `LuaRouterApi::RegisterHandler(msgId, fn)` | 注册消息处理器 | 场景事件路由到 Lua |
| `HotUpdateMgr` | 双缓冲热更新 | 脚本变更不重启服务 |
| `MsgRouterLua` | 串行单线程执行 Lua 消息 | 场景内事件处理 |

**scripts/ 目录（Lua 脚本）：**

```
mmo/scripts/
├── scenes.lua          -- 场景定义（SceneConfig 表）
├── monster_ai.lua      -- 怪物AI状态机（OnTick返回新状态）
├── drop_tables.lua     -- 掉落表配置 + RollDrops()函数
├── guild_logic.lua     -- 行会业务规则（权限/仓库操作）
├── chat_logic.lua      -- 聊天过滤规则（禁词/长度/冷却）
└── npc_dialog.lua      -- NPC对话文本模板
```

---

## 四、模块归属总表

| 模块 | 语言 | 理由 |
|------|------|------|
| `Entity` / `EntityMgr` | C++ | 高频路由索引 |
| `Scene` / `SceneMgr` | C++ | 场景生命周期管理 |
| `Item` / `ItemMgr` | C++ | 高频 CRUD |
| `PlayerActor` | C++ | 高频访问，RPC 直连 |
| `CombatSystem` | C++ | 伤害公式每帧多次 |
| `MonsterDef`（数据） | C++ | 定义结构稳定 |
| **MonsterAI**（逻辑） | **Lua** | 状态机规则可调 |
| **DropTable 配置** | **Lua** | 掉落热更新 |
| **GuildLogic**（操作） | **Lua** | 权限规则运营可控 |
| **ChatLogic**（过滤） | **Lua** | 禁词等运营可控 |
| `PetInstance` | C++ | 轻量结构，AI复用monster_ai.lua |
| `NPCManager` | C++ | 简单查找，变化少 |
| `WorldChannel` 数据 | C++ | 频道存储结构稳定 |

---

## 五、实施计划（5 期）

### Phase 1 — 基础架构（纯C++，不引入Lua，现有测试全绿）

**目标**：在现有代码上建立 Entity/Scene/Item 三层抽象，所有改动向下兼容，不引入 Lua 依赖。

**新增文件：**

```
include/snc/mmorpg/legend/
├── Entity.h                 ← 实体基类 + EntityType 枚举
├── EntityMgr.h/cpp          ← 全局实体索引（按id + 类型）
├── Scene.h/cpp              ← 场景（实体容器）
├── SceneMgr.h/cpp           ← 场景管理器（替代 MapManager）
├── Item.h                   ← 统一物品结构
└── ItemMgr.h/cpp            ← 物品管理器（背包/仓库/地面合并）

src/
├── EntityMgr.cpp
├── Scene.cpp
├── SceneMgr.cpp
├── ItemMgr.cpp
```

**修改文件：**

```
include/snc/mmorpg/legend/
├── PlayerActor.h/cpp    → 继承 Entity，添加 scene_id_
├── MonsterManager.h     → MonsterInstance 继承 Entity，从 Manager 剥离
├── NPCManager.h         → NpcInstance 继承 Entity
└── GameService.h/cpp    → 用 SceneMgr/EntityMgr/ItemMgr 替换 MapManager

tests/
├── test_player_actor.cpp → 适配新字段名
└── 新增 test_entity.cpp / test_scene.cpp / test_item.cpp
```

**删除文件：**
- `include/snc/mmorpg/legend/PlayerActor.h.bak`
- `include/snc/mmorpg/legend/_tmp_PlayerActor.h`

**验收标准**：`cmake --build . && ctest` 全绿，无新增警告。

---

### Phase 2 — Lua 框架集成

**目标**：启用 Lua，搭建 C++↔Lua 通信桥梁。

**前提条件**：
- 安装 Lua 5.4（或指定 `LUA_ROOT`），使 `SNC_LUA_AVAILABLE` 生效
- 或保留 stub 模式，Phase 3 代码对 stub 兼容（Lua 不可用时走 C++ fallback）

**新增文件：**

```
include/snc/mmorpg/legend/
└── LuaBridge.h/cpp        ← 封装 LuaVmManager + LuaRouterApi + HotUpdateMgr

scripts/
├── scenes.lua             ← 空壳（仅返回场景表）
├── monster_ai.lua         ← 空壳（仅定义 OnTick stub）
└── drop_tables.lua        ← 空壳（仅定义 RollDrops stub）
```

**修改文件：**
```
GameService.h/cpp    → Init() 时创建 LuaBridge，加载 scripts/
CMakeLists.txt       → 依赖 snc_rpc_lua（条件链接）
```

**验收标准**：CMake 检测到 Lua 时编译真实 Lua 绑定，未检测到时编译 stub 且可运行。

---

### Phase 3 — MonsterAI + 掉落 + 宠物走 Lua

**目标**：将怪物AI和掉落配置迁移到 Lua，新增宠物系统。

**新增文件：**
```
scripts/
├── monster_ai.lua      ← 完整状态机（IDLE/PATROL/CHASE/ATTACK/RETURN/DEAD）
└── drop_tables.lua     ← 掉落表 + RollDrops()

include/snc/mmorpg/legend/
└── PetInstance.h/cpp   ← 宠物实体（继承 Entity）

src/
└── PetInstance.cpp
```

**修改文件：**
```
MonsterManager.h/cpp  → 精简为数据层，Tick 委托 LuaBridge
GameService.cpp       → Spawn 怪物时注册 Lua AI 回调
DropManager.h/cpp     → Roll 逻辑委托 LuaBridge
PlayerActor.h/cpp     → 添加 SummonPet()/DespawnPet() 接口
```

**验收标准**：
- 测试：怪物状态转换（Lua 驱动）
- 测试：掉落掷骰统计与配置一致
- 测试：道士召唤宠物，宠物移动跟随

---

### Phase 4 — 行会 + 聊天走 Lua

**目标**：行会权限规则和聊天过滤逻辑迁移到 Lua。

**新增文件：**
```
scripts/
├── guild_logic.lua   ← 建会/踢人/解散权限检查
├── chat_logic.lua    ← 禁词/长度/冷却过滤
└── npc_dialog.lua    ← NPC对话文本模板
```

**修改文件：**
```
GuildManager.h/cpp   → 业务方法改为调用 LuaBridge
WorldChannel.h/cpp   → 过滤逻辑改为调用 LuaBridge
NPCManager.h/cpp     → 对话返回改为调用 LuaBridge
```

**验收标准**：
- 测试：Lua 禁词列表修改后立即生效
- 测试：行会操作权限检查（会长/副会长/成员）

---

### Phase 5 — 完善与清理

**目标**：填补占位实现，清理临时文件，完成端到端 demo。

**工作内容：**
1. 实现 `CombatSystem::UseSkill()` — 实际技能伤害计算
2. 实现 `CombatSystem::Poison()` — 道士施毒
3. 实现 `MonsterManager` CHASE/ATTACK 真实追击逻辑
4. 实现 `RespawnIfNeeded` — 怪物死亡重生触发链
5. 修复 `NPCManager.h` 注释乱码
6. 更新 `README.md` — 反映新架构
7. 删除所有 `.bak` / `_tmp_*` 文件

---

## 六、与现有设计文档的关系

| 原设计章节 | 新状态 | 说明 |
|-----------|--------|------|
| 一~十二（v1.0 完整设计） | 保留，作为参考历史 | 核心玩法和业务规则仍有效 |
| 十三（v2.0 重构方案） | **被本章取代** | 本章是 v2.0 的最终落地规划 |
| MapManager | 废弃，由 SceneMgr 取代 | 常量 kMapXXX 迁移到 SceneMgr |
| 怪物AI（C++状态机） | 迁移到 Lua | 数据层保留，逻辑层走 Lua |
| 掉落表（C++硬编码） | 迁移到 Lua 配置 | C++ 只负责存储和查询 |

---

## 七、执行约束

1. **Phase 1 必须独立通过编译和测试**，不引入任何 Lua 编译期依赖
2. **Entity 基类必须包含 `scene_id_`**，这是场景化架构的基础
3. **lua.hpp 仅在 Phase 2+ 引入**，Phase 1 代码不得包含 Lua 头文件
4. **所有 C++ 测试在 Phase 1 完成后必须全部通过**，不改变测试语义
5. **stub 模式下的 GameService 行为必须与 Lua 模式等价**（通过条件编译保证）
