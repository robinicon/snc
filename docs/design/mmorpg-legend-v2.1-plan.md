# MMORPG 传奇服务器 — 设计审计与优化方案

> 版本：v2.1
> 日期：2026-10-14
> 基于：v2.0 落地方案 + 当前代码库实现 + Phase 1~5 执行情况

---

## 一、当前实现状态总览

### 1.1 已完成模块（Phase 1~5 全部）

| 模块 | 语言 | 状态 | 说明 |
|------|------|------|------|
| Entity / EntityMgr | C++ | ✅ 完成 | 三层抽象（Entity/Scene/Item） |
| Scene / SceneMgr | C++ | ✅ 完成 | 替代 MapManager，含 kMapXXX 常量 |
| Item / ItemMgr | C++ | ✅ 完成 | 统一背包/仓库/地面掉落模型 |
| PlayerActor | C++ | ✅ 完成 | 继承 Entity，含职业/背包/装备/PK/行会缓存 |
| CombatSystem | C++ | ✅ 完成 | 伤害公式 + LuaBridge 驱动（含 C++ fallback） |
| MonsterManager | C++ | ✅ 完成 | AI 决策委托 Lua，Stub 降级 |
| DropManager | C++ | ✅ 完成 | 地面掉落 30min 过期，路径修复 |
| GuildManager | C++ | ✅ 完成 | 权限检查修复，成员缓存清除修复 |
| LuaBridge | C++ | ✅ 完成 | 条件编译（Lua 可用/Stub 双模式） |
| WorldChannel / NPCManager | C++ | ✅ 完成 | 基础实现 |
| GameService | C++ | ✅ 完成 | Init 加载 6 份 Lua 脚本，Shutdown 清理 |

### 1.2 已知问题（已修复）

| # | 问题 | 修复方式 | 状态 |
|---|------|---------|------|
| B1 | MaxHp() 因 Equip() 累加导致 > 150 | Equip 改为覆盖计算，重新遍历槽位 | ✅ |
| B2 | UseSkill check 依赖 Lua 可用 | CanUseSkill 前置检查 + LuaBridge 注入顺序 | ✅ |
| B3 | Drop 过期判断时间戳基准不一致 | Drop 创建用 steady_clock，测试传绝对时间 | ✅ |
| B4 | Guild PutGold 权限检查 | rank==3 成员无法存金，修复测试 rank | ✅ |
| B5 | DissolveGuild 未清除成员 guild_info_ | 解散时遍历清除所有成员缓存 | ✅ |

### 1.3 待完善项（本次审计发现）

见第三节"待改进项"。

---

## 二、架构健康度评估

### 2.1 分层架构正确性

```
✅ Gate 服（TCP 接入层）— 已有框架支持
✅ Game 服（业务层）   — GameService 组合所有子系统
✅ Entity 三层抽象    — Entity / Scene / Item 已落地
✅ 高频/低频分层      — 战斗/位置 C++，AI/掉落/Lua 配置 分离
⚠️  World 服          — 仅预留接口，未实现跨服场景锁
⚠️  LoginServer       — 未实现（设计文档有规划）
```

### 2.2 线程安全评估

| 组件 | 锁策略 | 评估 |
|------|--------|------|
| SceneMgr | 读写锁（mutable mutex） | ✅ 合理 |
| EntityMgr | mutex | ✅ 合理 |
| DropManager | mutex | ✅ 合理 |
| GuildManager | mutex | ✅ 合理 |
| MonsterManager | mutex | ✅ 合理 |
| PlayerActor | **无锁**（单线程访问） | ⚠️ 需确认并发模型 |
| GameService::players_ | mutex | ✅ 合理 |

### 2.3 Lua 集成质量

| 方面 | 评估 | 说明 |
|------|------|------|
| 条件编译 | ✅ | `SNC_LUA_AVAILABLE` 宏控制，无 Lua 时全 stub |
| 降级路径 | ✅ | UseSkill/AI/公式均有 C++ fallback |
| 脚本加载 | ✅ | Init() 中按顺序加载 6 份脚本 |
| 热更新 | ❌ | 未实现 HotUpdateMgr，脚本修改需重启 |
| 错误隔离 | ⚠️ | Lua pcall 捕获但无 VM 级隔离 |

---

## 三、待改进项（按优先级排序）

### P0 — 阻塞上线的风险项

#### P0-1: World 服完全空壳

**现状**：`WorldService.h` 仅预留接口，无实际跨服广播路由。

**风险**：多服架构下玩家无法跨服通信，攻城战无法实现。

**建议**：
```cpp
// 实现跨服场景管理器
class CrossSceneMgr {
public:
    // 创建跨服场景，向参与的每个 Game 服发 MSG_SCENE_BEGIN
    int32_t CreateCrossScene(const std::vector<int32_t>& gameServerIds);
    // 任意 Game 服掉线 → 标记该服玩家离线，继续至结束
    void OnGameServerDisconnected(int32_t gameServerId, int32_t sceneId);
    // 场景结束 → 向所有参与服发 MSG_SCENE_END
    void EndCrossScene(int32_t sceneId);
};
```

#### P0-2: Gate fd 清理回调缺失

**现状**：审计文档 M5 已指出，`fd_to_game_` 无清理逻辑。

**风险**：异常断连后 fd 占用，路由表泄漏。

**建议**：在 TcpConnection 的 CloseCallback 中添加：
```cpp
void OnClientDisconnected_(int32_t fd) {
    auto it = fd_to_game_.find(fd);
    if (it != fd_to_game_.end()) {
        auto gameId = it->second;
        fd_to_game_.erase(it);
        NotifyGameServerDisconnect(gameId, fd);
    }
}
```

#### P0-3: 登录流程时序未完整实现

**现状**：设计文档规定的 5 步登录流程（HTTP→JWT→TCP→Auth→Assign）仅部分实现。

**风险**：客户端无法完成完整登录。

**建议**：按审计文档六的时序图补全：
1. AccountService HTTP `/api/account/login` 签发 JWT（已有 StubDatabase）
2. Gate 本地验签（持有相同 HS256 secret）
3. Master 分配 Game 服 → RPC 路由

---

### P1 — 重要但非阻塞

#### P1-1: 宠物系统（PetInstance）未实现

**现状**：`EntityType::PET = 3` 已定义，`PlayerActor.h` 注释有"召唤神兽"，但无 PetInstance 类。

**建议**：
```cpp
// include/snc/mmorpg/legend/PetInstance.h
class PetInstance : public Entity {
public:
    using Ptr = std::shared_ptr<PetInstance>;
    // AI 复用 monster_ai.lua（通过 MonsterManager::SetLuaBridge 共享）
    // 跟随逻辑：每帧朝主人位置移动 speed * dt
};
```

#### P1-2: 怪物 CHASE/ATTACK 追击逻辑为空

**现状**：`MonsterManager::UpdateState()` 可调用 Lua AI 决策，但 `MoveToward()` 和实际攻击逻辑未完整实现。

**建议**：补全 Chase → 移动 → 到达攻击范围 → 触发 `hitCb` 回调链。

#### P1-3: Respawn 无触发路径

**现状**：`monster->alive = false` 无外部设置，`RespawnIfNeeded` 永不触发。

**建议**：在 DeathCallback 中设置 `monster->alive = false` 并记录 `died_at_ts`。

#### P1-4: NPCManager 对话/商店逻辑未实现

**现状**：`DispatchNPC_()` 仅回 "ok"，无真实交互。

**建议**：补全 NPCDialog / Buy / Sell / Teleport 的消息处理。

---

### P2 — 体验优化

#### P2-1: 端口硬编码

**现状**：所有端口（18080/20000/21000/22000/23000）硬编码在代码中。

**建议**：改为从配置文件或环境变量读取：
```cpp
uint16_t port = std::stoull(std::getenv("GAME_PORT") ? : "23000");
```

#### P2-2: 版本协议缺失

**现状**：客户端与服务器版本不一致时无拒绝机制。

**建议**：登录时交换版本号，不一致则返回 `ERR_VERSION_MISMATCH`。

#### P2-3: 消息协议解析粗糙

**现状**：`OnFromGate()` 用字符串搜索解析 JSON，无 protobuf 类型安全。

**建议**：迁移到 proto 定义 + `snc_proto` 序列化。

---

### P3 — 技术债清理

| # | 项 | 说明 |
|---|------|------|
| T1 | 删除 `.bak` / `_tmp_*` 文件 | `PlayerActor.h.bak` / `_tmp_PlayerActor.h` |
| T2 | 修复注释乱码 | `NPCManager.h` / `MonsterManager.h` 等文件 |
| T3 | 统一日志格式 | `SNC_LOG_INFO` 宏参数风格不一致 |
| T4 | AddItem 返回值语义错误 | 返回 `std::min(999, itemId)` 而非实际添加数量 |

---

## 四、优化方案

### 4.1 架构优化：引入 Actor 模型

**问题**：当前 GameService 主循环是单线程 pull 模式，随着实体数量增长会成为瓶颈。

**建议**：使用框架已有的 `WorkerPool` 将实体 Tick 分散到多核：
```cpp
// GameService::MainLoop_() 改造
WorkerPool pool(4); // 4 线程
for (auto& [id, scene] : scenes_) {
    pool.Submit([scene, nowMs, dtMs]{ scene->TickAll(nowMs, dtMs); });
}
pool.Drain();
```

### 4.2 Lua 热更新（HotUpdate）

**问题**：脚本修改需重启服务，运营不可接受。

**建议**：利用 `snc_rpc_lua` 的 `HotUpdateMgr`：
```cpp
// GameService::Init() 中注册热更新监听
hot_update_mgr_ = std::make_shared<HotUpdateMgr>(lua_state_);
hot_update_mgr_->Watch(LuaScriptPath::MONSTER); // 监控文件变更
hot_update_mgr_->OnChanged([this](const std::string& path) {
    SNC_LOG_INFO("legend.hotupdate", "Reloading: %s", path.c_str());
    lua_bridge_.LoadScript(path); // 重载脚本
});
```

### 4.3 数据库持久化

**问题**：当前 `StubDatabase` 无持久化，重启丢失数据。

**建议**：Phase 5 后接入 MySQL：
```cpp
// 替代 StubDatabase
class MySqlDatabase : public IDatabase {
public:
    bool Connect(const DbConfig& cfg);
    PlayerData LoadPlayer(int32_t playerId);
    void SavePlayer(const PlayerData& data);
};
```

### 4.4 跨服 RPC 路由增强

**问题**：World 服跨服场景锁未实现。

**建议**：利用框架 `RpcClient` 指数退避重连 + `CircuitBreaker`：
```cpp
class CrossSceneRouter {
    std::unordered_map<int32_t, rpc::RpcClient::Ptr> game_clients_;
    std::unordered_map<int32_t, CircuitBreaker> cb_;

    bool SendToGame(int32_t gameId, int32_t msgId, const char* payload, size_t len) {
        auto it = game_clients_.find(gameId);
        if (it == game_clients_.end()) return false;
        if (cb_[gameId].AllowRequest()) {
            return it->second->Call(msgId, payload, len);
        }
        return false; // 熔断中，丢弃请求
    }
};
```

---

## 五、v2.1 实施路线图

### Phase 6（建议）— 登录流程补全

- [ ] 补全 Gate JWT 本地验签
- [ ] 补全 Master Game 服分配逻辑
- [ ] 端到端测试：HTTP 登录 → TCP 连接 → 进入游戏

### Phase 7（建议）— 宠物 + 怪物追击

- [ ] 实现 PetInstance 继承 Entity
- [ ] 道士召唤神兽技能完整链路
- [ ] 怪物 CHASE 追击逻辑（MoveToward + hitCb 触发）
- [ ] Respawn 触发链（DeathCallback 中设 alive=false）

### Phase 8（建议）— 持久化

- [ ] 接入 MySQL（替换 StubDatabase）
- [ ] 玩家数据 Save/Load
- [ ] 行会数据持久化

### Phase 9（建议）— World 服

- [ ] 跨服场景锁（CreateCrossScene / OnDisconnected / EndCrossScene）
- [ ] 世界频道跨服广播

### Phase 10（建议）— 运维能力

- [ ] 端口配置文件化
- [ ] 版本协议握手
- [ ] 优雅停机（Graceful Shutdown）补全

---

## 六、与 v2.0 方案的关系

| v2.0 章节 | v2.1 状态 | 说明 |
|-----------|----------|------|
| Phase 1~5 | ✅ 全部完成 | Entity/Scene/Item 三层、Lua 集成、战斗/AI/掉落/行会 |
| Phase 6（新） | 建议实施 | 登录流程补全 |
| Phase 7（新） | 建议实施 | 宠物+怪物追击+Respawn |
| Phase 8（新） | 建议实施 | MySQL 持久化 |
| Phase 9（新） | 建议实施 | World 服跨服场景 |
| Phase 10（新） | 建议实施 | 运维能力（配置/版本/优雅停机） |
| P0-1~3 风险 | 高优先级 | World 空壳/Gate fd/登录时序 |
| P1-1~4 待完善 | 中优先级 | 宠物/追击/Respawn/NPC |
| P2-1~3 优化 | 低优先级 | 端口/版本/协议 |
| P3 技术债 | 随开发清理 | .bak/乱码/返回值 |

---

## 七、关键技术决策（ADR）

| ADR | 决策 | 理由 |
|-----|------|------|
| ADR-006 | 怪物 AI 走 Lua，数据层保留 C++ | 状态机规则运营可调，数据定义稳定 |
| ADR-007 | 掉落掷骰 C++ 实现（不迁移 Lua） | 高频调用，Lua 桥接开销不值得 |
| ADR-008 | GameService 主循环单线程，实体 Tick 并行 | 保持简单，Phase 6 后再评估 WorkerPool |
| ADR-009 | Lua 脚本路径相对 current_path() | 便于测试运行，部署时通过符号链接解决 |
| ADR-010 | 行会权限检查 C++ 实现（不迁移 Lua） | 权限规则变更频率低，C++ 更安全 |
