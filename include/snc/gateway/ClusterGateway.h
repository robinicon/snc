#pragma once

/// @file ClusterGateway.h
/// @brief Load-balanced RPC gateway with circuit-breaker protection.
///
/// ClusterGateway forwards requests to backend services using the ServiceRegistry
/// for endpoint discovery and the CircuitBreakerRegistry for per-backend fault
/// isolation. It provides:
///   - Round-robin endpoint selection via the service registry.
///   - Automatic circuit-breaking when a backend exceeds its error threshold.
///   - Per-route request/success/failure/circuit-open statistics.
///
/// Usage:
///   auto registry = ServiceRegistry::Create();
///   auto gateway  = std::make_shared<ClusterGateway>(registry);
///   gateway->AddBackend("login", client);
///   gateway->Forward("login", MSG_ID_LOGIN, payload, len, reply);

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "snc/discovery/ServiceRegistry.h"
#include "snc/circuit/CircuitBreaker.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcSession.h"
#include "snc/base/Log.h"

namespace snc {
namespace gateway {

using discovery::ServiceRegistry;
using discovery::ServiceEntry;
using discovery::ServiceEntryPtr;
using rpc::RpcClient;
using rpc::RpcSession;
using rpc::RpcError;
using circuit::CircuitBreaker;
using circuit::CircuitBreakerRegistry;

/// A single backend route entry: maps a service name to its client, breaker, and stats.
struct BackendRoute {
    std::string            serviceName;    ///< Service name (key in registry)
    RpcClient::Ptr         client;         ///< RPC client connected to a backend
    CircuitBreaker::Ptr    breaker;        ///< Per-backend circuit breaker
    std::atomic<int64_t>   requests{0};    ///< Total forwarded requests
    std::atomic<int64_t>   successes{0};   ///< Successful responses
    std::atomic<int64_t>   failures{0};    ///< Failed responses
    std::atomic<int64_t>   circuitOpen{0}; ///< Times circuit was open (rejected)
};

using BackendRoutePtr = std::shared_ptr<BackendRoute>;

/// Cluster gateway: forwards requests to backend services with discovery + protection.
///
/// Thread-safe: Forward() does not hold the lock during the RPC call.
/// GetStats() and ListBackends() acquire the lock for consistent reads.
class ClusterGateway {
public:
    using Ptr       = std::shared_ptr<ClusterGateway>;
    /// Reply callback signature: invoked when a response arrives or an error occurs.
    using RpcResult = std::function<void(RpcError, const uint8_t*, size_t)>;

    /// Construct with the shared service registry.
    explicit ClusterGateway(ServiceRegistry::Ptr registry)
        : registry_(std::move(registry)) {}

    /// Register an outgoing connection to a backend service.
    /// `serviceName` is the key used to look up endpoints in the registry.
    /// Thread-safe.
    void AddBackend(const std::string& serviceName, RpcClient::Ptr client) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto route = std::make_shared<BackendRoute>();
        route->serviceName  = serviceName;
        route->client       = std::move(client);
        route->breaker      = breakerRegistry_.GetOrCreate(serviceName);
        backends_[serviceName] = route;
        SNC_LOG_INFO("Gateway: registered backend '%s'", serviceName.c_str());
    }

    /// Send an RPC request through the gateway.
    /// Picks a healthy endpoint from the registry, checks the circuit breaker,
    /// and forwards the call to the registered client.
    /// Returns true if the call was forwarded, false if rejected
    /// (no endpoint available or circuit breaker is open).
    bool Forward(const std::string& serviceName, int32_t msgId,
                 const char* payload, size_t len, RpcResult reply) {
        auto route = GetRoute(serviceName);
        if (!route) {
            SNC_LOG_WARN("Gateway: no backend for '%s'", serviceName.c_str());
            return false;
        }

        // Circuit breaker check
        if (!route->breaker->Allow()) {
            ++route->circuitOpen;
            reply(RpcError::SERVICE_UNAVAILABLE, nullptr, 0);
            return false;
        }

        // Pick a healthy endpoint from registry
        auto endpoint = PickEndpoint(serviceName);
        if (!endpoint) {
            SNC_LOG_WARN("Gateway: no healthy endpoint for '%s'", serviceName.c_str());
            reply(RpcError::SERVICE_UNAVAILABLE, nullptr, 0);
            return false;
        }

        ++route->requests;

        // Wrap the reply callback to update breaker + stats
        auto wrapped = [route, reply](RpcError err, const uint8_t* data, size_t sz) {
            if (err == RpcError::OK) {
                route->breaker->RecordSuccess();
                ++route->successes;
            } else {
                route->breaker->RecordFailure();
                ++route->failures;
            }
            reply(err, data, sz);
        };

        if (!route->client) {
            reply(RpcError::SERVICE_UNAVAILABLE, nullptr, 0);
            return false;
        }

        route->client->Call(msgId, payload, len, std::move(wrapped));
        return true;
    }

    /// Statistics for a single backend route.
    struct RouteStats {
        int64_t requests   = 0; ///< Total forwarded requests
        int64_t successes  = 0; ///< Successful responses received
        int64_t failures   = 0; ///< Failed responses received
        int64_t circuitOpen = 0; ///< Times the circuit breaker rejected the request
        int     healthyEndpoints = 0; ///< Current healthy endpoint count in registry
    };

    /// Get stats for a backend route. Thread-safe.
    RouteStats GetStats(const std::string& serviceName) const {
        RouteStats s{};
        auto route = GetRoute(serviceName);
        if (route) {
            s.requests   = route->requests.load();
            s.successes  = route->successes.load();
            s.failures   = route->failures.load();
            s.circuitOpen = route->circuitOpen.load();
        }
        auto endpoints = registry_ ? registry_->GetHealthy(serviceName) : std::vector<ServiceEntryPtr>{};
        s.healthyEndpoints = static_cast<int>(endpoints.size());
        return s;
    }

    /// List all registered backend service names. Thread-safe.
    std::vector<std::string> ListBackends() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::string> names;
        for (auto& [name, _] : backends_) names.push_back(name);
        return names;
    }

    /// (Test-only) Expose the breaker for a backend — used by unit tests.
    CircuitBreaker::Ptr GetBreakerForTest(const std::string& serviceName) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = backends_.find(serviceName);
        return (it != backends_.end()) ? it->second->breaker : nullptr;
    }

private:
    BackendRoutePtr GetRoute(const std::string& serviceName) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = backends_.find(serviceName);
        return (it != backends_.end()) ? it->second : nullptr;
    }

    ServiceEntryPtr PickEndpoint(const std::string& serviceName) const {
        if (!registry_) return nullptr;
        return registry_->Pick(discovery::BalanceStrategy::ROUND_ROBIN, serviceName);
    }

    mutable std::mutex              mtx_;
    ServiceRegistry::Ptr            registry_;
    CircuitBreakerRegistry          breakerRegistry_;
    std::unordered_map<std::string, BackendRoutePtr> backends_;
};

} // namespace gateway
} // namespace snc
