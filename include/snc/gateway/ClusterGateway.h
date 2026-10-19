// SPDX-License-Identifier: MIT
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
#include "snc/gateway/CircuitBreaker.h"
#include "snc/rpc/RpcClient.h"
#include "snc/base/Log.h"

namespace snc {
namespace gateway {

using discovery::ServiceRegistry;
using discovery::ServiceEntry;
using discovery::ServiceEntryPtr;
using rpc::RpcClient;
using rpc::RpcError;
using gateway::CircuitBreaker;
using gateway::CircuitBreakerRegistry;

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
    explicit ClusterGateway(ServiceRegistry::Ptr registry);

    /// Register an outgoing connection to a backend service.
    /// `serviceName` is the key used to look up endpoints in the registry.
    /// Thread-safe.
    void AddBackend(const std::string& serviceName, RpcClient::Ptr client);

    /// Send an RPC request through the gateway.
    /// Picks a healthy endpoint from the registry, checks the circuit breaker,
    /// and forwards the call to the registered client.
    /// Returns true if the call was forwarded, false if rejected
    /// (no endpoint available or circuit breaker is open).
    bool Forward(const std::string& serviceName, int32_t msgId,
                 const char* payload, size_t len, RpcResult reply);

    /// Statistics for a single backend route.
    struct RouteStats {
        int64_t requests     = 0; ///< Total forwarded requests
        int64_t successes    = 0; ///< Successful responses received
        int64_t failures     = 0; ///< Failed responses received
        int64_t circuitOpen  = 0; ///< Times the circuit breaker rejected the request
        int     healthyEndpoints = 0; ///< Current healthy endpoint count in registry
    };

    /// Get stats for a backend route. Thread-safe.
    RouteStats GetStats(const std::string& serviceName) const;

    /// List all registered backend service names. Thread-safe.
    std::vector<std::string> ListBackends() const;

    /// (Test-only) Expose the breaker for a backend — used by unit tests.
    CircuitBreaker::Ptr GetBreakerForTest(const std::string& serviceName) const;

private:
    BackendRoutePtr GetRoute(const std::string& serviceName) const;
    ServiceEntryPtr PickEndpoint(const std::string& serviceName) const;

    mutable std::mutex              mtx_;
    ServiceRegistry::Ptr            registry_;
    CircuitBreakerRegistry          breakerRegistry_;
    std::unordered_map<std::string, BackendRoutePtr> backends_;
};

} // namespace gateway
} // namespace snc
