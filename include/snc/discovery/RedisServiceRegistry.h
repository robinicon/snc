// Cross-process service registry using Redis as the backend.
//
// Extends the in-memory ServiceRegistry by syncing registration state
// to/from Redis using service-specific keys:
//   snc:service:{name}:{host}:{port}  → JSON payload with TTL
//
// Call UpdateFromRedis() periodically (e.g. every 5s) to refresh
// local state from the cluster. Call Register() / Unregister() and
// they will be propagated to Redis as well.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "snc/discovery/ServiceRegistry.h"
#include "snc/discovery/RedisBridge.h"
#include "snc/base/Log.h"

namespace snc {
namespace discovery {

/// Service state stored in Redis (simple key-value, not real JSON).
/// Format: "host:port:weight:healthy"
struct RedisSyncState {
    std::string serviceName;
    std::string host;
    uint16_t    port;
    int32_t     weight;
    bool        healthy;
};

/// Cross-process service registry backed by Redis.
/// Local ServiceRegistry is the source of truth; Redis is the sync layer.
class RedisServiceRegistry : public ServiceRegistry {
public:
    using Ptr     = std::shared_ptr<RedisServiceRegistry>;
    using HeartbeatFn = std::function<void(const RedisSyncState&)>;
    using RefreshFn   = std::function<std::vector<RedisSyncState>()>;

    /// ttlSeconds: how long an entry lives without a heartbeat refresh.
    explicit RedisServiceRegistry(std::shared_ptr<IRedisClient> redis,
                                  int ttlSeconds = 30)
        : redis_(std::move(redis)), ttlSeconds_(ttlSeconds), stopHeartbeat_(false) {}

    ~RedisServiceRegistry() {
        stopHeartbeat_ = true;
        if (heartbeatThread_.joinable()) heartbeatThread_.join();
    }

    // ── Override Register to also sync to Redis ──────────────────

    void Register(const std::string& serviceName,
                  const std::string& host,
                  uint16_t port,
                  int32_t weight = 1) override {
        ServiceRegistry::Register(serviceName, host, port, weight);
        SyncToRedis(serviceName, host, port, weight, true);
        StartHeartbeatIfNeeded();
    }

    void Unregister(const std::string& serviceName,
                    const std::string& host,
                    uint16_t port) override {
        ServiceRegistry::Unregister(serviceName, host, port);
        SyncToRedis(serviceName, host, port, 0, false);
    }

    void MarkUnhealthy(const std::string& serviceName) override {
        ServiceRegistry::MarkUnhealthy(serviceName);
        // De-sync from Redis (let TTL expire)
        for (auto& e : GetAll(serviceName)) {
            if (e->healthy) {
                SyncToRedis(e->serviceName, e->host, e->port, e->weight, false);
            }
        }
    }

    void DeregisterAll(const std::string& serviceName) override {
        ServiceRegistry::DeregisterAll(serviceName);
        for (auto& e : GetAll(serviceName)) {
            SyncToRedis(serviceName, e->host, e->port, 0, false);
        }
    }

    // ── Cross-process sync ───────────────────────────────────────

    /// Pull current state from Redis and reconcile with local registry.
    /// Call this periodically (e.g. every 5 seconds) from a background thread.
    void UpdateFromRedis() {
        if (!redis_) return;
        auto remote = RefreshLocal_();
        // Reconcile: register anything new or changed
        for (const auto& s : remote) {
            if (!ExistsLocally_(s.serviceName, s.host, s.port)) {
                ServiceRegistry::Register(s.serviceName, s.host, s.port, s.weight);
            } else if (s.healthy) {
                // Ensure healthy flag matches
                auto entries = GetHealthy(s.serviceName);
                for (auto& e : entries) {
                    if (e->host == s.host && e->port == s.port) {
                        e->healthy = s.healthy;
                        break;
                    }
                }
            }
        }
    }

    // ── Callbacks ────────────────────────────────────────────────

    /// Called after each local Register() to push to Redis.
    void SetHeartbeatCallback(HeartbeatFn cb) { heartbeatCb_ = std::move(cb); }

    /// Called by UpdateFromRedis() to fetch remote state.
    void SetRefreshCallback(RefreshFn cb) { refreshCb_ = std::move(cb); }

private:
    // ── Key helpers ──────────────────────────────────────────────

    static std::string MakeKey(const std::string& serviceName,
                               const std::string& host, uint16_t port) {
        return "snc:service:" + serviceName + ":" + host + ":" + std::to_string(port);
    }

    static std::string SerializeState(const RedisSyncState& s) {
        std::ostringstream oss;
        oss << s.host << ":" << s.port << ":" << s.weight << ":" << (s.healthy ? "1" : "0");
        return oss.str();
    }

    static RedisSyncState DeserializeState(const std::string& serviceName,
                                           const std::string& val) {
        RedisSyncState s{};
        s.serviceName = serviceName;
        // Parse "host:port:weight:healthy"
        std::istringstream iss(val);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(iss, token, ':')) parts.push_back(token);
        if (parts.size() >= 4) {
            s.host     = parts[0];
            s.port     = static_cast<uint16_t>(std::stoi(parts[1]));
            s.weight   = std::stoi(parts[2]);
            s.healthy  = parts[3] == "1";
        }
        return s;
    }

    // ── Redis I/O ────────────────────────────────────────────────

    void SyncToRedis(const std::string& serviceName,
                     const std::string& host,
                     uint16_t port,
                     int32_t weight,
                     bool healthy) {
        if (!redis_) return;
        RedisSyncState state{serviceName, host, port, weight, healthy};
        if (heartbeatCb_) {
            heartbeatCb_(state);
        } else {
            auto key   = MakeKey(serviceName, host, port);
            auto value = SerializeState(state);
            redis_->Set(key, value, healthy ? ttlSeconds_ : 0);
        }
    }

    std::vector<RedisSyncState> RefreshLocal_() {
        if (refreshCb_) return refreshCb_();
        // No callback set — return empty (no cross-process data)
        return {};
    }

    bool ExistsLocally_(const std::string& name, const std::string& host, uint16_t port) const {
        auto entries = GetAll(name);
        for (auto& e : entries) {
            if (e->host == host && e->port == port) return true;
        }
        return false;
    }

    // ── Heartbeat thread ─────────────────────────────────────────

    void StartHeartbeatIfNeeded() {
        if (heartbeatThread_.joinable()) return;
        heartbeatThread_ = std::thread([this]() {
            while (!stopHeartbeat_.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                // Re-push heartbeats for all local entries
                for (auto& name : ListServices()) {
                    auto entries = GetAll(name);
                    for (auto& e : entries) {
                        if (e->healthy) {
                            SyncToRedis(e->serviceName, e->host, e->port,
                                        e->weight, true);
                        }
                    }
                }
            }
        });
    }

    // ── Members ──────────────────────────────────────────────────

    std::shared_ptr<IRedisClient> redis_;
    int                           ttlSeconds_;
    HeartbeatFn                   heartbeatCb_;
    RefreshFn                     refreshCb_;
    std::atomic<bool>             stopHeartbeat_{false};
    std::thread                   heartbeatThread_;
};

} // namespace discovery
} // namespace snc
