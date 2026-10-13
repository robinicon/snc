# 14 — ClusterGateway（集群网关）

**目标**：掌握 `ClusterGateway` 的用法，理解它如何将服务发现、负载均衡和熔断器组合在一起。

## 概述

`snc::gateway::ClusterGateway` 是一个线程安全的 RPC 转发网关，内部集成了：

- **ServiceRegistry**：端点发现和轮询选点
- **CircuitBreakerRegistry**：按后端服务维护独立熔断器
- **请求统计**：每个后端的路由数、成功数、失败数、熔断拒绝次数

```
客户端 ──RPC──▶ ClusterGateway
                    │
              ┌─────┴─────┐
              │  Login    │──▶ backend1 (10.0.0.1:8001)
              │  Map      │──▶ backend2 (10.0.0.2:8002)
              └───────────┘
```

头文件入口：

```cpp
#include <snc/gateway/ClusterGateway.h>
```

## 创建与注册后端

```cpp
#include <snc/gateway/ClusterGateway.h>
#include <snc/discovery/ServiceRegistry.h>
#include <snc/rpc/RpcClient.h>
#include <snc/rpc/RpcSession.h>
#include <snc/rpc/Entity.h>

auto registry = snc::discovery::ServiceRegistry::Create();
registry->Register("login", "10.0.0.1", 8001);
registry->Register("login", "10.0.0.2", 8001);
registry->Register("map",   "10.0.0.2", 8002);

auto gateway = std::make_shared<snc::gateway::ClusterGateway>(registry);

// 为每个后端服务注册一个 RpcClient
auto makeClient = [](core::IEventLoop* el) {
    auto entity      = std::make_shared<rpc::Entity>(nullptr);
    auto rpcSession  = std::make_shared<rpc::RpcSession>(entity);
    return std::make_shared<rpc::RpcClient>(rpcSession, el, nullptr);
};

gateway->AddBackend("login", makeClient(eventLoop.get()));
gateway->AddBackend("map",   makeClient(eventLoop.get()));
```

## 转发请求

`Forward()` 方法自动完成：熔断检查 → 端点选择 → RPC 调用 → 统计更新。

```cpp
std::string payload = "player_name";
gateway->Forward("login", 0x1001,
    payload.c_str(), payload.size(),
    [](rpc::RpcError err, const uint8_t* resp, size_t len) {
        if (err == rpc::RpcError::OK) {
            std::cout << "Success\n";
        } else {
            std::cout << "Failed: " << rpc::RpcErrorStr(err) << "\n";
        }
    });
```

如果熔断器已打开，回调会立即以 `RpcError::SERVICE_UNAVAILABLE` 触发，不会发起网络请求。

## 查看统计

```cpp
// 查询某个后端的统计
auto stats = gateway->GetStats("login");
std::cout << "requests=" << stats.requests
          << " successes=" << stats.successes
          << " failures=" << stats.failures
          << " circuitOpen=" << stats.circuitOpen
          << " healthy=" << stats.healthyEndpoints << "\n";

// 列出所有已注册的后端服务名
auto backends = gateway->ListBackends();
for (auto& name : backends) {
    std::cout << "  " << name << "\n";
}
```

## 完整示例

```cpp
#include <snc/gateway/ClusterGateway.h>
#include <snc/discovery/ServiceRegistry.h>
#include <snc/rpc/RpcClient.h>
#include <snc/rpc/RpcSession.h>
#include <snc/rpc/Entity.h>
#include <snc/base/Log.h>

int main() {
    snc::base::InitLogger(snc::base::LogLevel::INFO);
    auto eventLoop = snc::core::CreateEventLoop();

    // 1. 注册服务端点
    auto registry = snc::discovery::ServiceRegistry::Create();
    registry->Register("login", "10.0.0.1", 8001);
    registry->Register("login", "10.0.0.2", 8001);

    // 2. 创建网关
    snc::gateway::ClusterGateway gateway(registry);

    // 3. 注册 RPC 客户端
    auto entity   = std::make_shared<snc::rpc::Entity>(nullptr);
    auto session  = std::make_shared<snc::rpc::RpcSession>(entity);
    auto client   = std::make_shared<snc::rpc::RpcClient>(session, eventLoop.get(), nullptr);
    gateway.AddBackend("login", client);

    // 4. 发起请求
    std::string payload = "test_player";
    gateway.Forward("login", 0x1001,
        payload.c_str(), payload.size(),
        [](snc::rpc::RpcError err, const uint8_t*, size_t) {
            snc::SNC_LOG_INFO("Result: %s", snc::rpc::RpcErrorStr(err));
        });

    eventLoop->Run();
    return 0;
}
```

## 注意事项

- **`AddBackend` 线程安全**：可在启动后随时注册新后端
- **`Forward` 线程安全**：可在任意线程调用，RPC 调用本身不持有锁
- **每次 `Forward` 调用内部重新 pick 端点**：不缓存连接，端点变化时自动感知
- **熔断器由 Gateway 内部管理**：通过 `GetStats().circuitOpen` 可查看熔断拒绝次数，不需要手动管理

参见 [10 - 服务注册与发现](10-service-discovery.md)、[09 - 熔断器](09-circuit-breaker.md)
