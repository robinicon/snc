# 22 — 传奇版概览

**目标**：了解基于 SNC 框架的传奇 (Legend of Mir) 风格 MMORPG 服务器架构。

## 概述

`mmo/` 目录包含基于 SNC 框架的传奇风格 MMORPG 服务器实现，对应 `examples/example_03_game.cpp`。

与框架层（`snc_mmorpg`）的区别：

| 维度 | 框架层 (snc_mmorpg) | 传奇版 (mmo/) |
|------|---------------------|---------------|
| 职责 | 基础服务组件（Gate/Game/World/Master） | 传奇游戏逻辑实现 |
| 玩家实体 | 简单 PlayerActor（ID/名字/位置） | 增强版 PlayerActor（职业/背包/装备/技能/PK/行会） |
| 地图管理 | MapManager（已废弃） | SceneMgr（场景管理） |
| NPC/怪物 | 无 | NPCManager / MonsterManager |
| 战斗 | 无 | CombatSystem |
| 掉落 | 无 | DropManager |
| 行会 | 无 | GuildManager |
| 世界频道 | 无 | WorldChannel |
| Lua 脚本 | snc_rpc_lua 通用桥接 | LuaBridge（传奇专用） |

## 模块清单

```
mmo/include/snc/mmorpg/legend/
├── GameService.h      # 集成所有子系统的游戏服务
├── PlayerActor.h      # 增强版玩家（职业/背包/装备/技能）
├── Entity.h           # 实体基类（玩家/NPC/怪物/Pet）
├── EntityMgr.h        # 实体管理器
├── SceneMgr.h         # 场景管理器（替代 MapManager）
├── Scene.h            # 场景定义
├── Item.h / ItemMgr.h # 物品系统
├── NPCManager.h       # NPC 管理
├── MonsterManager.h   # 怪物管理
├── CombatSystem.h     # 战斗系统
├── DropManager.h      # 掉落管理
├── GuildManager.h     # 行会管理
├── WorldChannel.h     # 世界频道
├── PetInstance.h      # 宠物实体（道士召唤）
└── LuaBridge.h        # Lua 脚本桥接

mmo/src/                # 上述模块的 .cpp 实现
```

## 消息协议

### 服务端→客户端（Gate 转发）

| 消息 ID | 含义 |
|---------|------|
| `0x1200` | 玩家属性全量同步 |
| `0x1201` | 实体位置批量同步 |
| `0x1202` | 伤害结果 |
| `0x1203` | 死亡通知 |
| `0x1204` | 掉落物刷新 |
| `0x1205` | 升级通知 |
| `0x1206` | 聊天广播 |
| `0x1207` | 行会通知 |
| `0x1208` | 进入场景 |
| `0x1209` | 离开场景 |
| `0x120A` | 移动响应（确认/拒绝） |
| `0x120B` | 通用错误提示 |

### 客户端→服务端（Gate 转发）

| 消息 ID | 含义 |
|---------|------|
| `0x1100` | 移动 |
| `0x1101` | 攻击 |
| `0x1102` | NPC 对话 |
| `0x1103` | 聊天 |
| `0x1104` | 拾取物品 |
| `0x1105` | 释放技能 |
| `0x1106` | 行会请求 |

### RPC 内部消息

| 消息 ID | 方向 | 含义 |
|---------|------|------|
| `0x2000` | Gate→Game | 客户端消息转发 |
| `0x2100` | Game→Gate | 服务端推送给客户端 |
| `0x3000` | Game→Master | 注册/心跳 |
| `0x3001` | Game→Master | 心跳续期 |

## GameService 使用方式

```cpp
#include "snc/mmorpg/legend/GameService.h"

// 1. 创建 RPC 客户端（用于与 Master 通信）
auto registry = discovery::ServiceRegistry::Create();
auto master_rpc = std::make_shared<rpc::RpcClient>(
    std::make_shared<rpc::RpcSession>(nullptr));

// 2. 创建 GameService
auth::JwtConfig jwt_cfg{"jwt-secret-key-32-chars-minimum!", 7200, "snc-mmorpg", "HS256"};
legend::GameService svc(master_rpc, registry, jwt_cfg);

// 3. 初始化所有子系统（地图/NPC/怪物/掉落/行会/世界频道）
svc.Init();

// 4. 启动服务（阻塞调用）
svc.Start(8001, "game_01");

// 5. 优雅停机
svc.Shutdown();
```

## 架构分层

```
┌─────────────────────────────────────────────────────┐
│  传奇游戏逻辑 (mmo/src/)                            │
│  PlayerActor / CombatSystem / DropManager / ...    │
├─────────────────────────────────────────────────────┤
│  GameService（传奇版）                               │
│  集成所有子系统，处理 RPC 消息分发                    │
├─────────────────────────────────────────────────────┤
│  snc_mmorpg（框架层）                                │
│  GateService / GameService / WorldService / Master  │
├─────────────────────────────────────────────────────┤
│  snc_rpc / snc_core / snc_discovery / snc_auth     │
│  RPC / TCP / 服务发现 / JWT                         │
├─────────────────────────────────────────────────────┤
│  snc_base                                          │
│  日志 / 线程 / 数据结构                             │
└─────────────────────────────────────────────────────┘
```

## 构建传奇版

传奇版作为 `snc_mmorpg` 库的一部分编译，不需要单独构建。确保 CMake 预设中启用 `SNC_BUILD_MMORPG=ON` 和 `SNC_BUILD_MMO_LEGEND=ON`（默认开启）。

```bash
cmake --preset=full-msvc
cmake --build --preset=full-msvc --target example_03_game
.\build\Release\example_03_game.exe 8001 127.0.0.1 9000
```

## 设计文档

完整的传奇版设计方案请参考：

- [mmorpg-legend-server-design.md](file:///c:/Users/10422/Documents/code/snc/docs/design/mmorpg-legend-server-design.md)（整体方案）
- [mmorpg-legend-implementation-spec.md](file:///c:/Users/10422/Documents/code/snc/docs/design/mmorpg-legend-implementation-spec.md)（实施规格）

## 下一步

- [19 — Actor 生命周期](./19-actor-lifecycle.md) — 框架层 PlayerActor 基础
- [14 — ClusterGateway](./14-cluster-gateway.md) — 集群网关（多 Game 服负载均衡）
- [17 — 账号服](./17-account-service.md) — AccountService 集成
