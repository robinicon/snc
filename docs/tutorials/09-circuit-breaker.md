# 09 — 熔断器与限流器

**目标**：掌握 `CircuitBreaker`、`TokenBucket` 和 `CircuitBreakerRegistry` 的使用方式。

## 概述

`snc_circuit` 模块提供三种组件：

| 组件 | 作用 |
|------|------|
| `CircuitBreaker` | 三态熔断器，防止下游服务故障级联扩散 |
| `TokenBucket` | 令牌桶限流器，控制单服务的 QPS 上限 |
| `CircuitBreakerRegistry` | 按服务名管理多个熔断器实例 |

## CircuitBreaker（熔断器）

### 三态转换

```
CLOSED ──(连续失败≥threshold)──> OPEN ──(等待 openTimeout)──> HALF_OPEN
  ^                                                      │
  └────(连续成功≥successThreshold)───────────────────────┘
  └────(任意失败)─────────────────────────────────────────┘
```

- **CLOSED**：正常运行，统计失败次数
- **OPEN**：拒绝所有请求，等待恢复超时后进入 HALF_OPEN
- **HALF_OPEN**：允许少量探测请求；成功 → CLOSED，失败 → OPEN

### 构造与配置

```cpp
#include <snc/circuit/CircuitBreaker.h>

snc::circuit::CircuitConfig cfg;
cfg.failureThreshold    = 5;            // 连续失败 5 次触发熔断
cfg.successThreshold    = 3;            // HALF_OPEN 下连续成功 3 次恢复
cfg.openTimeout         = std::chrono::seconds(30); // OPEN 状态保持 30 秒
cfg.maxRequestsInHalfOpen = 1;          // HALF_OPEN 最多允许 1 条探测请求

auto breaker = std::make_shared<snc::circuit::CircuitBreaker>("login_service", cfg);
```

### 典型用法

```cpp
if (breaker->Allow()) {
    try {
        // 执行远程调用
        DoRemoteCall();
        breaker->RecordSuccess();       // 成功：重置失败计数
    } catch (...) {
        breaker->RecordFailure();       // 失败：累计计数，可能触发熔断
    }
} else {
    // 熔断中：执行降级逻辑（返回缓存、默认值等）
    ReturnFallback();
}
```

### 查询状态

```cpp
auto state  = breaker->GetState();    // CLOSED / OPEN / HALF_OPEN
auto stats  = breaker->GetStats();    // consecutiveFailures / totalSuccesses / ...

// 手动干预
breaker->ForceOpen();                 // 强制熔断（运维关闭服务）
breaker->Reset();                     // 重置为 CLOSED
```

## TokenBucket（令牌桶限流器）

令牌桶以固定速率补充令牌，允许短期突发流量（burst），长期维持 `rate` QPS。

```cpp
#include <snc/circuit/CircuitBreaker.h>

// rate=100 QPS，突发上限=20
snc::circuit::TokenBucket bucket(100.0, /* burst */ 20);

if (bucket.TryAcquire()) {
    // 允许通过
} else {
    // 限流：请求排队或返回 503
}

// 阻塞等待令牌（适用于管理面操作）
bool acquired = bucket.WaitAcquire(/* timeoutMs */ 500);

// 统计信息
auto st = bucket.GetStats();   // allowed / rejected / currentTokens
```

## CircuitBreakerRegistry（熔断器注册表）

按服务名统一管理多个熔断器，提供便捷方法，无需手动查找实例。

```cpp
auto registry = std::make_shared<snc::circuit::CircuitBreakerRegistry>();

// 获取或创建（首次调用自动创建，后续返回已存在实例）
registry->GetOrCreate("login_service", cfg);
registry->GetOrCreate("map_service",   cfg);

// 便捷方法
if (registry->Allow("login_service")) {
    try {
        DoLogin();
        registry->RecordSuccess("login_service");
    } catch (...) {
        registry->RecordFailure("login_service");
    }
}

// 动态调整配置
snc::circuit::CircuitConfig strict_cfg;
strict_cfg.failureThreshold = 3;
registry->Configure("login_service", strict_cfg);
```

## 完整示例：调用远程服务

```cpp
#include <snc/circuit/CircuitBreaker.h>

class RemoteServiceClient {
public:
    RemoteServiceClient(snc::circuit::CircuitBreakerRegistry::Ptr reg)
        : registry_(reg) {}

    std::string CallLogin(const std::string& user) {
        static const std::string kSvc = "login_service";

        // 1. 熔断检查
        if (!registry_->Allow(kSvc)) {
            return "{\"error\":\"service_circuit_open\"}";
        }

        // 2. 限流检查（可选）
        // if (!bucket_.TryAcquire()) return "{\"error\":\"rate_limited\"}";

        // 3. 执行调用
        try {
            std::string result = DoHttpCall("http://login:8001/login", user);
            registry_->RecordSuccess(kSvc);
            return result;
        } catch (...) {
            registry_->RecordFailure(kSvc);
            return "{\"error\":\"upstream_error\"}";
        }
    }

private:
    snc::circuit::CircuitBreakerRegistry::Ptr registry_;
};
```

## 注意事项

- **`Allow()` 必须在调用前检查**，不可跳过直接进入业务逻辑
- **`RecordSuccess` / `RecordFailure` 必须配对调用**，缺失会导致状态机卡住
- **`CircuitBreaker` 不是线程安全的单例**：建议用 `CircuitBreakerRegistry` 管理，或在业务层加锁
- **HALF_OPEN 状态下，连续成功必须达到 `successThreshold` 次才恢复 CLOSED**，单次成功不够

参见 [02 - RPC 服务](02-rpc-service.md)、[10 - 服务注册与发现](10-service-discovery.md)
