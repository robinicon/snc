// Circuit breaker pattern — prevents cascading failures across services.
//
// States:
//   CLOSED   → normal operation, failures counted
//   OPEN     → requests rejected immediately, after timeout → HALF_OPEN
//   HALF_OPEN → allow one probe request; success → CLOSED, failure → OPEN
//
// Also includes a Token Bucket rate limiter for per-service QPS control.
#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <functional>
#include <memory>

namespace snc {
namespace circuit {

// ── Circuit Breaker ─────────────────────────────────────────────

enum class CircuitState : int {
    CLOSED   = 0,
    OPEN     = 1,
    HALF_OPEN = 2,
};

struct CircuitConfig {
    int32_t failureThreshold = 5;      // consecutive failures to trip
    int32_t successThreshold = 3;      // consecutive successes in HALF_OPEN to close
    std::chrono::milliseconds openTimeout = std::chrono::seconds(30); // wait before HALF_OPEN
    int32_t maxRequestsInHalfOpen = 1;  // probe requests allowed
};

class CircuitBreaker {
public:
    using Ptr = std::shared_ptr<CircuitBreaker>;

    explicit CircuitBreaker(const std::string& name, const CircuitConfig& cfg = CircuitConfig())
        : name_(name), cfg_(cfg),
          state_(CircuitState::CLOSED),
          consecutiveFailures_(0), consecutiveSuccesses_(0),
          lastFailureTime_(std::chrono::steady_clock::now()),
          halfOpenRequests_(0) {}

    /// Call before executing a remote request.
    /// Returns true if the call is permitted, false if the circuit is OPEN.
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

    /// Record a successful call — advances toward CLOSED from HALF_OPEN.
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

    /// Record a failed call — may trip the breaker to OPEN.
    void RecordFailure() {
        std::lock_guard<std::mutex> lk(mtx_);
        lastFailureTime_ = std::chrono::steady_clock::now();
        if (state_ == CircuitState::HALF_OPEN) {
            // Any failure in HALF_OPEN → back to OPEN
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

    /// Force-open the breaker (e.g. on manual intervention).
    void ForceOpen() {
        std::lock_guard<std::mutex> lk(mtx_);
        state_ = CircuitState::OPEN;
        consecutiveFailures_ = 0;
        consecutiveSuccesses_ = 0;
        halfOpenRequests_ = 0;
    }

    /// Reset to CLOSED.
    void Reset() {
        std::lock_guard<std::mutex> lk(mtx_);
        state_ = CircuitState::CLOSED;
        consecutiveFailures_ = 0;
        consecutiveSuccesses_ = 0;
        halfOpenRequests_ = 0;
    }

    CircuitState GetState() const {
        std::lock_guard<std::mutex> lk(mtx_);
        // Check if we should transition from OPEN → HALF_OPEN
        if (state_ == CircuitState::OPEN && IsTimeoutElapsed_()) {
            return CircuitState::HALF_OPEN;
        }
        return state_;
    }

    const std::string& Name() const { return name_; }

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
    bool IsTimeoutElapsed_() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFailureTime_).count() >= cfg_.openTimeout.count();
    }

    std::string                    name_;
    CircuitConfig                  cfg_;
    mutable std::mutex             mtx_;
    CircuitState                   state_{CircuitState::CLOSED};
    int32_t                        consecutiveFailures_;
    int32_t                        consecutiveSuccesses_;
    std::chrono::steady_clock::time_point lastFailureTime_;
    int32_t                        halfOpenRequests_;
    std::atomic<int64_t>           totalFailures_{0};
    std::atomic<int64_t>           totalSuccesses_{0};
};

// ── Token Bucket Rate Limiter ────────────────────────────────────

/// Allows up to `rate` requests per second, with a burst capacity of `burst`.
class TokenBucket {
public:
    /// rate: requests per second; burst: max concurrent tokens (burst capacity).
    explicit TokenBucket(double rate, int32_t burst = 10)
        : rate_(rate), burst_(burst), tokens_(static_cast<double>(burst)) {}

    /// Try to acquire one token. Returns true if allowed, false if rate-limited.
    bool TryAcquire() {
        std::lock_guard<std::mutex> lk(mtx_);
        Refill_();
        if (tokens_ >= 1.0) {
            --tokens_;
            return true;
        }
        return false;
    }

    /// Block until a token is available (or timeoutMs milliseconds).
    /// Returns true if acquired, false if timed out.
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
        int64_t  allowed    = 0;
        int64_t  rejected   = 0;
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
    void Refill_() const {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastRefill_).count();
        tokens_ = std::min(static_cast<double>(burst_), tokens_ + elapsed * rate_);
        lastRefill_ = now;
    }

    double              rate_;
    int32_t             burst_;
    mutable double      tokens_;
    mutable std::mutex  mtx_;
    mutable std::chrono::steady_clock::time_point lastRefill_ = std::chrono::steady_clock::now();
    std::atomic<int64_t> totalAllowed_{0};
    std::atomic<int64_t> totalRejected_{0};
};

// ── Per-service breaker registry ─────────────────────────────────

class CircuitBreakerRegistry {
public:
    using Ptr = std::shared_ptr<CircuitBreakerRegistry>;

    /// Get or create a breaker for the given service name.
    CircuitBreaker::Ptr GetOrCreate(const std::string& serviceName,
                                    const CircuitConfig& cfg = CircuitConfig()) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = breakers_.find(serviceName);
        if (it != breakers_.end()) return it->second;
        auto cb = std::make_shared<CircuitBreaker>(serviceName, cfg);
        breakers_[serviceName] = cb;
        return cb;
    }

    /// Convenience: call Allow() on the breaker for serviceName.
    /// Note: this uses the breaker's existing config. To set or update config,
    /// call Configure() or GetOrCreate() first.
    bool Allow(const std::string& serviceName) {
        return GetOrCreate(serviceName)->Allow();
    }

    /// Set or update the config for an existing breaker.
    void Configure(const std::string& serviceName, const CircuitConfig& cfg) {
        std::lock_guard<std::mutex> lk(mtx_);
        breakers_[serviceName] = std::make_shared<CircuitBreaker>(serviceName, cfg);
    }

    /// Convenience: RecordSuccess on the breaker for serviceName.
    void RecordSuccess(const std::string& serviceName) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = breakers_.find(serviceName);
        if (it != breakers_.end()) it->second->RecordSuccess();
    }

    /// Convenience: RecordFailure on the breaker for serviceName.
    void RecordFailure(const std::string& serviceName) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = breakers_.find(serviceName);
        if (it != breakers_.end()) it->second->RecordFailure();
    }

private:
    mutable std::mutex              mtx_;
    std::unordered_map<std::string, CircuitBreaker::Ptr> breakers_;
};

} // namespace circuit
} // namespace snc
