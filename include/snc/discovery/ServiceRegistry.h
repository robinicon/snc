#pragma once

/// @file ServiceRegistry.h
/// @brief Thread-safe, process-local service registry with load-balancing support.
///
/// ServiceRegistry is a pure-STL in-process registry for tracking service
/// endpoints. It supports:
///   - Register / Unregister endpoints
///   - Health flag management (for graceful shutdown)
///   - Three load-balancing strategies: ROUND_ROBIN, RANDOM, WEIGHTED
///
/// No external dependencies; safe for use in unit tests and production.

#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>

namespace snc {
namespace discovery {

/// A registered service instance (one endpoint of a service).
struct ServiceEntry {
    std::string serviceName;  ///< e.g. "login", "map"
    std::string host;         ///< IP address or hostname
    uint16_t    port;         ///< TCP port
    int32_t     weight;       ///< load balancing weight (default 1)
    bool        healthy;      ///< true if the endpoint is accepting traffic

    ServiceEntry() : port(0), weight(1), healthy(true) {}
    ServiceEntry(const std::string& name, const std::string& h, uint16_t p, int w = 1)
        : serviceName(name), host(h), port(p), weight(w), healthy(true) {}
};

using ServiceEntryPtr = std::shared_ptr<ServiceEntry>;
using ServiceList = std::vector<ServiceEntryPtr>;

/// Load-balancing strategies for endpoint selection.
enum class BalanceStrategy : int {
    ROUND_ROBIN = 0,  ///< Cyclic selection across healthy endpoints
    RANDOM      = 1,  ///< Pseudo-random selection (thread-safe counter-based)
    WEIGHTED    = 2,  ///< Selection proportional to endpoint weight
};

/// Process-local service registry — thread-safe for concurrent register/unregister/query.
///
/// Pure STL implementation with no external dependencies. Suitable for both
/// unit tests (stub registry) and production (in-process service discovery).
class ServiceRegistry {
public:
    using Ptr = std::shared_ptr<ServiceRegistry>;

    virtual ~ServiceRegistry() = default;
    static Ptr Create() { return std::make_shared<ServiceRegistry>(); }

    // ── Register / Unregister ──────────────────────────────────

    /// Register a service endpoint. If an entry with the same (name, host, port) exists,
    /// its healthy flag is refreshed and weight is updated.
    virtual void Register(const std::string& serviceName,
                  const std::string& host,
                  uint16_t port,
                  int32_t weight = 1) {
        auto entry = std::make_shared<ServiceEntry>(serviceName, host, port, weight);
        std::lock_guard<std::mutex> lk(mtx_);
        auto& bucket = buckets_[serviceKey(serviceName, host, port)];
        bool found = false;
        for (auto& e : bucket) {
            if (e->host == host && e->port == port) {
                e->healthy = true;
                e->weight = weight;
                found = true;
                break;
            }
        }
        if (!found) {
            bucket.push_back(entry);
            services_[serviceName].push_back(entry);
        }
    }

    /// Unregister a specific endpoint by (name, host, port).
    virtual void Unregister(const std::string& serviceName,
                    const std::string& host,
                    uint16_t port) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto key = serviceKey(serviceName, host, port);
        auto it = buckets_.find(key);
        if (it == buckets_.end()) return;
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
            [&](const ServiceEntryPtr& e) {
                return e->host == host && e->port == port;
            }), list.end());
        auto sit = services_.find(serviceName);
        if (sit != services_.end()) {
            sit->second.erase(
                std::remove_if(sit->second.begin(), sit->second.end(),
                    [&](const ServiceEntryPtr& e) {
                        return e->host == host && e->port == port;
                    }),
                sit->second.end());
        }
    }

    /// Mark all endpoints of a service as unhealthy (used for graceful shutdown).
    virtual void MarkUnhealthy(const std::string& serviceName) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = services_.find(serviceName);
        if (it == services_.end()) return;
        for (auto& e : it->second) e->healthy = false;
    }

    /// Remove all endpoints of a service from the registry.
    virtual void DeregisterAll(const std::string& serviceName) {
        std::lock_guard<std::mutex> lk(mtx_);
        services_.erase(serviceName);
        std::vector<std::string> keysToRemove;
        for (auto& [k, list] : buckets_) {
            list.erase(std::remove_if(list.begin(), list.end(),
                [&](const ServiceEntryPtr& e) {
                    return e->serviceName == serviceName;
                }), list.end());
            if (list.empty()) keysToRemove.push_back(k);
        }
        for (auto& k : keysToRemove) buckets_.erase(k);
    }

    // ── Query ──────────────────────────────────────────────────

    /// Get all healthy endpoints for a service.
    ServiceList GetHealthy(const std::string& serviceName) const {
        std::lock_guard<std::mutex> lk(mtx_);
        ServiceList result;
        auto it = services_.find(serviceName);
        if (it == services_.end()) return result;
        for (auto& e : it->second) {
            if (e->healthy) result.push_back(e);
        }
        return result;
    }

    /// Get all endpoints (healthy + unhealthy) for a service.
    ServiceList GetAll(const std::string& serviceName) const {
        std::lock_guard<std::mutex> lk(mtx_);
        ServiceList result;
        auto it = services_.find(serviceName);
        if (it == services_.end()) return result;
        result = it->second;
        return result;
    }

    /// Pick one healthy endpoint using the given strategy.
    /// Returns nullptr if no healthy endpoint is available.
    ServiceEntryPtr Pick(BalanceStrategy strategy = BalanceStrategy::ROUND_ROBIN,
                         const std::string& serviceName = "") const {
        std::lock_guard<std::mutex> lk(mtx_);
        ServiceList healthy;
        if (serviceName.empty()) {
            for (auto& [name, list] : services_) {
                for (auto& e : list) {
                    if (e->healthy) healthy.push_back(e);
                }
            }
        } else {
            auto it = services_.find(serviceName);
            if (it != services_.end()) {
                for (auto& e : it->second) {
                    if (e->healthy) healthy.push_back(e);
                }
            }
        }
        if (healthy.empty()) return nullptr;

        switch (strategy) {
            case BalanceStrategy::ROUND_ROBIN: {
                size_t idx = rrCounter_++ % healthy.size();
                return healthy[idx];
            }
    case BalanceStrategy::RANDOM: {
                std::uniform_int_distribution<size_t> dist(0, healthy.size() - 1);
                size_t idx = dist(rng_);
                return healthy[idx];
            }
            case BalanceStrategy::WEIGHTED: {
                int totalWeight = 0;
                for (auto& e : healthy) totalWeight += e->weight;
                if (totalWeight <= 0) totalWeight = (int)healthy.size();
                size_t hash = std::hash<std::string>{}(
                    serviceName + std::to_string(rrCounter_++));
                int target = (int)(hash % (size_t)totalWeight);
                int accumulated = 0;
                for (auto& e : healthy) {
                    accumulated += e->weight;
                    if (target < accumulated) return e;
                }
                return healthy.back();
            }
        }
        return healthy[0];
    }

    // ── Statistics ─────────────────────────────────────────────

    /// Total number of distinct registered service names.
    size_t ServiceCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return services_.size();
    }

    /// Total number of endpoints across all services.
    size_t EndpointCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        size_t count = 0;
        for (auto& [name, list] : services_) count += list.size();
        return count;
    }

    /// List all registered service names.
    std::vector<std::string> ListServices() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::string> names;
        names.reserve(services_.size());
        for (auto& [name, entries] : services_) names.push_back(name);
        return names;
    }

    // ── Health Check (caller invokes periodically) ─────────────

    /// Refresh health of all registered endpoints to healthy.
    /// Called by the external health checker thread during periodic checks.
    void RefreshHealth() {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [name, list] : services_) {
            for (auto& e : list) e->healthy = true;
        }
    }

private:
    static std::string serviceKey(const std::string& name,
                                  const std::string& host, uint16_t port) {
        return name + ":" + host + ":" + std::to_string(port);
    }

    mutable std::mutex mtx_;
    std::unordered_map<std::string, ServiceList> services_;   // name -> entries
    std::unordered_map<std::string, ServiceList> buckets_;    // key -> entries (dedup)
    mutable std::atomic<size_t> rrCounter_{0};
    mutable std::mt19937 rng_{std::random_device{}()};
};

} // namespace discovery
} // namespace snc
