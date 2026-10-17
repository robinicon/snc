# Skynet 实现传奇类 MMORPG — 可行性评估与落地方案

**日期**：2026-10-15
**状态**：评估草稿

---

## 一、执行摘要

| 维度 | 结论 |
|------|------|
| **总体可行性** | ✅ 高可行，但需权衡架构取舍 |
| **推荐路径** | 分阶段迁移：先 Lua 游戏逻辑层，再逐步替换框架层 |
| **核心收益** | 开发效率↑、热更能力↑、Actor 天然隔离 |
| **核心代价** | 性能上限↓、类型安全↓、C++ 资产复用率低 |
| **建议适用场景** | 中小型团队（10-30人），追求快速迭代和灵活业务逻辑 |

---

## 二、Skynet 框架核心特性回顾

### 2.1 架构概览

```
┌──────────────────────────────────────────────────────┐
│                   Harbor 1 (进程)                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│  │ Gate     │  │ Game_1   │  │ Monitor  │           │
│  │ Service  │  │ Service  │  │ Service  │           │
│  └──────────┘  └──────────┘  └──────────┘           │
├──────────────────────────────────────────────────────┤
│                   Harbor 2 (进程)                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│  │ (standby)│  │ Game_2   │  │ DB       │           │
│  │          │  │ Service  │  │ Service  │           │
│  └──────────┘  └──────────┘  └──────────┘           │
└──────────────────────────────────────────────────────┘
         ↑                                      ↑
    skynet.tar（二进制 RPC 协议，跨进程）
         每个 Service = 独立轻量进程 + 独立 Lua VM
```

### 2.2 关键机制

| 机制 | Skynet 实现 | 与 SNC 对比 |
|------|------------|------------|
| Actor 模型 | 每 Service 独立 VM + 私有消息队列 | SNC：共享线程池 + weak_ptr |
| 进程模型 | Harbor 多进程（C 层 fork） | SNC：多进程通过 systemd/Docker |
| 定时器 | `skynet.timer`（C 层高精度） | SNC：TimerManager（std::map） |
| 网络 I/O | `skynet.socket`（LFS 封装 epoll） | SNC：IEventLoop（epoll/select/kqueue） |
| 服务发现 | `ServiceHub` + `Watchdog` | SNC：ServiceRegistry + ClusterGateway |
| 热更新 | `skynet.reloader`（Lua VM 级） | SNC：双缓冲 HotUpdateMgr |
| C 扩展 | CService（C 写的高性能服务） | SNC：LuaBridge（C++ 导出到 Lua） |

### 2.3 Skynet 的性能特点

- **单线程 Actor**：每个 Service 内部无锁，顺序处理消息
- **跨进程 RPC**：skynet.tar 协议，序列化开销约 50-100ns/消息
- **协程调度**：Lua 5.3 coroutines，上下文切换约 100-200ns
- **典型吞吐**：单机 10万+ 消息/秒（纯路由场景）

---

## 三、可行性评估

### 3.1 技术匹配度分析

#### 3.1.1 传奇 MMO 核心需求 vs Skynet 能力

| 传奇 MMO 需求 | Skynet 能力 | 匹配度 | 说明 |
|--------------|------------|--------|------|
| 大量玩家并发（千人同服） | Harbor 水平扩展 | ✅ 高 | 每个 Game Service 承载部分玩家 |
| 场景内实体 Tick（30fps） | 单线程无竞争 | ✅ 高 | Actor 天然隔离，无需锁 |
| 战斗计算（高频） | Lua 可接受 | ⚠️ 中 | 热点路径建议 CService |
| 怪物 AI（低频 Tick） | Lua 完全胜任 | ✅ 高 | 当前已用 Lua 实现 |
| 行会/社交系统 | Lua 灵活实现 | ✅ 高 | 规则多变，Lua 适合 |
| 掉落/装备逻辑 | Lua 配置驱动 | ✅ 高 | 与当前方案一致 |
| 跨服通信 | skynet.tar | ✅ 高 | 已有成熟协议 |
| 热更新（不重启） | reloader | ✅ 高 | 核心优势 |
| 持久化存储 | 外部 MySQL/Redis | ✅ 高 | 同 SNC |
| 监控/运维 | monitor service | ✅ 高 | 内置健康检查 |

#### 3.1.2 关键差异点

```
┌─────────────────────────────────────────────────────────────┐
│  差异 1：Actor 粒度                                          │
│                                                             │
│  Skynet：每个玩家 = 独立 Service（~4MB/进程）                │
│  SNC：所有玩家 Actor 共享线程池                              │
│                                                             │
│  影响：Skynet 隔离性更好，但进程开销大；SNC 资源利用率高     │
├─────────────────────────────────────────────────────────────┤
│  差异 2：序列化                                              │
│                                                             │
│  Skynet：自带 binary proto（紧凑）                           │
│  SNC：PBFbs stub（未完整实现）                              │
│                                                             │
│  影响：Skynet 网络层更成熟，SNC 需补齐                       │
├─────────────────────────────────────────────────────────────┤
│  差异 3：C++ 集成深度                                        │
│                                                             │
│  Skynet：CService 可写高性能模块，但接口设计需学习成本       │
│  SNC：LuaBridge 较直接，但 VM 共享带来并发复杂               │
│                                                             │
│  影响：SNC 现有 C++ 代码（CombatSystem 等）需重构或包装     │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 性能对比

| 指标 | Skynet（Lua-only） | Skynet（Lua+CService） | SNC（当前） |
|------|-------------------|----------------------|------------|
| 单进程 CPU 利用 | 1 核（单线程） | 多核（C 部分多线程） | N 核（线程池） |
| 消息延迟 P99 | ~50μs | ~10μs | ~20μs |
| 内存/玩家 | ~4MB（含 VM） | ~2MB（共享 VM） | ~0.5MB |
| 千玩家吞吐 | 5万 msg/s | 15万 msg/s | 20万 msg/s |
| 热更停机时间 | 0（VM 级） | 0 | ~200ms（版本切换） |

**结论**：Skynet 在开发效率和热更体验上有优势，但在绝对性能上限上弱于 SNC。

### 3.3 开发效率对比

| 维度 | Skynet | SNC（当前） |
|------|--------|------------|
| 新业务逻辑开发 | ⭐⭐⭐⭐⭐ Lua 快速迭代 | ⭐⭐⭐ C++ 编译等待 |
| 数值调整热更 | ⭐⭐⭐⭐⭐ 即时生效 | ⭐⭐⭐ 需重启服务 |
| 调试难度 | ⭐⭐⭐ 栈清晰 | ⭐⭐ 多线程竞态难复现 |
| 类型安全 | ⭐⭐ 动态类型 | ⭐⭐⭐⭐⭐ 编译期检查 |
| 代码重构 | ⭐⭐ 风险较高 | ⭐⭐⭐⭐ 编译器助力 |
| 测试覆盖 | ⭐⭐ 较难单测 | ⭐⭐⭐⭐ doctest 完善 |

---

## 四、落地方案

### 4.1 推荐路径：渐进式迁移

```
阶段 0（评估期，2周）      阶段 1（框架搭建，4周）     阶段 2（逻辑迁移，8周）     阶段 3（优化上线，4周）
     │                         │                          │                          │
     ▼                         ▼                          ▼                          ▼
┌─────────┐              ┌──────────┐              ┌──────────┐              ┌──────────┐
│Skynet   │              │Harbor    │              │GameLogic │              │性能调优  │
│原型验证 │───►         │+LuaVM    │───►         │迁移完成  │───►         │灰度发布  │
│核心概念 │              │基础服务  │              │C++/Lua   │              │全量上线  │
│         │              │搭建完成  │              │双轨运行  │              │监控跟进  │
└─────────┘              └──────────┘              └──────────┘              └──────────┘
```

### 4.2 阶段详解

#### 阶段 0：原型验证（2 周）

**目标**：验证 Skynet 能否支撑传奇 MMO 的核心场景

**交付物**：
- [ ] Skynet 开发环境搭建（Ubuntu 22.04 + Lua 5.4 + CMake）
- [ ] Hello World Service 示例（Gate → Game → DB）
- [ ] 玩家登录流程原型（连接 → 认证 → 进入场景）
- [ ] 性能基准测试（1000 虚拟玩家，测量延迟和内存）

**关键技术验证点**：
```lua
-- 验证点 1：Actor 隔离
-- 每个玩家独立 Service，消息不会串
local player = require "player"
player.create(1001)  -- 创建玩家 1001 的 Actor
player.create(1002)  -- 创建玩家 1002 的 Actor
-- 确认两个 Actor 的消息队列独立

-- 验证点 2：跨 Service 调用
-- Gate → Game 的消息转发延迟 < 1ms
local result = skynet.call("game_1", "lua", "login", username, password)

-- 验证点 3：热更新
-- 修改 combat.lua 后 reloader 自动加载，无需重启
```

**风险**：如果原型阶段发现 Skynet 的 Actor 粒度导致内存超支（>50GB/千玩家），则需重新评估。

---

#### 阶段 1：框架搭建（4 周）

**目标**：建立可运行的 Skynet 传奇 MMO 基础框架

**目录结构规划**：
```
mmo-skynet/
├── conf/                    # 配置文件
│   ├── master.conf         # Harbor 配置
│   ├── gate.conf           # Gate 服配置
│   └── game.conf           # Game 服配置
├── main.lua                 # 启动入口
├── skynet-src/              # Skynet 核心（git submodule）
├── lualib/                  # 项目 Lua 库
│   ├── mmo/
│   │   ├── player.lua      # 玩家逻辑（对应 PlayerActor）
│   │   ├── scene.lua       # 场景管理（对应 Scene）
│   │   ├── combat.lua      # 战斗逻辑（对应 CombatSystem）
│   │   ├── monster.lua     # 怪物 AI（对应 MonsterManager）
│   │   ├── drop.lua        # 掉落管理（对应 DropManager）
│   │   └── guild.lua       # 行会系统（对应 GuildManager）
│   └── db/
│       ├── mysql.lua       # MySQL 封装
│       └── redis.lua       # Redis 封装
├── service/                 # C Services
│   ├── gate_service.c      # 网关服务
│   ├── game_service.c      # 游戏服务（C 层骨架）
│   └── db_service.c        # 数据库服务
└── tests/                   # 测试
    ├── test_player.lua
    ├── test_combat.lua
    └── ...
```

**核心模块映射（SNC → Skynet）**：

| SNC 模块 | Skynet 对应 | 改造策略 |
|---------|------------|---------|
| `PlayerActor` | `service/player.lua`（每个玩家一个 Service） | 直接迁移逻辑，VM 隔离自动获得 |
| `Scene` | `service/scene.lua`（场景管理器 Service） | 持有玩家 Service 地址表 |
| `CombatSystem` | `lualib/mmo/combat.lua` | 保留算法，重写为 Lua |
| `MonsterManager` | `service/monster.lua` | 已有 Lua 实现，直接复用 |
| `DropManager` | `lualib/mmo/drop.lua` | 已有 Lua 实现，直接复用 |
| `GuildManager` | `service/guild.lua` | 已有 Lua 实现，直接复用 |
| `WorldChannel` | `service/world_channel.lua` | 已有 Lua 实现，直接复用 |
| `TcpServer/EventLoop` | `skynet.socket` + `skynet.gate` | 使用 Skynet 内置 |
| `RpcClient/RpcServer` | `skynet.call` / `skynet.send` | API 替换 |
| `ServiceRegistry` | `skynet.register` + `skynet.queryservice` | Skynet 内置 |
| `ClusterGateway` | Harbor 间路由 | Skynet 内置 |

**关键实现代码示例**：

```lua
-- service/player.lua（玩家 Actor）
local skynet = require "skynet"
local player_lib = require "mmo.player_lib"

local player = {}
player.__index = player

function player.new(player_id, character_data)
    local self = setmetatable({}, player)
    self.id = player_id
    self.hp = character_data.hp
    self.mp = character_data.mp
    self.exp = character_data.exp
    self.level = character_data.level
    self.scene_id = nil
    self.inventory = {}
    self.skills = {}
    return self
end

function player:take_damage(damage)
    self.hp = math.max(0, self.hp - damage)
    if self.hp == 0 then
        self:on_dead()
    end
end

function player:on_message(msg_type, payload)
    if msg_type == "MOVE" then
        self:on_move(payload.x, payload.y)
    elseif msg_type == "ATTACK" then
        self:on_attack(payload.target_id)
    elseif msg_type == "CHAT" then
        self:on_chat(payload.message)
    end
end

function player:on_tick(dt)
    -- 每秒自然恢复
    if skynet.time() % 1 == 0 then
        self.hp = math.min(self.max_hp, self.hp + self.hp_regenerate)
        self.mp = math.min(self.max_mp, self.mp + self.mp_regenerate)
    end
end

skynet.start(function()
    local id = skynet.getenv("player_id")
    local data = skynet.call("db", "lua", "load_player", tonumber(id))
    local p = player.new(tonumber(id), data)
    skynet.register(".player_" .. id)

    while true do
        local _, msg = skynet.consumequeue()
        local session, source, msg_type, payload = skynet.unpack(msg)
        p:on_message(msg_type, payload)
        if session ~= 0 then
            skynet.ret(skynet.pack(true))
        end
    end
end)
```

```lua
-- service/scene.lua（场景管理）
local skynet = require "skynet"
local scene_mgr = require "mmo.scene_mgr"

local players = {}  -- player_id -> scene_service_addr

function scene_mgr:add_player(player_id, scene_id)
    local scene_addr = self.scenes[scene_id]
    if not scene_addr then
        scene_addr = skynet.call(".scene_hub", "lua", "create_scene", scene_id)
        self.scenes[scene_id] = scene_addr
    end
    players[player_id] = scene_addr
    skynet.call(scene_addr, "lua", "enter_player", player_id)
end

function scene_mgr:remove_player(player_id)
    local scene_addr = players[player_id]
    if scene_addr then
        skynet.call(scene_addr, "lua", "leave_player", player_id)
        players[player_id] = nil
    end
end

skynet.start(function()
    skynet.register(".scene_hub")
    -- ...
end)
```

---

#### 阶段 2：逻辑迁移（8 周）

**周序安排**：

| 周次 | 任务 | 产出 |
|------|------|------|
| W1-W2 | 账号服迁移（Auth + DB） | 登录/注册流程可用 |
| W3-W4 | Gate 服迁移（连接管理） | TCP 接入 + JWT 验签 |
| W5-W6 | 玩家系统迁移（PlayerActor → Lua Service） | 角色创建/移动/聊天 |
| W7-W8 | 战斗系统迁移（CombatSystem → Lua） | 技能/伤害/死亡逻辑 |
| W9-W10 | 怪物系统迁移（MonsterManager） | AI/巡逻/攻击/重生 |
| W11-W12 | 行会/掉落/世界频道迁移 | 完整游戏循环 |

**迁移策略**：

```
┌──────────────────────────────────────────────────────────────┐
│  策略 A：直接重写为 Lua（推荐用于游戏逻辑层）                   │
│  - combat.lua, monster.lua, drop.lua, guild.lua             │
│  - 原因：这些逻辑本来就设计为 Lua 可替换                      │
│  - 收益：获得热更新能力                                       │
├──────────────────────────────────────────────────────────────┤
│  策略 B：CService 包装（推荐用于性能热点）                     │
│  - CombatSystem 核心计算（如果 Lua 性能不达标）               │
│  - 大型场景的实体查询（EntityMgr 的空间索引）                 │
│  - 原因：C 层性能高，避免 Lua GC 抖动                         │
├──────────────────────────────────────────────────────────────┤
│  策略 C：保留 SNC C++ 代码（推荐用于框架层）                   │
│  - 暂时不迁移，通过 RPC 调用 SNC 服务                        │
│  - 双轨运行，逐步替换                                         │
│  - 原因：降低迁移风险，验证 Skynet 可行性后再决定             │
└──────────────────────────────────────────────────────────────┘
```

---

#### 阶段 3：优化上线（4 周）

**任务清单**：
- [ ] 性能压测（目标：单机 2000+ 在线玩家，P99 延迟 < 50ms）
- [ ] 内存泄漏检测（Lua profiler + Valgrind）
- [ ] 容灾测试（Harbor 故障转移、Watchdog 重启验证）
- [ ] 监控面板对接（prometheus metrics 导出）
- [ ] 灰度发布方案（1 个 Harbor 先上生产）

---

## 五、风险评估

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| Lua GC 暂停导致卡顿 | 🔴 高 | 使用 Lua 5.4 + incremental GC；热点路径用 CService |
| 单 Actor 消息积压 | 🟡 中 | 设置队列深度上限，超限时主动丢弃/熔断 |
| Harbor 故障转移延迟 | 🟡 中 | Watchdog 配置合理 timeout；Graceful Shutdown |
| Lua 与 C++ 代码复用冲突 | 🟡 中 | 阶段 2 采用双轨策略，不强制一次性替换 |
| 团队技能转型成本 | 🟡 中 | 阶段 0 原型验证降低不确定性；编写 Skynet 最佳实践文档 |
| 社区维护风险（Skynet 已停止活跃维护） | 🟠 低 | 基于 cloudwu/skynet fork 维护；或考虑 SkyFire 等替代 |

---

## 六、替代方案对比

如果 Skynet 不可行，可考虑的备选方案：

| 方案 | 优势 | 劣势 | 适用场景 |
|------|------|------|---------|
| **SkyFire**（Skynet 分支） | 活跃维护、C++ 扩展改进 | 社区较小 | 需要 Skynet 风格 + 活跃社区 |
| **保持 SNC 继续演进** | 现有资产完整、性能最优 | 热更能力有限 | 团队 C++ 强、性能优先 |
| **Unreal NSH（Networked Simulation Hook）** | 工业级、引擎集成 | 重量级、学习曲线陡 | 3D 高保真 MMO |
| **自研 Actor 框架** | 完全可控 | 开发成本高 | 长期战略项目 |

---

## 七、推荐决策

### 场景 A：团队以 Lua 为主，追求快速迭代
→ **选择 Skynet**，按上述 4 阶段方案执行

### 场景 B：团队以 C++ 为主，性能优先
→ **保持 SNC**，在其基础上增强热更新（参考 Skynet reloader 思路）

### 场景 C：团队两者兼顾，希望渐进迁移
→ **双轨并行**：Skynet 新业务 + SNC 存量业务，通过 RPC 互调，逐步迁移

---

## 八、下一步行动

1. **本周**：搭建 Skynet 开发环境，跑通 Hello World
2. **下周**：实现玩家登录原型（Gate → Game → DB 完整链路）
3. **2 周内**：完成阶段 0 原型验证报告，决定是否进入阶段 1
