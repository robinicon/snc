# 在 SNC 中引入 Skynet 热更思路 — 可行性评估

**日期**：2026-10-15
**状态**：评估草稿

---

## 一、背景：SNC 现有热更能力 vs Skynet reloader

### 1.1 SNC 当前方案：双缓冲 HotUpdateMgr

SNC 已有 `HotUpdateMgr`（[LuaBinding.h](file:///c:/Users/10422/Documents/code/snc/include/snc/rpc/lua/LuaBinding.h#L170-L215)），采用**双缓冲 + 手动 Commit** 模式：

```
当前流程：
  Buffer A (active) ←─┐
                      │  LoadVersion() 加载新版本到 Buffer B
  Buffer B (pending)  │  Commit() 原子切换 activeIdx_
                      │
  执行时机：          需 C++ 代码显式调用
```

**已有特性**：
- 双 VM 隔离（新旧脚本并行运行，切换后旧 VM 才销毁）
- 加载失败自动回滚（`Rollback()`）
- 版本追踪（`activeVersion_`）

**现有缺陷**：
| 缺陷 | 影响 |
|------|------|
| 无自动触发机制 | 需外部调用 `LoadVersion()` + `Commit()`，无法文件监听驱动 |
| 全局状态硬切换 | 切换瞬间所有 Lua 回调指向新 VM，无过渡期 |
| 无依赖解析 | 不检查 Lua 脚本间 require 链的一致性 |
| 无灰度/回滚 API | 只有 Commit/Rollback 两态 |
| `Reload()` 是空操作 | [LuaBinding.cpp:171](file:///c:/Users/10422/Documents/code/snc/src/rpc/lua/LuaBinding.cpp#L171) 返回 false 并打日志 |

### 1.2 Skynet reloader 核心思想

Skynet 的 `skynet.reloader` 是**基于文件监控 + VM 替换**的方案：

```
Skynet 流程：
  1. watch 目录下 .lua 文件的 mtime
  2. 检测到变化 → 创建新 Lua VM
  3. 在新 VM 中重新 require 所有模块（带 version arg）
  4. 替换所有注册的全局函数指针
  5. 旧 VM 继续服务已挂起的调用，结束后自然 GC
```

**关键设计**：
- **lazy reload**：不主动触发，等待下次定时器 tick 或消息处理时检查
- **VM 级替换**：整个 Lua state 整体替换，而非单文件 reload
- **依赖链重建**：通过 `require` 版本号参数重建模块间引用

---

## 二、差距分析

### 2.1 架构差异导致的热更挑战

```
┌─────────────────────────────────────────────────────────────────────┐
│  差异 1：Lua VM 所有权                                              │
│                                                                     │
│  Skynet：每个 Service 持有自己的 Lua VM，reloader 只需替换            │
│           本 Service 的 VM，无跨 Service 依赖问题                   │
│                                                                     │
│  SNC：  GameService 持有唯一 Lua VM，CombatSystem/MonsterManager     │
│         通过指针引用（SetLuaBridge）。热更要同时更新这些指针引用     │
├─────────────────────────────────────────────────────────────────────┤
│  差异 2：C++ ↔ Lua 对象绑定                                         │
│                                                                     │
│  Skynet：C Service 通过 luaL_newstate() 创建 VM，绑定一次性完成     │
│           reload 时重新创建 VM 即可                                │
│                                                                     │
│  SNC：  LuaBridge 通过 light userdata 将 C++ 指针推入 Lua           │
│         热更后 Lua 侧仍持有旧 VM 的指针，需显式重新注册              │
├─────────────────────────────────────────────────────────────────────┤
│  差异 3：没有文件事件循环                                            │
│                                                                     │
│  Skynet：内置 skynet.timer 可挂载文件监听回调                       │
│                                                                     │
│  SNC：  EventLoop 目前只处理网络 I/O（epoll/select/kqueue）         │
│         需要扩展以支持文件监控，或引入独立 watcher 线程             │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 具体差距清单

| 能力 | Skynet | SNC 现状 | 补齐难度 |
|------|--------|---------|---------|
| 文件变更监听 | `skynet.reloader` 内置 | ❌ 不存在 | 🟡 中 |
| 自动触发热更 | mtime 检测驱动 | ❌ 需手动调用 | 🟡 中 |
| VM 整体替换 | ✅ 原生支持 | ⚠️ 双缓冲但不自动 | 🟢 低 |
| 依赖链重建 | `require(name, version)` | ❌ 无版本号机制 | 🔴 高 |
| C++ 绑定更新 | 重新创建 VM 自动解决 | ⚠️ 需手动更新指针 | 🟡 中 |
| 灰度/回滚 | 可通过 reloader 实现 | ⚠️ 部分支持（Manual） | 🟢 低 |
| 跨服务协调 | 每个 Service 独立 reload | ❌ 单 VM 架构 | 🔴 高 |

---

## 三、可行性评估

### 3.1 分层可行性

#### 层 1：基础热更（文件监听 + 双缓冲切换）

**可行性：✅ 高**

这是最小可行路径，利用现有 `HotUpdateMgr` 扩展即可：

```
新增组件：LuaHotWatcher
  - 独立线程轮询 mmo/lua/*.lua 的 mtime
  - 检测到变化 → 调 HotUpdateMgr::LoadVersion()
  - 调用 GameService::OnHotUpdateComplete() 更新 C++ 绑定指针
```

**改动量预估**：约 200-300 行 C++ 代码

```cpp
// 新增：LuaHotWatcher.h（概念性伪代码）
class LuaHotWatcher {
public:
    void Start(const std::vector<std::string>& scriptPaths,
               HotUpdateMgr::Ptr hot_mgr,
               std::function<void()> on_commit);

private:
    void WatchLoop_(); // 独立线程，轮询 mtime
    std::mutex last_mtime_mtx_;
    std::unordered_map<std::string, std::time_t> last_mtime_;
};

// 在 GameService 中添加
void GameService::OnHotUpdateCommit() {
    // 重新注册 C++ 子系统到新的 Lua VM
    lua_State* new_L = lua_bridge_new_.L();
    LuaBridge::RegisterAllStatics(new_L);
    // 重新 push 全局变量...
}
```

#### 层 2：依赖链重建（require 版本号机制）

**可行性：⚠️ 中 — 需改造 Lua 加载链**

Skynet 的 `require(name, version)` 机制要求：
1. 每个 Lua 模块感知版本参数
2. 模块间通过版本号建立依赖关系
3. reload 时按依赖顺序重建

```lua
-- 当前 combat.lua 是无版本的原生 Lua
local M = {}
function M.calc_physical_damage(...) end
return M

-- 需要改造为 Skynet 风格
local M = {}
local VERSION = 1  -- 模块版本号

function M.__init__(version)
    if version and version > VERSION then
        error("version mismatch: need " .. VERSION .. " got " .. version)
    end
end

return M
```

**改造工作量**：6 个 Lua 脚本 + require 链重写 + C++ 侧版本追踪

#### 层 3：VM 级整体替换（最彻底的热更）

**可行性：✅ 高 — 但需要架构调整**

当前 GameService 使用单一共享 VM：
```cpp
// 当前：单 VM，C++ 子系统通过指针注入
lua_state_ = luaL_newstate();
lua_bridge_ = LuaBridge(lua_state_);
// 各子系统持有 &lua_bridge_ 引用
monster_mgr_->SetLuaBridge(&lua_bridge_);
combat_->SetLuaBridge(&lua_bridge_);
```

改为可替换 VM：
```cpp
// 目标：VM 可整体替换，子系统通过 shared_ptr 持有 VM
vm_manager_ = std::make_shared<LuaVmManager>();
lua_bridge_ = LuaBridge(vm_manager_->L());
// 子系统持有 shared_ptr<LuaVmManager>，替换时更新 shared_ptr
monster_mgr_->SetLuaVm(vm_manager_);
combat_->SetLuaVm(vm_manager_);
```

**改动影响面**：
- `MonsterManager::SetLuaBridge()` → `SetLuaVm()`：需改接口签名
- `CombatSystem::SetLuaBridge()` → `SetLuaVm()`：需改接口签名
- 所有 Lua 回调需通过 `shared_ptr` 访问 VM，引入轻微锁开销

#### 层 4：跨 Service 协调热更（多 Game 服场景）

**可行性：🟡 中 — 依赖分布式协调**

如果未来有多个 Game Service（分服），需要：
1. Master Service 下发热更指令
2. 各 Game Service 按序 reload（避免玩家掉线）
3. 协调期间暂停新玩家接入

这可以复用现有的 RPC 机制（`RpcClient::Call`），不需要额外框架支持。

---

## 四、推荐落地方案：三层渐进

### 4.1 总览

```
Phase A（2周）→ 基础热更：文件监听 + 双缓冲切换
Phase B（3周）→ 增强热更：依赖链 + C++ 绑定自动更新
Phase C（4周）→ 高级热更：VM 级整体替换 + 多服协调
```

### 4.2 Phase A：基础热更（高可行性，低风险）

**目标**：修改 `mmo/lua/` 下的脚本后，服务器自动检测并切换，无需重启。

**核心改动**：

1. **新增 `LuaHotWatcher` 类**
   ```
   include/snc/rpc/lua/LuaHotWatcher.h
   src/rpc/lua/LuaHotWatcher.cpp
   ```
   - 独立线程，轮询 `mmo/lua/*.lua` 的 mtime
   - 变化检测阈值：1 秒（防频繁触发）
   - 检测到变化 → 调 `HotUpdateMgr::LoadVersion()`

2. **修复 `HotUpdateMgr::Reload()`**
   - 当前返回 false，需改为保存当前加载路径
   - 允许"重新加载当前版本"用于测试

3. **GameService 集成**
   - `Init()` 末尾启动 `LuaHotWatcher`
   - 添加 `OnHotUpdateVersion(uint32_t version)` 钩子

4. **测试**
   - 修改 `combat.lua` 中的伤害公式 → 验证新伤害生效
   - 写入错误 Lua 语法 → 验证回滚到旧版本

**不改动**：
- ❌ 不改 Lua 脚本内容
- ❌ 不改 C++ 子系统接口
- ❌ 不改 VM 生命周期管理

### 4.3 Phase B：增强热更（中可行性，中等风险）

**目标**：让热更感知模块依赖关系，保证切换一致性。

**核心改动**：

1. **Lua 模块版本号**
   - 每个 `.lua` 脚本顶部声明 `M.VERSION = N`
   - `HotUpdateMgr` 维护版本号映射表

2. **依赖解析**
   - 解析每个 Lua 文件的 `require` 语句
   - 构建依赖 DAG（有向无环图）
   - 热更时按拓扑序重新加载

3. **C++ 绑定自动更新**
   - `LuaBridge` 提供 `RebindAll()` 方法
   - 热更 Commit 后自动重新 push 全局变量
   - 子系统通过 `weak_ptr<LuaBridge>` 避免悬空引用

4. **灰度发布**
   - `LoadVersion()` 支持传入 `canary_percentage`（金丝雀比例）
   - 部分玩家路由到新版本，观察后再全量切换

**改动影响面**：
- 6 个 Lua 脚本需添加版本号
- `LuaBridge` 新增 `RebindAll()`
- `HotUpdateMgr` 增加依赖解析逻辑（约 150 行）

### 4.4 Phase C：VM 级整体替换（低可行性评估，高价值）

**目标**：完全替换共享 VM 模式，每个 Game Service 持有独立可替换 VM。

**核心改动**：

1. **架构调整**
   ```
   当前：GameService 持有单 VM
     └── lua_bridge_ (LuaBridge)
           ├── monster_mgr_->SetLuaBridge(&lua_bridge_)
           └── combat_->SetLuaBridge(&lua_bridge_)

   目标：GameService 持有 shared_ptr<VM>
     └── vm_manager_ (shared_ptr<LuaVmManager>)
           ├── monster_mgr_->SetLuaVm(vm_manager_)  // weak_ptr 持有
           └── combat_->SetLuaVm(vm_manager_)       // weak_ptr 持有
   ```

2. **VM 替换原子性**
   - 新 VM 创建 → 加载所有脚本 → RebindAll()
   - 原子切换 `vm_manager_` shared_ptr
   - 旧 VM 自然 GC（无强引用后）

3. **多 Game Service 协调**
   - Master Service 下发 reload 指令
   - 各 Game Service 依次 reload（不并发，避免玩家跨服不一致）

**改动影响面**：
- `MonsterManager` / `CombatSystem` 接口需改造
- `GameService` 初始化逻辑重写
- 引入 `shared_ptr` 跨 VM 传递的复杂性

---

## 五、与 Skynet reloader 的核心差距总结

| 能力维度 | Skynet reloader | SNC 引入后（Phase A+B+C） | 差距 |
|---------|----------------|--------------------------|------|
| 触发方式 | mtime 自动检测 | mtime 自动检测 | ✅ 无差距 |
| 切换粒度 | VM 级（整个 Lua state） | VM 级（Phase C 后） | Phase A/B 有差距 |
| 状态隔离 | 天然隔离（每个 Service 独立 VM） | 共享 VM（Phase C 后改善） | Phase A/B 有差距 |
| 依赖管理 | require 版本号链 | 拓扑排序 + 版本号（Phase B） | ⚠️ 部分差距 |
| C++ 绑定 | C Service 自动重建 | 手动 RebindAll（Phase B） | ⚠️ 部分差距 |
| 灰度发布 | 通过 Watchdog 控制 | 需自定义实现（Phase B） | ⚠️ 部分差距 |
| 多服协调 | Harbor 间通信 | RPC 协调（Phase C） | ⚠️ 部分差距 |

---

## 六、风险与缓解

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| 热更期间 Lua GC 卡顿 | 🟡 中 | Phase C 用独立 VM，旧 VM 在 GC 安全点切换 |
| C++ 指针悬空（旧 VM 中持有了玩家的 Lua 引用） | 🔴 高 | Phase C 引入 `weak_ptr` 包装，切换前清理所有活跃引用 |
| 依赖链循环导致 reload 死锁 | 🟡 中 | 依赖解析阶段检测环，报错拒绝热更 |
| 双缓冲切换期间的消息丢失 | 🟢 低 | Commit 前 drain 旧 VM 消息队列 |
| 文件监控线程与主线程竞争 | 🟢 低 | watcher 线程只读 mtime，写操作通过消息队列传给主线程 |

---

## 七、结论与建议

### 7.1 总体判断

**可行性：✅ 高** — 核心热更机制可与现有 `HotUpdateMgr` 无缝衔接，主要工作量在于补齐文件监控和 C++ 绑定更新。

### 7.2 建议执行路径

| 优先级 | 阶段 | 预计工时 | 收益 |
|--------|------|---------|------|
| P0 | Phase A：基础热更 | 2 周 | 开发期无需重启即可测试 Lua 改动 |
| P1 | Phase B：增强热更 | 3 周 | 生产环境安全的数值调整热更 |
| P2 | Phase C：VM 级替换 | 4 周 | 彻底消除共享 VM 并发风险 |

### 7.3 不建议做的事情

- ❌ **不要一次性全面替换为 Skynet**（参考前一评估报告，性能代价过高）
- ❌ **不要修改现有 Lua 脚本语法**（Phase A 前保持兼容性）
- ❌ **不要在 Phase A 引入 require 版本号**（过早优化，增加迁移成本）

### 7.4 可借鉴的 Skynet 其他思路

除了 reloader，以下 Skynet 设计也值得引入 SNC：

| 思路 | 应用场景 | 改造难度 |
|------|---------|---------|
| **Watchdog 健康检查** | 自动重启崩溃的 Game Service | 🟢 低 |
| **CService 分离热点** | CombatSystem 核心计算移至 C | 🟡 中 |
| **LFS socket 封装** | 统一网络 API 风格 | 🟡 中（已有 EventLoop） |
| **Timer 精度优化** | 使用高精度定时器替代 std::map | 🟢 低 |
