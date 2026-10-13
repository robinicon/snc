# MMORPG 服务器架构审计报告

> 审计日期：2026-10-13
> 审计范围：mmorpg-server-design.md 设计稿 + SNC 框架源码对照分析

---

## 一、总体评价

设计文档在方向上是正确的，对 SNC 框架已有能力（ServiceRegistry、CircuitBreaker、WsGateway、JwtService）的利用也基本准确。但存在 **3 个高风险缺陷**、**5 个中风险遗漏** 和 **若干可优化项**，需要在编码前修复。

---

## 二、高风险缺陷（必须修复）

### H1：Master 服的健康检查逻辑与设计不符

**问题**：设计文档中 Master 的健康检查思路是"周期性轮询各服心跳"，但框架已有更简洁的机制——`ServiceRegistry::RefreshHealth()` 由外部调用刷新，服务节点自己上报心跳来保持健康。设计文档描述的"超时标记不健康"实际上与 `RefreshHealth()` 的工作方式矛盾。

**正确做法**：
```
每个 Game/Map/World 服独立启动一个心跳线程，每 N 秒调用 ServiceRegistry::Register() 刷新 healthy flag
Master 的 CheckLoop_ 不做"超时判死"，只做"统计 + 日志 + 告警"
不健康的节点由心跳线程自身的超时逻辑维护（心跳间隔 5s，超时 15s = 3 次丢失）
```

### H2：JWT 验证流程未考虑 Gate 无法直接调账号服的场景

**问题**：Gate 服连接时，需要验证客户端 JWT。设计文档没有说明 Gate 如何验证——是直接调 Account HTTP 接口，还是在本地缓存公钥？

- 若 Gate 调 Account HTTP：产生额外网络延迟，且账号服宕机则全服无法登录
- 若 Gate 本地验签：需要共享同一 HS256 密钥，需在设计中明确说明

**正确做法**：推荐 Gate 本地验签（HS256），密钥通过 Master 配置下发或预共享；Account 服只负责签发，不参与每次登录校验。

### H3：消息协议混用两条不同路径导致路由歧义

**问题**：设计文档同时定义了两种消息传递方式：

1. **HTTP 账号流程**（`POST /api/account/login`）→ 走 HttpServer，返回 JWT
2. **TCP 游戏流程**（`MsgId 0x1001 MSG_LOGIN_REQ`）→ 走 TcpConnection + Msg 协议

但 Gate 服的设计没有说清楚：**客户端先连 TCP，然后再调 HTTP 拿 JWT？** 还是先 HTTP 登录拿 token，再 TCP 连接时携带 token？

**时序澄清（必须是以下顺序）**：
```
1. 客户端 → HTTP POST /api/account/login → 拿到 JWT
2. 客户端 → TCP 连接 Gate (fd)
3. 客户端 → Gate 发送 MSG_AUTH (含 JWT)
4. Gate 本地验签 → 向 Master 查询 Game 服分配 → 转发认证结果
5. 认证成功 → Gate 建立持久 RPC 连接到指定 Game 服 → 后续业务消息路由
```

---

## 三、中风险遗漏（强烈建议补充）

### M1：缺少服务优雅停机（Graceful Shutdown）机制

**问题**：设计文档完全没有提 shutdown 流程。当 Master 或某个 Game 服重启时，Gate 持有的 RpcClient 连接会突然断开，导致在线玩家被强制踢出。

**建议补充**：
```
1. Master 收到 SIGTERM/SIGINT → 先 MarkUnhealthy("game") → 通知所有 Game 服
2. Game 服收到关闭信号 → 停止接受新连接 → 等待现有玩家退出（最多 30s）
3. Game 服向 Master 发送 MSG_SERVICE_UNREGISTER → 退出 RPC 监听
4. Master 确认注销后，触发 Gate 清理该 Game 服相关 session
```

### M2：Map 服与 Game 服的关系未明确，存在实现歧义

**问题**：设计文档"推荐方案 B"（合并），但章节八又单独设计了 Map 服。这会导致实现时不知道该写一个类还是两个进程。

**明确建议**：
```
阶段一（开发）：Game 服内置 Map 功能，不独立进程。
                代码结构：GameService 内部持有 MapManager 对象
阶段二（扩展）：Map 独立进程，通过 RPC 与 Game 服通信
                此时需要新增 MSG_MAP_ENTER / MSG_MAP_EXIT 消息
```

### M3：无跨服数据一致性保障

**问题**：World 服仅"预留接口"，但没有定义任何跨服通信的保证机制。跨服战场需要多个 Game 服同时参与，如果其中一个 Game 服崩溃，其他服的进程怎么处理？

**建议**：
```
在 World 服增加"场景锁"机制：
- 创建跨服场景时，World 向参与的每个 Game 服发送 MSG_SCENE_BEGIN
- Game 服返回确认后，World 开始同步
- 任意 Game 服掉线 → World 标记该服玩家为"离线"，场景继续运行至结束
- 场景结束时，World 向所有 Game 服发送 MSG_SCENE_END
```

### M4：GM 命令路径设计缺陷

**问题**：设计文档 GM 命令路径是 `HTTP → Master → RPC → Game服`，但 Master 同时是 RPC 服务端和 HTTP 服务端，两者各自有独立事件循环，可能冲突。

**正确路径**：
```
GM 命令走两条独立通道：
1. HTTP 通道（AdminService）：运维人员通过浏览器/工具直接调 Master HTTP API
   GET /api/gm/cmd?cmd=give_item&player_id=1001&item_id=10001&count=10
2. RPC 通道（内部）：Game 服主动上报状态，Master 通过 RpcClient 反向推命令
   两条通道在 Master 内部都注册到同一个 Handler，只共享业务逻辑
```

### M5：Gate 无会话过期清理

**问题**：Gate 的 `fd_to_game_` 映射表没有清理逻辑。如果客户端异常断开（非正常关闭），fd 可能长期占用，造成内存泄漏和路由错乱。

**建议**：
```
Gate 在 TcpConnection 的 CloseCallback 中：
  OnClientDisconnected_(int32_t fd) {
      // 1. 查找对应的 game_server
      // 2. 清理 fd_to_game_[fd]
      // 3. 通知 Game 服该玩家已断开（发送 MSG_PLAYER_DISCONNECT）
  }
```

---

## 四、低风险优化项

| # | 位置 | 问题 | 建议 |
|---|------|------|------|
| O1 | 端口规划 | 所有端口硬编码，多实例部署会冲突 | 改为配置文件，支持环境变量覆盖 |
| O2 | 账号服 | 使用 StubDatabase，无持久化 | 开发环境可以接受，注明"上线前替换为 MySQL" |
| O3 | Master | HealthCheckLoop_ 与心跳冲突 | 合并：心跳线程自己维护 last_heartbeat_ms，CheckLoop_ 只做日志 |
| O4 | Game 服 | Actor 系统只设计了 PlayerActor | 补充 NPC Actor、怪物 Actor 基类，方便扩展 |
| O5 | World 服 | 完全空壳 | 至少实现跨服广播路由，而非仅"预留" |
| O6 | 整体 | 无版本协议 | 客户端版本号与服务器版本不一致时拒绝连接（防止客户端回退） |

---

## 五、框架能力与设计的对照表

| 设计需求 | 对应框架能力 | 状态 | 备注 |
|----------|-------------|------|------|
| HTTP 账号注册/登录 | `HttpServer` + `StubDatabase` + `JwtService` | ✅ 可直接使用 | 零新增依赖 |
| JWT 本地验签 | `auth::JwtService::Verify()` | ✅ 可直接使用 | Gate 需持有相同 secret |
| 服务注册/发现 | `ServiceRegistry`（进程内）/ `RedisServiceRegistry`（跨进程） | ⚠️ 需选型 | 开发环境用进程内，生产用 Redis |
| 负载均衡 | `ClusterGateway` + `ServiceRegistry::Pick()` | ✅ 可直接使用 | RoundRobin 默认 |
| 熔断保护 | `CircuitBreaker` | ✅ 可直接使用 | Gate→Game 路径需加熔断 |
| TCP 接入+心跳 | `TcpConnection`（自带 ping/pong + 背压） | ✅ 可直接使用 | Gate 直接用 TcpConnection |
| WebSocket 接入 | `WsGateway` + `IGatewayBridge` | ✅ 可直接使用 | 已有完整实现，直接继承接口 |
| 跨服 RPC | `RpcServer` + `RpcClient` + 自动重连 | ✅ 可直接使用 | RpcClient 已有指数退避重连 |
| 消息队列/背压 | `MessageQueue` + `WorkerPool` | ⚠️ 需接入 | Gate 接收消息后入队，Worker 线程消费 |
| 服务状态 HTTP 查询 | `AdminService` | ✅ 可直接使用 | Master 复用 |
| 跨进程服务注册 | `RedisServiceRegistry`（需 snc_redis） | ⚠️ 需 Redis | 开发环境可用进程内替代 |

---

## 六、优化后的架构时序图

```
┌─────────┐     HTTP POST login      ┌──────────┐
│ 客户端   │ ──────────────────────►  │ Account  │
│         │                          │  (HTTP)  │
│         │ ◄──────────────────────  │          │
│  (JWT)  │    200 {token, user_id}  │          │
└────┬────┘                          └──────────┘
     │
     │ TCP connect
     ▼
┌─────────┐     MSG_AUTH(jwt)        ┌──────────┐
│  Gate    │ ──────────────────────►  │  Master  │
│ (TCP:22000)                          │ (RPC:20000)
│         │ ◄──────────────────────  │          │
│         │  200 {game_addr:port}    │          │
└────┬────┘                          └────┬─────┘
     │                                    │
     │ RPC establish                     │ 注册+心跳(5s)
     ▼                                    ▼
┌─────────┐                          ┌──────────┐
│  Game    │ ◄─────────────────────  │  Master  │
│ (RPC:23000)   MSG_LOGIN_CONFIRM   │          │
│         │                          └──────────┘
└─────────┘
     │
     │ 业务消息路由（后续所有 Msg 经 Gate → Game）
```

---

## 七、修改后的目录结构（更新版）

```
snc/
├── include/snc/mmorpg/
│   ├── AccountService.h   ← 不变
│   ├── MasterService.h    ← 更新：心跳线程合并，去掉重复 CheckLoop
│   ├── GateService.h      ← 更新：补 fd 清理回调、本地 JWT 验签
│   ├── GameService.h      ← 更新：Game+Map 合并实现
│   ├── MapManager.h       ← 新增：MapManager 类（Game 服内部）
│   └── WorldService.h     ← 更新：增加跨服场景管理框架
│
├── examples/
│   ├── example_01_account.cpp   ← 不变
│   ├── example_02_master.cpp    ← 更新：合并心跳与检测
│   ├── example_03_game.cpp      ← 更新：含 MapManager
│   ├── example_04_map.cpp       ← 删除（功能并入 example_03）
│   ├── example_05_world.cpp     ← 更新：增加场景锁
│   ├── example_06_gate.cpp      ← 更新：补 fd 清理 + JWT 本地验签
│   └── example_07_mmorpg.cpp    ← 更新：单体多进程版
│
├── docs/design/
│   ├── mmorpg-server-design.md  ← 主文档（上述版本）
│   └── mmorpg-architecture-audit.md  ← 本文档
│
└── CMakeLists.txt
    └── examples/CMakeLists.txt  ← 删除 example_04_map 条目
```

---

## 八、实施计划修订（基于审计结论）

### 阶段一：账号服 + Master 服（修正版）
- [ ] `example_01_account.cpp` — HTTP 注册/登录 + JWT 签发
- [ ] `example_02_master.cpp` — **心跳线程统一**：每个服独立心跳，Master 只做统计和 HTTP 查询
- [ ] 单元测试：心跳超时判定、服务注销

### 阶段二：Gate 服（修正版）
- [ ] `example_06_gate.cpp` — **补全 fd 清理回调** + 本地 JWT 验签 + MsgRouter 路由表
- [ ] 测试：异常断连后 fd_to_game_ 映射清理

### 阶段三：Game 服（修正版）
- [ ] `example_03_game.cpp` — **含 MapManager**，不分独立进程
- [ ] `MapManager.h` — 地图实例管理、玩家传送、场景广播
- [ ] 单元测试：玩家进出地图、地图内广播

### 阶段四：World 服 + 完整集成
- [ ] `example_05_world.cpp` — 跨服场景锁（创建/加入/结束）
- [ ] `example_07_mmorpg.cpp` — 单体多进程启动脚本
- [ ] 端到端测试：登录 → 进入游戏 → 聊天 → 副本 → GM 命令 → 退出

---

## 九、关键技术决策记录（ADR）

| ADR | 决策 | 理由 |
|-----|------|------|
| ADR-001 | Gate 本地验签 JWT，不实时调账号服 | 降低登录延迟，避免账号服单点故障 |
| ADR-002 | 开发环境 Map 合并到 Game 服，不独立进程 | 减少进程数，降低调试复杂度 |
| ADR-003 | 服务注册使用进程内 ServiceRegistry（开发）/ RedisServiceRegistry（生产） | 零依赖启动，平滑迁移 |
| ADR-004 | Master 不做超时判死，只做心跳统计 | 服务自己维护健康状态，Master 只观察 |
| ADR-005 | 异常断连由 Gate CloseCallback 清理 fd 映射 | TcpConnection 已内置 DisconnectReason，无需额外机制 |
