# SNC — 服务器网络组件库

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

适用于游戏服务器的 C++17 微服务网络框架。

## 概述

SNC 提供了一套分层设计的库，覆盖从底层 TCP I/O 到上层 RPC、服务发现及熔断器的完整网络栈。

```
snc_base      — 日志、数据结构、线程原语
snc_core      — 事件循环（IOCP/epoll/kqueue）、TcpConnection、消息路由
snc_rpc       — 异步 RPC 客户端/服务端、Entity 句柄管理
snc_circuit   — 熔断器（CLOSED/OPEN/HALF_OPEN）+ 令牌桶限流器
snc_discovery — 内存态 / Redis 后端服务注册表
snc_gateway   — 集群网关，负载均衡转发，集成熔断器
snc_monitor   — 运行时指标（I/O 空闲统计、反压检测、Lua 阻塞时间）
snc_rpc_lua   — Lua 脚本桥接，支持热重载游戏逻辑
snc_serial_pb — Protobuf 序列化适配器（默认 stub 模式）
snc_serial_fbs — FlatBuffers 序列化适配器（默认 stub 模式）
snc_http      — 轻量 HTTP 服务器（路由、中间件链、JSON 响应）
snc_auth      — JWT（HS256）签名、验签、中间件工厂
snc_admin     — Admin API 脚手架，通过回调注入业务逻辑
snc_websocket — WebSocket 网关（RFC 6455），IGatewayBridge 回调接口
snc_redis     — Redis 客户端适配器（默认内存 stub；可选 hiredis 后端）
snc_db        — 数据库适配层，内置 StubDatabase 用于测试
```

### 支持平台

| 平台 | 状态 | 说明 |
|------|------|------|
| Windows (MSVC) | ✅ 完全支持 | IOCP/select 事件循环 |
| Linux (GCC/Clang) | ✅ 完全支持 | epoll 事件循环 |
| macOS | ✅ 完全支持 | kqueue 事件循环 |

### 编译器要求

- 符合 C++17 标准的编译器（MSVC 2019+、GCC 8+、Clang 7+）
- CMake ≥ 3.20

## 教程

参考 [docs/tutorials/](docs/tutorials/) 中的入门教程：

| 教程 | 内容 |
|------|------|
| [01 - 快速开始](docs/tutorials/01-quickstart.md) | 编译配置与首个回显服务器 |
| [02 - RPC 服务](docs/tutorials/02-rpc-service.md) | RPC 客户端/服务端模式 |
| [03 - 连接生命周期](docs/tutorials/03-connection-lifecycle.md) | EventLoop、反压、Entity 处理 |
| [04 - HTTP 服务器](docs/tutorials/04-http-server.md) | 路由注册、中间件、GET/POST |
| [05 - JWT 认证](docs/tutorials/05-jwt-auth.md) | JwtService、MakeJwtMiddleware、Bearer 认证 |
| [06 - Admin API](docs/tutorials/06-admin-api.md) | AdminService 回调注入、玩家操作 |
| [07 - WebSocket 网关](docs/tutorials/07-websocket-gateway.md) | IGatewayBridge、WS 握手、心跳 |
| [08 - Redis 客户端](docs/tutorials/08-redis-config.md) | Stub/Hiredis 双后端配置与使用 |
| [09 - 熔断器](docs/tutorials/09-circuit-breaker.md) | CircuitBreaker + TokenBucket + Registry |
| [10 - 服务发现](docs/tutorials/10-service-discovery.md) | ServiceRegistry 注册、查询、负载均衡 |
| [11 - 定时器管理器](docs/tutorials/11-timer-manager.md) | TimerManager 一次性与周期定时器 |
| [12 - EntityHandle](docs/tutorials/12-entity-handle.md) | 跨线程安全访问连接的 weak_ptr 包装器 |
| [13 - Runtime Monitor](docs/tutorials/13-monitor.md) | 运行时指标采集（IO 空闲率、背压、Lua 阻塞、队列深度） |
| [14 - ClusterGateway](docs/tutorials/14-cluster-gateway.md) | 融合服务发现 + 熔断器的 RPC 转发网关 |
| [15 - 数据库适配器](docs/tutorials/15-mysql-database.md) | IDatabase 抽象层与 StubDatabase 内存测试 |
| [16 - 序列化](docs/tutorials/16-serialization.md) | Protobuf / FlatBuffers 序列化后端 |
| [17 - 账号服](docs/tutorials/17-account-service.md) | AccountService：HTTP 注册/登录、JWT 签发、Gate 本地验签 |
| [18 - WorkerPool](docs/tutorials/18-worker-pool.md) | 工作线程池：IO 与业务逻辑分离、消息分派 |
| [19 - Actor 生命周期](docs/tutorials/19-actor-lifecycle.md) | PlayerActor 生命周期管理：登录→在线→踢出→析构，weak_ptr 跨线程安全访问 |

## 编译构建

### 环境准备

- **CMake 3.20+**
- 可选依赖（默认关闭，库仍可正常编译）：
  - **Lua 5.4 / LuaJIT** — `SNC_RPC_ENABLE_LUA=ON`
  - **Protobuf** — `SNC_ENABLE_PROTOBUF=ON`
  - **FlatBuffers** — `SNC_ENABLE_FLATBUFFERS=ON`
  - **hiredis** — Redis 后端服务发现

### Windows（MSVC）快速开始

```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### Linux / macOS 快速开始

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### CMake 预设

项目附带 `CMakePresets.json`，包含四种预设配置：

| 预设 | 说明 |
|------|------|
| `minimal` | 仅 `snc_base` + `snc_core` + `snc_rpc` |
| `normal` | minimal + `snc_rpc_lua` + `snc_monitor` |
| `full` | 全部功能（Lua、Protobuf、FlatBuffers、Monitor） |
| `relwithdebinfo` | Release 含调试信息（默认） |

```bash
cmake --preset=full
cmake --build --preset=full
```

### 功能开关

| 开关 | 默认值 | 说明 |
|------|--------|------|
| `SNC_BUILD_RPC` | ON | 编译 RPC 库 |
| `SNC_RPC_ENABLE_LUA` | ON | 启用 Lua 脚本桥接 |
| `SNC_ENABLE_PROTOBUF` | OFF | 启用 Protobuf 序列化 |
| `SNC_ENABLE_FLATBUFFERS` | OFF | 启用 FlatBuffers 序列化 |
| `SNC_ENABLE_MONITOR` | ON | 启用运行时监控 |
| `SNC_ENABLE_SANITIZER` | OFF | 启用 AddressSanitizer（Debug 模式） |
| `SNC_ENABLE_HTTP` | ON | 启用 HTTP 服务器模块 |
| `SNC_ENABLE_AUTH` | ON | 启用 JWT 认证模块 |
| `SNC_ENABLE_ADMIN` | ON | 启用 Admin API 模块 |
| `SNC_ENABLE_WEBSOCKET` | OFF | 启用 WebSocket 网关模块 |
| `SNC_REDIS_CLIENT` | OFF | 启用 hiredis Redis 后端（默认：内存 stub） |

## 架构设计

### 分层结构

```
┌─────────────────────────────────────────────────┐
│  应用层 / 游戏逻辑（Lua 或 C++）                  │
├─────────────────────────────────────────────────┤
│  snc_admin    — Admin HTTP API（回调注入）        │
│  snc_http     — HTTP 服务器 + 中间件链           │
│  snc_auth     — JWT 签名 / 验签                 │
│  snc_websocket — WebSocket 网关                  │
├─────────────────────────────────────────────────┤
│  snc_gateway  — 集群网关、负载均衡                │
│  snc_discovery — 服务注册表                      │
│  snc_circuit  — 熔断器                           │
├─────────────────────────────────────────────────┤
│  snc_rpc      — RPC 客户端 / 服务端              │
├─────────────────────────────────────────────────┤
│  snc_core     — 事件循环、TCP、消息路由          │
├─────────────────────────────────────────────────┤
│  snc_base     — 日志、线程、数据结构             │
└─────────────────────────────────────────────────┘
```

### 核心设计模式

- **Entity / EntityHandle**：每个逻辑游戏实体持有独立的 `Entity`（含嵌入连接的 shared_ptr）。
  `EntityHandle<Entity>` 是基于 weak_ptr 的安全跨线程传递包装器。
- **RpcSession**：以序列号为键维护待处理调用映射表。请求被序列化为 `Msg` 对象，携带 `kRpcMsgId=0xFEED`。
- **ClusterGateway**：线程安全的无锁 `Forward()` 路径。统计信息与熔断器状态由互斥锁保护。
- **CircuitBreaker**：三态状态机（CLOSED → OPEN → HALF_OPEN），支持配置失败阈值与恢复超时。
- **MsgRouter**：按 `msgId` 将消息分发至处理器函数，同时支持 C++ 和 Lua 处理器。

## 公开 API 速览

### 日志

```cpp
#include "snc/base/Log.h"

SNC_LOG_INFO("tag", "Hello %s", name);
SNC_LOG_WARN("tag", "Something went wrong: %d", err);
SNC_LOG_ERROR("tag", "Fatal: %s", msg);
```

### 创建 RPC 服务端

```cpp
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcSession.h"

// RpcServer 无 Listen 方法；使用 core::Acceptor + EventLoop 接受连接
auto server  = std::make_shared<rpc::RpcServer>();
auto entity  = std::make_shared<rpc::Entity>(nullptr);
auto session = std::make_shared<rpc::RpcSession>(entity);

server->RegisterHandler(kLoginMsgId, [](IConnection::Ptr conn,
                                         rpc::RpcSession::Ptr /*session*/,
                                         Msg::Ptr msg,
                                         std::function<void(RpcError, const uint8_t*, size_t)> respond) {
    // 处理登录请求
    respond(RpcError::OK, nullptr, 0);
});
```

### 创建 RPC 客户端

```cpp
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcSession.h"
#include "snc/rpc/Entity.h"

auto entity      = std::make_shared<rpc::Entity>(nullptr);
auto rpcSession  = std::make_shared<rpc::RpcSession>(entity);
auto client      = std::make_shared<rpc::RpcClient>(rpcSession, eventLoop, nullptr);

std::string payload = "player_name";
client->Call(kLoginMsgId, payload.c_str(), payload.size(),
    [](RpcError err, const uint8_t* resp, size_t rlen) {
        if (err == RpcError::OK) { /* 处理响应 */ }
    });
```

### 熔断器

```cpp
#include "snc/circuit/CircuitBreaker.h"

circuit::CircuitConfig cfg;
cfg.failureThreshold  = 5;   // 连续失败 5 次触发熔断
cfg.openTimeout       = std::chrono::seconds(30);
cfg.successThreshold  = 3;

auto breaker = std::make_shared<circuit::CircuitBreaker>("my_service", cfg);

if (breaker->Allow()) {
    // 允许请求，执行业务逻辑
    try {
        // ...
        breaker->RecordSuccess();
    } catch (...) {
        breaker->RecordFailure();
    }
} else {
    // 服务已熔断，执行降级逻辑
}
```

### 服务发现

```cpp
#include "snc/discovery/ServiceRegistry.h"

auto reg = discovery::ServiceRegistry::Create();
reg->Register("login", "10.0.0.1", 8001, /* weight */ 1);
reg->Register("map",   "10.0.0.2", 8002, /* weight */ 2);

auto endpoints = reg->GetHealthy("login");  // 返回 ServiceList（健康节点）
auto all       = reg->GetAll("login");      // 返回所有节点（含不健康）
auto one       = reg->Pick(discovery::BalanceStrategy::ROUND_ROBIN, "login");
```

## 目录结构

```
snc/
├── CMakeLists.txt          # 根构建配置
├── CMakePresets.json       # 预定义构建预设
├── include/snc/            # 公共头文件（安装目标）
│   ├── base/               # 日志、线程、数据结构
│   ├── core/               # 事件循环、TCP、消息、消息队列、WorkerPool
│   ├── rpc/                # RPC 客户端、服务端、Entity、EntityHandle、MsgRouter
│   ├── rpc/lua/            # Lua RPC API 绑定
│   ├── http/               # HTTP 服务器、路由器
│   ├── auth/               # JWT 认证服务
│   ├── admin/              # Admin 管理 API
│   ├── ws/                 # WebSocket 网关
│   ├── circuit/            # 熔断器、令牌桶限流
│   ├── discovery/          # 服务注册表（内存 + Redis 后端）
│   ├── gateway/            # 集群网关（融合发现+熔断）
│   ├── monitor/            # 运行时指标采集
│   ├── redis/              # Redis 客户端（stub / hiredis）
│   ├── db/                 # 数据库抽象层（IDatabase / StubDatabase）
│   ├── serial/             # 序列化接口（ISerializer / PbSerializer / FbsSerializer）
│   ├── mmorpg/             # MMORPG 服务组件（GateService、GameService、WorldService）
│   ├── core.h / rpc.h      # 聚合头文件
├── src/                    # 核心库源码
├── libs/                   # 各子模块 CMake 子项目
│   ├── snc_base/
│   ├── snc_core/
│   ├── snc_rpc/
│   ├── snc_http/
│   ├── snc_auth/
│   ├── snc_admin/
│   ├── snc_websocket/
│   ├── snc_circuit/
│   ├── snc_discovery/
│   ├── snc_gateway/
│   ├── snc_monitor/
│   ├── snc_redis/
│   ├── snc_db/
│   ├── snc_serial_pb/
│   ├── snc_serial_fbs/
│   └── snc_rpc_lua/
├── examples/               # 示例代码
├── proto/                  # Protobuf 定义文件
├── tests/                  # 单元测试（自包含 Catch2-like 测试框架）
├── docs/
│   ├── tutorials/          # 入门教程（01~16）
│   ├── modules/            # 模块详细文档
│   └── design/             # 架构设计文档
└── thirdparty/             # 第三方头文件
```

## 许可证

MIT 许可证 — 见 [LICENSE](LICENSE)。

## 贡献指南

请参考 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 已知问题

请参考 [KNOWN_ISSUES.md](KNOWN_ISSUES.md) 了解平台限制与已知缺陷。
