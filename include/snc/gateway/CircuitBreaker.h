// SPDX-License-Identifier: MIT
// 熔断器模式（Circuit Breaker）—— 防止级联故障扩散到整个服务集群。
//
// 三态转换：
//   CLOSED   → 正常运行，统计失败次数
//   OPEN     → 拒绝所有请求，等待 openTimeout 后 → HALF_OPEN
//   HALF_OPEN → 允许少量探测请求；成功 → CLOSED，失败 → OPEN
//
// 同时包含令牌桶限流器（TokenBucket），用于单服务 QPS 控制。
#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <functional>
#include <memory>

namespace snc {
namespace gateway {

// ══════════════════════════════════════════════════════════════
// 熔断器
// ══════════════════════════════════════════════════════════════

/// 熔断器状态枚举。
enum class CircuitState : int {
    CLOSED    = 0,  /// 闭合：正常放行请求，统计失败
    OPEN      = 1,  /// 断开：拒绝所有请求
    HALF_OPEN = 2,  /// 半开：允许少量探测请求
};

/// 熔断器配置参数。
struct CircuitConfig {
    int32_t failureThreshold  = 5;                /// 连续失败多少次触发熔断
    int32_t successThreshold  = 3;                /// HALF_OPEN 下连续成功多少次恢复闭合
    std::chrono::milliseconds openTimeout = std::chrono::seconds(30); /// OPEN 状态的持续时间
    int32_t maxRequestsInHalfOpen = 1;            /// HALF_OPEN 允许的最大探测请求数
};

/// 熔断器核心类，线程安全。
/// 用途：在调用远程服务前检查 Allow()，记录 ResultSuccess/ResultFailure 以驱动状态机。
class CircuitBreaker {
public:
    using Ptr = std::shared_ptr<CircuitBreaker>;

    /// 构造熔断器，name 用于监控标识。
    explicit CircuitBreaker(const std::string& name, const CircuitConfig& cfg = CircuitConfig())
        : name_(name), cfg_(cfg),
          state_(CircuitState::CLOSED),
          consecutiveFailures_(0), consecutiveSuccesses_(0),
          lastFailureTime_(std::chrono::steady_clock::now()),
          halfOpenRequests_(0) {}

    /// 在执行远程调用前调用。返回 true 表示允许，false 表示熔断器已打开。
    bool Allow() {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (state_) {
            case CircuitState::OPEN:
                if (IsTimeoutElapsed_()) {
                    state_ = CircuitState::HALF_OPEN;
                    halfOpenRequests_ = 0;
                    return true;
                }
                return false;
            case CircuitState::HALF_OPEN:
                if (halfOpenRequests_ >= cfg_.maxRequestsInHalfOpen) return false;
                ++halfOpenRequests_;
                return true;
            default: // CLOSED
                return true;
        }
    }

    /// 记录一次成功调用。在 HALF_OPEN 状态下，连续成功 successThreshold 次后转为 CLOSED。
    void RecordSuccess() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == CircuitState::HALF_OPEN) {
            ++consecutiveSuccesses_;
            if (consecutiveSuccesses_ >= cfg_.successThreshold) {
                state_ = CircuitState::CLOSED;
                consecutiveFailures_ = 0;
                consecutiveSuccesses_ = 0;
            }
        } else {
            consecutiveFailures_ = 0;
        }
        ++totalSuccesses_;
    }

    /// 记录一次失败调用。可能将状态转为 OPEN。
    void RecordFailure() {
        std::lock_guard<std::mutex> lk(mtx_);
        lastFailureTime_ = std::chrono::steady_clock::now();
        if (state_ == CircuitState::HALF_OPEN) {
            // HALF_OPEN 下任何失败都立即回到 OPEN
            state_ = CircuitState::OPEN;
            consecutiveSuccesses_ = 0;
            halfOpenRequests_ = 0;
        } else {
            ++consecutiveFailures_;
            if (consecutiveFailures_ >= cfg_.failureThreshold) {
                state_ = CircuitState::OPEN;
                consecutiveSuccesses_ = 0;
            }
        }
        ++totalFailures_;
    }

    /// 强制打开熔断器（手动干预场景，如运维关闭服务）。
    void ForceOpen() {
        std::lock_guard<std::mutex> lk(mtx_);
        state_ = CircuitState::OPEN;
        consecutiveFailures_ = 0;
        consecutiveSuccesses_ = 0;
        halfOpenRequests_ = 0;
    }

    /// 重置为 CLOSED 状态（清除所有统计）。
    void Reset() {
        std::lock_guard<std::mutex> lk(mtx_);
        state_ = CircuitState::CLOSED;
        consecutiveFailures_ = 0;
        consecutiveSuccesses_ = 0;
        halfOpenRequests_ = 0;
    }

    /// 获取当前状态（若处于 OPEN 且超时已过，自动返回 HALF_OPEN）。
    CircuitState GetState() const {
        std::lock_guard<std::mutex> lk(mtx_);
        if (state_ == CircuitState::OPEN && IsTimeoutElapsed_()) {
            return CircuitState::HALF_OPEN;
        }
        return state_;
    }

    const std::string& Name() const { return name_; }

    /// 熔断器统计数据。
    struct Stats {
        int32_t consecutiveFailures = 0;
        int32_t consecutiveSuccesses = 0;
        int64_t totalFailures = 0;
        int64_t totalSuccesses = 0;
    };
    Stats GetStats() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return {
            static_cast<int32_t>(consecutiveFailures_),
            static_cast<int32_t>(consecutiveSuccesses_),
            totalFailures_,
            totalSuccesses_,
        };
    }

private:
    /// 判断 OPEN 超时是否已过（供内部状态转换使用）。
    bool IsTimeoutElapsed_() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFailureTime_).count() >= cfg_.openTimeout.count();
    }

    std::string                         name_;
    CircuitConfig                       cfg_;
    mutable std::mutex                  mtx_;
    CircuitState                        state_{CircuitState::CLOSED};
    int32_t                             consecutiveFailures_;
    int32_t                             consecutiveSuccesses_;
    std::chrono::steady_clock::time_point lastFailureTime_;
    int32_t                             halfOpenRequests_;
    std::atomic<int64_t>                totalFailures_{0};
    std::atomic<int64_t>                totalSuccesses_{0};
};

// ══════════════════════════════════════════════════════════════
// 令牌桶限流器
// ══════════════════════════════════════════════════════════════

/// 令牌桶限流器：允许突发流量（burst），但长期维持 rate QPS。
/// 与熔断器配合使用，可实现"限流 + 熔断"的双重保护。
///
/// 算法：
///   - 桶容量 = burst，初始满桶
///   - 每次 TryAcquire 消耗 1 个令牌
///   - 每过 elapsed 秒补充 elapsed * rate 个令牌（上限 burst）
class TokenBucket {
public:
    /// rate: 每秒允许的请求数；burst: 桶容量（突发上限）。
    explicit TokenBucket(double rate, int32_t burst = 10)
        : rate_(rate), burst_(burst), tokens_(static_cast<double>(burst)) {}

    /// 尝试获取一个令牌。返回 true 表示允许通过，false 表示限流。
    bool TryAcquire() {
        std::lock_guard<std::mutex> lk(mtx_);
        Refill_();
        if (tokens_ >= 1.0) {
            --tokens_;
            return true;
        }
        return false;
    }

    /// 阻塞等待令牌，直到 timeoutMs 毫秒后超时。
    /// 适用于对延迟不敏感的控制面请求。
    bool WaitAcquire(int timeoutMs = 1000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                Refill_();
                if (tokens_ >= 1.0) {
                    --tokens_;
                    return true;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    int32_t Burst() const { return burst_; }
    double  Rate()  const { return rate_; }

    struct Stats {
        int64_t  allowed     = 0;
        int64_t  rejected    = 0;
        double   currentTokens = 0;
    };
    Stats GetStats() const {
        std::lock_guard<std::mutex> lk(mtx_);
        Refill_();
        return {totalAllowed_, totalRejected_, tokens_};
    }

    void ResetStats() {
        std::lock_guard<std::mutex> lk(mtx_);
        totalAllowed_ = 0;
        totalRejected_ = 0;
    }

private:
    /// 按 elapsed * rate 补充令牌（不超过 burst）。
    void Refill_() const {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastRefill_).count();
        tokens_ = std::min(static_cast<double>(burst_), tokens_ + elapsed * rate_);
        lastRefill_ = now;
    }

    double                                   rate_;
    int32_t                                  burst_;
    mutable double                           tokens_;
    mutable std::mutex                       mtx_;
    mutable std::chrono::steady_clock::time_point lastRefill_ = std::chrono::steady_clock::now();
    std::atomic<int64_t>                     totalAllowed_{0};
    std::atomic<int64_t>                     totalRejected_{0};
};

// ══════════════════════════════════════════════════════════════
// 熔断器注册表（按服务名管理多个熔断器实例）
// ══════════════════════════════════════════════════════════════

/// 按服务名称管理熔断器实例的注册表。
/// 提供便捷方法 Allow/RecordSuccess/RecordFailure，无需手动查找。
class CircuitBreakerRegistry {
public:
    using Ptr = std::shared_ptr<CircuitBreakerRegistry>;

    /// 获取或创建指定服务的熔断器（线程安全）。
    CircuitBreaker::Ptr GetOrCreate(const std::string& serviceName,
                                    const CircuitConfig& cfg = CircuitConfig()) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = breakers_.find(serviceName);
        if (it != breakers_.end()) return it->second;
        auto cb = std::make_shared<CircuitBreaker>(serviceName, cfg);
        breakers_[serviceName] = cb;
        return cb;
    }

    /// 便捷方法：检查指定服务的熔断器是否允许请求。
    bool Allow(const std::string& serviceName) {
        return GetOrCreate(serviceName)->Allow();
    }

    /// 更新指定服务的熔断器配置（替换整个实例）。
    void Configure(const std::string& serviceName, const CircuitConfig& cfg) {
        std::lock_guard<std::mutex> lk(mtx_);
        breakers_[serviceName] = std::make_shared<CircuitBreaker>(serviceName, cfg);
    }

    /// 便捷方法：记录成功。
    void RecordSuccess(const std::string& serviceName) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = breakers_.find(serviceName);
        if (it != breakers_.end()) it->second->RecordSuccess();
    }

    /// 便捷方法：记录失败。
    void RecordFailure(const std::string& serviceName) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = breakers_.find(serviceName);
        if (it != breakers_.end()) it->second->RecordFailure();
    }

private:
    mutable std::mutex                                    mtx_;
    std::unordered_map<std::string, CircuitBreaker::Ptr>  breakers_;
};

} // namespace gateway
} // namespace snc
