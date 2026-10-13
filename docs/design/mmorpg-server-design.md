# MMORPG 服务器整体结构设计文档（审计修订版）

> **版本**：v2.0
> **日期**：2026-10-13
> **变更记录**：v2.0 采纳架构审计报告全部建议，详见 [mmorpg-architecture-audit.md](./mmorpg-architecture-audit.md)

---

## 一、架构评估

### 1.1 总体思路评估

| 服务 | 职责 | 合理性 | 评估 |
|------|------|--------|------|
| **账号服 (account)** | HTTP 注册/登录，返回 JWT | ✅ 合理 | 直接利用现有 `snc_http` + `snc_auth` 模块 |
| **Master 服** | 服务注册中心 + GM 命令分发 | ✅ 合理 | 复用 `ServiceRegistry`，心跳由各服自持，Master 只做统计观察 |
| **World 服** | 跨服路由 + 跨服场景同步 | ⚠️ 需细化 | 增加场景锁机制（SCENE_BEGIN/END），而非仅"预留" |
| **Game 服** | 玩家 Actor + 地图管理 + 战斗逻辑 | ✅ 合理 | MapManager 作为内部模块，不独立进程 |
| **Map 服** | 副本/场景管理 | ✅ 已合并 | 开发阶段合并进 Game 服，见 ADR-002 |
| **Gate 服** | TCP/WS 接入 + 消息路由 + JWT 本地验签 | ✅ 合理 | 复用 `TcpConnection` 心跳 + 背压，本地验签 |

### 1.2 关键技术决策（ADR）

| ADR | 决策 | 理由 |
|-----|------|------|
| ADR-001 | Gate 本地验签 JWT，不实时调账号服 | 降低登录延迟，避免账号服宕机时全服无法登录 |
| ADR-002 | 开发阶段 Map 合并进 Game 服，不独立进程 | 减少进程数，降低调试复杂度；高并发时再拆分 |
| ADR-003 | 服务注册使用进程内 `ServiceRegistry`（开发）/ `RedisServiceRegistry`（生产） | 零依赖启动，平滑迁移至 Redis |
| ADR-004 | Master 不做超时判死，只做心跳统计 | 各服自持心跳线程维护健康状态，Master 只观察 |
| ADR-005 | 异常断连由 Gate CloseCallback 清理 fd 映射 | TcpConnection 内置 `DisconnectReason`，无需额外机制 |

### 1.3 最终落地方案

```
客户端
  │  HTTP POST /api/account/login
  ▼
┌──────────┐     返回 { token, user_id }
│ Account  │
│ (HTTP)   │
└────┬─────┘
     │ TCP connect + MSG_AUTH(token)
     ▼
┌──────────┐   RPC 注册/心跳(5s)   ┌──────────┐
│   Gate   │◄─────────────────────►│  Master  │
│ (TCP:22k) │                       │ (RPC:20k)│
└────┬─────┘                       └──────────┘
     │ 验证JWT → 分配Game → 建立持久RPC
     ▼
┌──────────┐    RPC 业务消息         ┌──────────┐
│  Game    │◄───────────────────────►│  World   │
│  (RPC:23k)│ 含内部 MapManager       │ (RPC:25k)│
└──────────┘                         └──────────┘
```

> **说明**：Game 服内部持有 `MapManager` 对象管理地图实例，不独立进程。World 服负责跨服场景锁（SCENE_BEGIN / SCENE_END）。

---

## 二、完整通信时序

```
[阶段 1] 账号登录（HTTP）
  客户端 ──POST /api/account/login──► Account
  Account ◄──{token, user_id}──────  客户端

[阶段 2] 游戏接入（TCP + JWT 验签）
  客户端 ──TCP connect─────────────► Gate
  客户端 ──MSG_AUTH(token)─────────► Gate
  Gate ──[本地HS256验签]──────────── 通过
  Gate ──RPC MSG_LOGIN_REQ────────► Master
  Master ◄──{game_addr:port}──────  Gate
  Gate ──建立持久RPC连接───────────► Game
  Gate ◄──MSG_LOGIN_CONFIRM───────  Game

[阶段 3] 业务消息路由
  客户端 ──MSG_CHAT/MOVE/...─────► Gate ──RPC─────────► Game

[阶段 4] 服务心跳（各服自持）
  Game 每5s ──RPC心跳────────────► Master
  Master 仅统计日志，不做超时判死
```

---

## 三、协议规划

### 3.1 消息 ID 分配表

| MsgId 范围 | 方向 | 用途 |
|------------|------|------|
| `0x1000–0x100F` | Client → Gate | 登录握手（AUTH/LOGIN_RSP） |
| `0x1100–0x11FF` | Client → Gate | 玩家移动/动作 |
| `0x1200–0x12FF` | Client → Gate | 聊天/系统消息 |
| `0x2000–0x20FF` | Gate → Game | 登录后会话建立、玩家消息转发 |
| `0x3000–0x30FF` | Server → Master | 心跳（0x3000） |
| `0x3100–0x31FF` | Server ↔ Master | 注册(0x3101)/注销(0x3102) |
| `0x3200–0x32FF` | Master → Server | GM 命令(0x3200)/结果(0x3201) |
| `0x3300–0x33FF` | Server ↔ Server | 玩家断连通知(0x3300) |
| `0x3400–0x34FF` | Game ↔ World | 跨服场景 BEGIN(0x3401)/END(0x3402) |
| `0x4000–0x40FF` | Broadcast | 全局公告/场景广播 |

### 3.2 关键消息定义

#### 3.2.1 账号 HTTP 接口

```
POST /api/account/login
Content-Type: application/json
{ "username": "player001", "password": "secret123" }

Response 200:
{ "token": "eyJ...", "user_id": 10001, "message": "login_ok" }
// 账号不存在则自动创建

POST /api/account/register
{ "username": "new_player", "password": "secret456" }
// 已存在则直接执行登录，返回相同 token
```

#### 3.2.2 客户端握手消息（MsgId 0x1000/0x1001）

```
MSG_AUTH (0x1000):
  payload: { token: string, client_ver: string }
  // Gate 本地 HS256 验签，不回调 Account 服

MSG_AUTH_RSP (0x1001):
  payload: {
    err_code: int32,       // 0=成功
    game_server_addr: string,
    game_server_port: int32
  }
```

#### 3.2.3 服务注册与心跳（MsgId 0x3100/0x3101/0x3000）

```
MSG_SERVICE_REGISTER (0x3101):
  // 服务启动时发送一次
  payload: { service_name: string, host: string, port: int32, weight: int32 }

MSG_SERVICE_UNREGISTER (0x3102):
  // 服务关闭时发送一次（优雅停机）
  payload: { service_name: string, host: string, port: int32 }

MSG_HEARTBEAT (0x3000):
  // 每 5s 发送，刷新 healthy flag
  payload: {
    service_name: string,
    uptime_ms: int64,
    player_count: int32
  }
  // 注意：3 次心跳丢失（15s）后，ServiceRegistry 内部 healthy 自动置 false
```

#### 3.2.4 GM 命令（MsgId 0x3200/0x3201）

```
MSG_GM_COMMAND (0x3200):
  // 通过 HTTP Admin API 触发，Master 通过 RpcClient 推送到目标服
  payload: {
    cmd: string,           // "give_item"/"teleport"/"kill_all"
    target_service: string, // "game"/"all"
    target_addr: string,    // 空则广播到所有实例
    params: dict
  }

MSG_GM_RESULT (0x3201):
  payload: { cmd: string, success: bool, result: string }
```

#### 3.2.5 跨服场景消息（MsgId 0x3401/0x3402）

```
MSG_SCENE_BEGIN (0x3401):
  // World → Game：跨服场景开始
  payload: { scene_id: int64, scene_type: string, participating_games: [string] }

MSG_SCENE_END (0x3402):
  // World → Game：跨服场景结束
  payload: { scene_id: int64, result: string }

MSG_SCENE_PLAYER_JOIN (0x3403):
  // Game → World：玩家进入跨服场景
  payload: { scene_id: int64, player_id: int64, game_addr: string }
```

---

## 四、目录结构

```
snc/
├── include/snc/mmorpg/                 ← MMORPG 专用头文件（新建）
│   ├── AccountService.h                ← 账号服业务逻辑（JWT签发 + 内存账号库）
│   ├── MasterService.h                 ← Master 服务管理（心跳统计 + GM分发）
│   ├── GateService.h                   ← Gate 路由（fd映射 + JWT本地验签 + 断连清理）
│   ├── GameService.h                   ← 游戏主逻辑（Actor + 玩家 + MapManager）
│   ├── MapManager.h                    ← 地图实例管理（含传送/广播）
│   └── WorldService.h                  ← 跨服场景管理（场景锁/匹配）
│
├── examples/                           ← 各服示例（新建）
│   ├── example_01_account.cpp          ← 账号服（HTTP 注册/登录）
│   ├── example_02_master.cpp           ← Master 服（服务管理 + GM）
│   ├── example_03_game.cpp             ← Game 服（含 MapManager）
│   ├── example_04_gate.cpp             ← Gate 服（TCP接入 + 路由 + JWT验签）
│   ├── example_05_world.cpp            ← World 服（跨服场景锁）
│   └── example_06_mmorpg.cpp           ← 完整多进程启动示例
│
├── docs/
│   ├── design/
│   │   ├── mmorpg-server-design.md     ← 本文档
│   │   └── mmorpg-architecture-audit.md← 架构审计报告
│   └── tutorials/
│       ├── 08-account-server.md
│       ├── 09-master-server.md
│       ├── 10-game-server.md
│       ├── 11-gate-server.md
│       └── 12-mmorpg-full.md
│
├── CMakeLists.txt                      ← 新增 examples 子目录配置
└── ...（现有文件不变）
```

> **变更说明**：原计划 `example_04_map.cpp` 已合并进 `example_03_game.cpp`，Map 功能由 `MapManager.h` 提供，不再作为独立进程示例。

---

## 五、账号服实现（Account Service）

### 5.1 依赖

- `snc_http::HttpServer` — HTTP 服务框架
- `snc_db::StubDatabase` — 内存账号存储（开发环境；生产替换为 MySQL）
- `snc_auth::JwtService` — JWT 签发（HS256）

### 5.2 核心流程

```
POST /api/account/login {username, password}
  │
  ├─ 账号不存在 → INSERT accounts → 自动生成 user_id → 签发 JWT → 返回 {token, user_id}
  └─ 账号存在   → SELECT 密码比对 → 签发 JWT → 返回 {token, user_id}
```

### 5.3 代码框架（AccountService.h）

```cpp
#pragma once
#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/db/Database.h"

namespace snc { namespace mmorpg {

class AccountService {
public:
    struct Config {
        std::string jwt_secret = "snc-mmorpg-dev-secret-key-32bytes!!";  // 与 Gate 共享
        auth::JwtConfig jwt_config;
        int32_t http_port = 18080;
    };

    explicit AccountService(const Config& cfg);
    void Run();
    void Stop();
    size_t OnlineCount() const;

private:
    HttpResponse HandleLogin_(const HttpRequest& req);
    HttpResponse HandleRegister_(const HttpRequest& req);
    HttpResponse HandleStatus_(const HttpRequest& req);

    Config config_;
    std::unique_ptr<auth::JwtService> jwt_svc_;
    std::shared_ptr<db::IDatabase> db_;
    std::unique_ptr<http::HttpServer> http_server_;
};

}} // namespace snc::mmorpg
```

---

## 六、Master 服实现（Master Service）

### 6.1 职责

1. **服务注册中心**：维护所有 Game/World/Gate 服务的 `ServiceEntry`
2. **心跳统计**：接收各服心跳，更新 `last_heartbeat_ms`，输出健康状态日志（不做超时判死）
3. **GM 命令分发**：通过 HTTP Admin API 接收 GM 指令，通过 `RpcClient` 推送到目标 Game 服
4. **服务状态 HTTP 查询**：`GET /api/master/services` 返回当前在线服务列表

### 6.2 核心数据结构

```cpp
// ServiceNode 由 ServiceRegistry 内部维护，外部通过 registry_->GetNodes("game") 访问
// Master 需要追踪的额外信息：
struct ServiceStats {
    int64_t  last_heartbeat_ms = 0;
    int32_t  player_count = 0;
    int32_t  consecutive_missed = 0;  // 连续丢失心跳次数（用于日志告警）
};
```

### 6.3 心跳与检测的统一设计（ADR-004）

```
每个 Game/World 服：
  ┌─────────────────────────────────────┐
  │ HeartbeatLoop_() 每 5s：            │
  │   1. master_client_->Send(HEARTBEAT) │
  │   2. ServiceRegistry::Register()    │  ← 刷新 healthy flag
  │                                      │
  Master：
  ┌─────────────────────────────────────┐
  │ HealthStatisticsLoop_() 每 10s：     │
  │   1. 遍历 registry_ 所有节点         │
  │   2. 计算当前健康比例                 │
  │   3. 输出日志： healthy=N/total      │
  │   4. 标记 consecutive_missed ≥ 3 的  │
  │      节点为 ALERT 级别               │
  │   ★ 不做超时无视或强制下线            │
  └─────────────────────────────────────┘
```

### 6.4 代码框架（MasterService.h）

```cpp
#pragma once
#include "snc/discovery/ServiceRegistry.h"
#include "snc/rpc/RpcServer.h"
#include "snc/http/HttpServer.h"
#include "snc/admin/AdminService.h"

namespace snc { namespace mmorpg {

class MasterService {
public:
    struct Config {
        int32_t rpc_port = 20000;
        int32_t http_port = 21000;
        int32_t heartbeat_interval_ms = 5000;
        int32_t stats_interval_ms = 10000;
    };

    explicit MasterService(const Config& cfg);
    void Run();
    void Stop();

    std::vector<std::string> ListServices() const;
    std::vector<discovery::ServiceEntryPtr> GetHealthyNodes(const std::string& name) const;

private:
    // RPC 处理
    void HandleRegister_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandleUnregister_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandleHeartbeat_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandleGmCommand_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);

    // HTTP Admin
    HttpResponse HandleStatus_(const HttpRequest& req);

    // GM 分发：通过 RpcClient 推送到目标服务
    void DispatchGmCommand_(const std::string& cmd, const std::string& target_addr,
                            const std::string& params);

    // 心跳统计（只做日志输出，不修改 healthy）
    void HealthStatisticsLoop_();

    Config config_;
    discovery::ServiceRegistry::Ptr registry_;
    rpc::RpcServer::Ptr rpc_server_;
    std::unique_ptr<http::HttpServer> http_server_;
    std::thread stats_thread_;
    std::atomic<bool> running_{false};
};

}} // namespace snc::mmorpg
```

---

## 七、Gate 服实现（Gate Service）

### 7.1 职责

1. 接收客户端 TCP/WebSocket 连接
2. **本地验证 JWT**（不回调账号服）
3. 向 Master 请求 Game 服分配，建立持久 RPC 会话
4. 维护 `client_fd → game_server_addr` 映射
5. **异常断连时清理映射并通知 Game 服**（M5）

### 7.2 关键代码框架（GateService.h）

```cpp
#pragma once
#include "snc/core/TcpConnection.h"
#include "snc/gateway/ClusterGateway.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"

namespace snc { namespace mmorpg {

class GateService {
public:
    struct Config {
        int32_t    tcp_port   = 22000;
        int32_t    ws_port    = 22001;
        int32_t    master_rpc_port = 20000;
        std::string master_host = "127.0.0.1";
        std::string jwt_secret  = "snc-mmorpg-dev-secret-key-32bytes!!"; // 与 Account 共享
    };

    explicit GateService(const Config& cfg);
    void Run();
    void Stop();

private:
    // TcpConnection 回调
    void OnConnected_(int32_t fd);
    void OnMessage_(int32_t fd, core::Msg::Ptr msg);
    void OnDisconnected_(int32_t fd, core::TcpConnection::DisconnectReason reason);
    // 注意：OnDisconnected_ 中必须清理 fd_to_game_ 并通知 Game 服（M5）

    // 消息路由
    void HandleAuth_(int32_t fd, core::Msg::Ptr msg);   // MSG_AUTH (0x1000)
    void RouteToGame_(int32_t fd, core::Msg::Ptr msg);
    void NotifyGameDisconnect_(int32_t fd);

    // 选择 Game 服（RoundRobin via ServiceRegistry::Pick）
    std::string PickGameServer_();

    Config config_;
    discovery::ServiceRegistry::Ptr registry_;
    std::unique_ptr<auth::JwtService> jwt_svc_;
    std::unique_ptr<rpc::RpcClient> master_client_;

    // fd → game_server_addr 映射（OnDisconnected_ 时清理）
    std::mutex fd_mutex_;
    std::unordered_map<int32_t, std::string> fd_to_game_;
};

}} // namespace snc::mmorpg
```

### 7.3 Gate 断连清理流程（M5）

```cpp
void GateService::OnDisconnected_(int32_t fd, core::TcpConnection::DisconnectReason reason) {
    // 1. 清理映射
    std::string game_addr;
    {
        std::lock_guard<std::mutex> lock(fd_mutex_);
        auto it = fd_to_game_.find(fd);
        if (it != fd_to_game_.end()) {
            game_addr = it->second;
            fd_to_game_.erase(it);
        }
    }
    // 2. 通知 Game 服该玩家已断开
    if (!game_addr.empty()) {
        NotifyGameDisconnect_(fd);  // 发送 MSG_PLAYER_DISCONNECT (0x3300)
    }
}
```

---

## 八、Game 服实现（Game Service）

### 8.1 职责

1. 管理玩家 Actor（每个玩家一个 PlayerActor）
2. 处理本地地图逻辑（移动/战斗/聊天）— **通过内部 MapManager**
3. 处理副本进入/离开（MapManager 管理实例生命周期）
4. 定期向 Master 发送心跳

### 8.2 Actor 系统

```cpp
class Actor {
public:
    virtual ~Actor() = default;
    virtual void OnMessage(core::Msg::Ptr msg) = 0;
    virtual void Tick(double dt) {}
};

class PlayerActor : public Actor {
public:
    int64_t    player_id;
    std::string nickname;
    int32_t    map_id;
    float      pos_x = 0.0f, pos_y = 0.0f;
    int64_t    connect_time_ms = 0;

    void OnMessage(core::Msg::Ptr msg) override;
    void Tick(double dt) override;
};

// 扩展点：后续可增加 NPCActor、MonsterActor
```

### 8.3 MapManager（合并进 Game 服，ADR-002）

```cpp
class MapManager {
public:
    struct MapInstance {
        int32_t       map_id;
        std::string   map_name;
        float         width = 100.0f, height = 100.0f;
        std::unordered_map<int64_t, std::pair<float,float>> players_;  // id → (x,y)
    };

    void Initialize();
    void EnterMap(int64_t player_id, int32_t map_id);
    void LeaveMap(int64_t player_id);
    void Teleport(int64_t player_id, int32_t map_id, float x, float y);
    void BroadcastInMap(int32_t map_id, core::Msg::Ptr msg);

private:
    std::unordered_map<int32_t, MapInstance> maps_;
    std::mutex mutex_;
};
```

### 8.4 代码框架（GameService.h）

```cpp
#pragma once
#include "snc/rpc/RpcServer.h"
#include "snc/discovery/ServiceRegistry.h"

namespace snc { namespace mmorpg {

class MapManager;  // 前向声明

class GameService {
public:
    struct Config {
        int32_t    rpc_port    = 23000;
        int32_t    master_rpc_port = 20000;
        std::string master_host = "127.0.0.1";
    };

    explicit GameService(const Config& cfg);
    void Run();
    void Stop();
    size_t PlayerCount() const;
    MapManager& MapManager();  // 供测试/调试访问

private:
    void HandleLoginConfirm_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandlePlayerMsg_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandleGmCommand_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandlePlayerDisconnect_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);

    void RegisterWithMaster_();
    void HeartbeatLoop_();          // 每5s调用 ServiceRegistry::Register() 刷新健康状态
    void GracefulShutdown_();       // 优雅停机：停止接受新玩家 → 等现有玩家退出 → 注销

    Config config_;
    rpc::RpcServer::Ptr rpc_server_;
    discovery::ServiceRegistry::Ptr master_registry_;
    std::unique_ptr<rpc::RpcClient> master_client_;
    std::unique_ptr<MapManager> map_mgr_;

    std::mutex player_mutex_;
    std::unordered_map<int64_t, std::shared_ptr<PlayerActor>> players_;
    std::atomic<int64_t> next_player_id_{1};
    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
};

}} // namespace snc::mmorpg
```

---

## 九、World 服实现（World Service）

### 9.1 职责（采纳 M3，增加场景锁）

1. **跨服场景锁**：创建跨服战场/活动，协调多个 Game 服
2. **跨服广播**：跨服聊天、跨服公告
3. **场景生命周期管理**：BEGIN → 各服参与 → END

### 9.2 场景锁状态机

```
  [IDLE]
    │ MSG_SCENE_CREATE (来自某 Game 服)
    ▼
  [CREATING] ── 向所有参与 Game 服发 MSG_SCENE_BEGIN ──►
    │ Game 服返回 ACK
    ▼
  [ACTIVE] ── 任意 Game 服掉线 → 标记该服玩家 offline，场景继续 ──►
    │ 所有玩家退出 或 超时
    ▼
  [ENDING] ── 向所有参与 Game 服发 MSG_SCENE_END ──►
    │
    ▼
  [IDLE]
```

### 9.3 代码框架（WorldService.h）

```cpp
#pragma once
#include "snc/rpc/RpcServer.h"
#include "snc/discovery/ServiceRegistry.h"

namespace snc { namespace mmorpg {

class WorldService {
public:
    struct Config {
        int32_t    rpc_port   = 25000;
        int32_t    master_rpc_port = 20000;
        std::string master_host = "127.0.0.1";
        int32_t    scene_timeout_ms = 300000;  // 场景最长 5 分钟
    };

    explicit WorldService(const Config& cfg);
    void Run();
    void Stop();

private:
    void HandleSceneCreate_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandleSceneAck_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandleScenePlayerJoin_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);
    void HandlePlayerDisconnectFromGame_(IConnection::Ptr conn, RpcSession::Ptr session, core::Msg::Ptr msg);

    void RegisterWithMaster_();
    void HeartbeatLoop_();
    void SceneTimeoutLoop_();

    Config config_;
    rpc::RpcServer::Ptr rpc_server_;
    discovery::ServiceRegistry::Ptr master_registry_;
    std::unique_ptr<rpc::RpcClient> master_client_;

    // 活跃场景管理
    std::mutex scene_mutex_;
    std::unordered_map<int64_t, struct SceneContext> active_scenes_;
    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
    std::thread timeout_thread_;
};

}} // namespace snc::mmorpg
```

---

## 十、优雅停机流程（M1）

```
[Master 收到 SIGTERM]
  1. master.Stop() → 停止 RPC 监听
  2. 遍历 registry_，对所有节点调用 MarkUnhealthy()
  3. 等待所有 RPC 会话清理（最多 5s）
  4. 输出日志 → exit(0)

[Game 服收到 SIGTERM]
  1. running_ = false → 停止心跳线程
  2. GracefulShutdown_()：
     a. 向 Gate 广播 MSG_GRACEFUL_SHUTDOWN（通知不再接受新玩家）
     b. 记录当前玩家数 N，启动倒计时（最多 30s）
     c. 每 1s 检查一次玩家数，当 players_.empty() 或超时则继续
  3. 向 Master 发送 MSG_SERVICE_UNREGISTER
  4. 停止 RpcServer → exit(0)

[Gate 服感知 Game 服 RPC 断开]
  1. 遍历 fd_to_game_，找出指向该 Game 服的所有 fd
  2. 对每个 fd 调用 OnDisconnected_(fd, DisconnectReason::RemoteClose)
  3. 触发对应 Game 服的 HandlePlayerDisconnect_ 流程
```

---

## 十一、CMake 配置

在根 `CMakeLists.txt` 追加：

```cmake
# ── MMORPG 示例程序 ────────────────────────────────────────────
option(SNC_BUILD_MMORPG_EXAMPLES "Build MMORPG example programs" ON)

if(SNC_BUILD_MMORPG_EXAMPLES)
    set(SNC_ENABLE_HTTP ON CACHE BOOL "" FORCE)
    set(SNC_ENABLE_AUTH ON CACHE BOOL "" FORCE)
    set(SNC_ENABLE_ADMIN ON CACHE BOOL "" FORCE)
    set(SNC_ENABLE_WEBSOCKET ON CACHE BOOL "" FORCE)
    add_subdirectory(examples)
endif()
```

`examples/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20)

add_executable(example_01_account example_01_account.cpp)
target_link_libraries(example_01_account PRIVATE
    snc_http snc_auth snc_db snc_base snc_core)

add_executable(example_02_master example_02_master.cpp)
target_link_libraries(example_02_master PRIVATE
    snc_rpc snc_discovery snc_http snc_admin snc_base snc_core)

add_executable(example_03_game example_03_game.cpp)
target_link_libraries(example_03_game PRIVATE
    snc_rpc snc_discovery snc_base snc_core)

add_executable(example_04_gate example_04_gate.cpp)
target_link_libraries(example_04_gate PRIVATE
    snc_rpc snc_gateway snc_discovery snc_auth snc_base snc_core)

add_executable(example_05_world example_05_world.cpp)
target_link_libraries(example_05_world PRIVATE
    snc_rpc snc_discovery snc_base snc_core)

add_executable(example_06_mmorpg example_06_mmorpg.cpp)
target_link_libraries(example_06_mmorpg PRIVATE
    snc_rpc snc_discovery snc_http snc_auth snc_gateway snc_db snc_base snc_core)
```

---

## 十二、端口分配

| 服务 | 协议 | 端口 | 说明 |
|------|------|------|------|
| Account | HTTP | 18080 | 账号注册/登录 |
| Master | RPC | 20000 | 服务注册/心跳 |
| Master | HTTP | 21000 | Admin 面板 / GM 命令 |
| Gate | TCP | 22000 | 客户端 TCP 接入 |
| Gate | WS | 22001 | 客户端 WebSocket 接入 |
| Game | RPC | 23000 | 游戏逻辑 + 地图管理 |
| World | RPC | 25000 | 跨服场景 |

---

## 十三、框架能力对照（更新版）

| 设计需求 | 对应框架能力 | 状态 | 备注 |
|----------|-------------|------|------|
| HTTP 账号注册/登录 | `HttpServer` + `StubDatabase` + `JwtService` | ✅ | 零新增依赖 |
| JWT 本地验签 | `auth::JwtService::Verify()` | ✅ | Gate 与 Account 共享同一 secret |
| 服务注册/发现 | `ServiceRegistry`（进程内开发）/ `RedisServiceRegistry`（生产） | ✅ | 两者接口一致，可替换 |
| 负载均衡 | `ClusterGateway` + `ServiceRegistry::Pick()` | ✅ | RoundRobin 默认 |
| 熔断保护 | `CircuitBreaker` | ✅ | Gate→Game 路径启用 |
| TCP 接入+心跳+背压 | `TcpConnection` | ✅ | 内置，零代码 |
| WebSocket 接入 | `WsGateway` + `IGatewayBridge` | ✅ | 已有完整实现 |
| 跨服 RPC + 自动重连 | `RpcClient`（指数退避） | ✅ | Gate 到 Game 持久连接 |
| GM 命令 HTTP 入口 | `AdminService` | ✅ | Master 复用 |
| 优雅停机 | 需自行实现（SIGTERM 信号处理） | ⚠️ | 各服自己处理 |

---

## 十四、实施计划

### 阶段一：账号服 + Master 服
- [ ] `example_01_account.cpp` — HTTP 注册/登录 + JWT 签发
- [ ] `example_02_master.cpp` — 心跳统计（不做判死）+ GM 分发 + HTTP 状态查询
- [ ] 验证：多服注册后 Master 日志正确输出健康比例

### 阶段二：Gate 服
- [ ] `example_04_gate.cpp` — TCP 接入 + 本地 JWT 验签 + fd_to_game_ 映射 + 断连清理
- [ ] 验证：客户端异常断连后 Gate 日志显示映射已清理

### 阶段三：Game 服
- [ ] `MapManager.h` — 地图实例管理、玩家传送、地图内广播
- [ ] `example_03_game.cpp` — Actor 系统 + 玩家管理 + 心跳 + 优雅停机
- [ ] 验证：玩家进出地图广播正常

### 阶段四：World 服 + 完整集成
- [ ] `example_05_world.cpp` — 跨服场景锁（IDLE→CREATING→ACTIVE→ENDING→IDLE）
- [ ] `example_06_mmorpg.cpp` — 单体多进程启动，端到端链路演示
- [ ] 端到端测试：登录 → 进入游戏 → 聊天 → 副本 → GM → 正常/异常退出

---

## 十五、风险评估（更新）

| 风险 | 影响 | 缓解措施 | 状态 |
|------|------|----------|------|
| Master 单点故障 | 高 | 开发阶段接受；生产阶段加主备 Master（下一阶段） | 已知 |
| Game 服单进程瓶颈 | 中 | 多 Game 实例 + Gate 负载均衡（RoundRobin） | 已知 |
| JWT 密钥泄露 | 高 | 开发固定密钥；生产通过环境变量注入，禁止硬编码 | 已知 |
| Game 服崩溃导致玩家掉线 | 中 | 优雅停机等待现有玩家退出（最多 30s） | 已知 |
| 跨服场景一致性 | 低（初期） | 场景锁状态机保证 BEGIN/END 配对；单服掉线不影响其他服 | 已知 |
