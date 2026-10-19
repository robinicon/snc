# 21 — 完整单服集成

**目标**：将 TCP 接入、RPC 通信、HTTP 管理接口集成到一个完整的游戏服务器进程中。

## 架构总览

单服集成示例（对应 `examples/example_06_mmorpg.cpp`）在一个进程中模拟四个服务：

```
┌─────────────────────────────────────────────────────┐
│              单进程多服务示例                         │
├──────────────┬──────────────┬───────────────────────┤
│  Account     │   Master     │       Game            │
│  HTTP :8080  │   RPC :9000  │      RPC :8001        │
│  注册/登录    │   服务注册    │    游戏逻辑/地图       │
│  JWT 签发    │   心跳统计    │    Actor 生命周期      │
├──────────────┴──────────────┴───────────────────────┤
│                  Gate (TCP :7777)                   │
│           客户端接入 → RPC 转发到 Game               │
└─────────────────────────────────────────────────────┘
```

实际生产环境中每个服务是独立进程，本示例在一个进程中演示通信流程。

## 服务启动顺序

```
1. AccountService 启动（HTTP 注册/登录/JWT）
2. 注册测试账号，签发 JWT
3. MasterService 启动（Admin HTTP + 服务注册表）
4. GameService 启动（注册到 Master，监听 RPC 端口）
5. GateService 启动（连接 Game，监听 TCP 端口）
```

## 核心代码结构

### AccountService（HTTP 鉴权）

```cpp
// 使用 StubDatabase 做内存存储
auto db = CreateStubDatabase();
db->Connect("stub://");

AccountService account_svc(db, jwt_cfg);
HttpServer account_server;
account_svc.RegisterRoutes(account_server.Routes());
account_server.Listen(8080);
std::thread([]{ account_server.Run(); }).detach();
```

### MasterService（服务注册中心）

```cpp
auto registry = ServiceRegistry::Create();
auto master_svc = std::make_shared<MasterService>(registry);
HttpServer admin_server;
master_svc->RegisterAdminRoutes(admin_server.Routes(), jwt_svc);
admin_server.Listen(9001);
std::thread([]{ admin_server.Run(); }).detach();
```

### GameService（游戏逻辑）

```cpp
// 创建指向 Master 的 RPC 客户端（用于心跳和注册）
auto master_rpc = std::make_shared<RpcClient>(
    std::make_shared<RpcSession>(nullptr));

auto game_registry = ServiceRegistry::Create();
game_registry->Register("game", "127.0.0.1", 8001, 1);

GameService game_svc(master_rpc, game_registry, jwt_cfg);
game_svc.InitMaps();  // 初始化地图

std::thread([&]{ game_svc.Start(8001, "game_01"); }).detach();
```

### GateService（TCP 接入）

```cpp
auto gate_registry = ServiceRegistry::Create();
gate_registry->Register("game", "127.0.0.1", 8001, 1);

GateService gate_svc(gate_registry, jwt_cfg);
gate_svc.Start(7777);  // 监听 TCP 7777 端口
```

## 消息流

```
客户端
  │  TCP 连接
  ▼
GateService (:7777)
  │  RPC 0x2000（Gate→Game）
  ▼
GameService (:8001)
  │  RPC 0x2100（Game→Gate，推送给客户端）
  ▼
客户端
```

## 完整示例入口

完整可运行代码见 [example_06_mmorpg.cpp](file:///c:/Users/10422/Documents/code/snc/examples/example_06_mmorpg.cpp)。

编译并运行：

```bash
cmake --build build --config Release --target example_06_mmorpg
.\build\Release\example_06_mmorpg.exe
```

测试流程：

```bash
# 1. 注册账号
curl -X POST http://localhost:8080/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"player1","password":"test123"}'

# 2. 登录获取 JWT
curl -X POST http://localhost:8080/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"player1","password":"test123"}'

# 3. 查询服务状态
curl http://localhost:9001/api/admin/services
```

## JWT 统一配置

所有服务共享同一 JWT 密钥，通过环境变量 `SNC_JWT_SECRET` 传入：

```bash
# Windows
set SNC_JWT_SECRET=my-secret-key-that-is-at-least-32-chars-long!

# Linux / macOS
export SNC_JWT_SECRET="my-secret-key-that-is-at-least-32-chars-long!"
```

密钥配置统一在 [common/config.h](file:///c:/Users/10422/Documents/code/snc/examples/common/config.h)：

```cpp
namespace snc_examples {
inline std::string JwtSecret() {
    const char* env = std::getenv("SNC_JWT_SECRET");
    return (env && env[0]) ? env : "snc-dev-jwt-secret-key-must-be-at-least-32-bytes!";
}
}
```

## 关键设计要点

| 要点 | 说明 |
|------|------|
| `detach()` 线程 | 各服务独立运行，主线程等待信号量后统一退出 |
| 优雅停机 | 收到 SIGINT/SIGTERM 后，依次调用各服务的 `Shutdown()` |
| 服务注册 | `ServiceRegistry` 维护内存态服务列表，Gate 通过它找到 Game 的 RPC 地址 |
| JWT 验签 | Gate 本地验签（无需网络请求），Game 依赖 Account 签发 |

## 下一步

- [19 — Actor 生命周期](./19-actor-lifecycle.md) — 理解 GameService 中的玩家管理
- [22 — 传奇版概览](./22-legend-overview.md) — 了解基于 SNC 的传奇 MMORPG 扩展
- [06 - Admin API](./06-admin-api.md) — 深入 MasterService 的管理接口
