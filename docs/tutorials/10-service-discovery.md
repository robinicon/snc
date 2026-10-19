# 10 — 服务注册与发现

**目标**：掌握 `ServiceRegistry` 的注册、查询、负载均衡策略，以及 Redis 后端的使用方式。

## 概述

`snc_discovery` 提供进程内服务注册表，用于网关层做服务端点发现和负载均衡。

头文件入口：

```cpp
#include <snc/discovery/ServiceRegistry.h>
```

## ServiceEntry 数据结构

```cpp
struct ServiceEntry {
    std::string serviceName;  // 服务名，如 "login"、"map"
    std::string host;         // IP 或主机名
    uint16_t    port;         // TCP 端口
    int32_t     weight;       // 负载均衡权重（默认 1）
    bool        healthy;      // 是否健康（默认 true）
};
```

## 创建与注册

使用静态工厂方法 `Create()` 创建实例：

```cpp
#include <snc/discovery/ServiceRegistry.h>

auto reg = snc::discovery::ServiceRegistry::Create();

// 注册端点（相同 name+host+port 会更新 weight 和 healthy 标志）
reg->Register("login", "10.0.0.1", 8001, /* weight */ 1);
reg->Register("login", "10.0.0.2", 8001, /* weight */ 2);
reg->Register("map",   "10.0.0.2", 8002, /* weight */ 3);
```

## 查询端点

```cpp
// 获取所有健康端点
auto healthy = reg->GetHealthy("login");   // ServiceList

// 获取全部端点（含不健康）
auto all = reg->GetAll("login");

// 按策略挑选一个健康端点
auto ep = reg->Pick(snc::discovery::BalanceStrategy::ROUND_ROBIN, "login");
// ep 为 ServiceEntryPtr，若无可用的健康节点则返回 nullptr
```

### 负载均衡策略

| 策略 | 说明 |
|------|------|
| `ROUND_ROBIN` | 轮询，按注册顺序循环选取 |
| `RANDOM` | 伪随机选取，基于线程安全计数器 |
| `WEIGHTED` | 按 `weight` 字段加权选取 |

## 生命周期管理

```cpp
// 标记某服务所有端点不健康（优雅下线时调用）
reg->MarkUnhealthy("login");

// 注销指定端点
reg->Unregister("login", "10.0.0.1", 8001);

// 移除某个服务的所有端点
reg->DeregisterAll("login");

// 定期刷新所有端点为健康（由外部健康检查线程调用）
reg->RefreshHealth();

// 统计信息
size_t svcCount = reg->ServiceCount();       // 已注册的服务数
size_t epCount  = reg->EndpointCount();      // 所有端点总数
auto   names    = reg->ListServices();       // 所有服务名列表
```

## 与 ClusterGateway 配合使用

`ClusterGateway`（见 [gateway/ClusterGateway.h](file:///c:/Users/10422/Documents/code/snc/include/snc/gateway/ClusterGateway.h)）内部持有 `ServiceRegistry` 引用，自动完成选点和熔断。

```cpp
#include <snc/gateway/ClusterGateway.h>
#include <snc/discovery/ServiceRegistry.h>
#include <snc/gateway/CircuitBreaker.h>

auto reg = snc::discovery::ServiceRegistry::Create();
reg->Register("login", "10.0.0.1", 8001, 1);
reg->Register("login", "10.0.0.2", 8001, 2);

// ClusterGateway 内部集成熔断器，自动 pick 端点并做失败统计
snc::gateway::ClusterGateway gateway(reg);
gateway.Start(/* workerThreads */ 4);

// 转发消息（线程安全）
gateway.Forward(session, msg);
```

## Redis 后端（ServiceRegistry + Redis）

当启用 `SNC_REDIS_CLIENT=ON` 时，可使用基于 Redis 的分布式服务注册表：

```cpp
#include <snc/discovery/RedisServiceRegistry.h>
#include <snc/redis/Client.h>

auto redis = snc::redis::IClient::Create();
redis->Connect("127.0.0.1", 6379);

// Redis 后端注册表（多进程共享同一份注册信息）
auto reg = snc::discovery::RedisServiceRegistry::Create(redis);
reg->Register("login", "10.0.0.1", 8001);
// 其他进程注册的端点也会立即可见（基于 Redis 广播）
```

详见 [docs/modules/redis-client.md](../modules/redis-client.md)。

## 注意事项

- `Register()` 同一 `(name, host, port)` 会**更新**已有条目，而非重复插入
- `GetHealthy()` 返回的是 `ServiceEntryPtr` 列表，可直接读取 `host`/`port`
- `Pick()` 在无可用的健康端点时返回 `nullptr`，调用方需做空指针检查
- 健康检查需由**外部线程**定期调用 `RefreshHealth()`，进程内不自动心跳

参见 [02 - RPC 服务](02-rpc-service.md)、[09 - 熔断器](09-circuit-breaker.md)
