// SPDX-License-Identifier: MIT
/// @file ClusterGateway.cpp
/// @brief Load-balanced RPC gateway implementation with circuit-breaker protection.

#include "snc/gateway/ClusterGateway.h"
#include <algorithm>

namespace snc {
namespace gateway {

// ── Constructor ─────────────────────────────────────────────────

ClusterGateway::ClusterGateway(ServiceRegistry::Ptr registry)
    : registry_(std::move(registry)) {}

// ── AddBackend ──────────────────────────────────────────────────

void ClusterGateway::AddBackend(const std::string& serviceName, RpcClient::Ptr client) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto route = std::make_shared<BackendRoute>();
    route->serviceName = serviceName;
    route->client      = std::move(client);
    route->breaker     = breakerRegistry_.GetOrCreate(serviceName);
    backends_[serviceName] = route;
    SNC_LOG_INFO("Gateway: registered backend '%s'", serviceName.c_str());
}

// ── Forward ─────────────────────────────────────────────────────

bool ClusterGateway::Forward(const std::string& serviceName, int32_t msgId,
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

// ── GetStats ────────────────────────────────────────────────────

ClusterGateway::RouteStats ClusterGateway::GetStats(const std::string& serviceName) const {
    RouteStats s{};
    auto route = GetRoute(serviceName);
    if (route) {
        s.requests     = route->requests.load();
        s.successes    = route->successes.load();
        s.failures     = route->failures.load();
        s.circuitOpen  = route->circuitOpen.load();
    }
    auto endpoints = registry_ ? registry_->GetHealthy(serviceName)
                               : std::vector<ServiceEntryPtr>{};
    s.healthyEndpoints = static_cast<int>(endpoints.size());
    return s;
}

// ── ListBackends ────────────────────────────────────────────────

std::vector<std::string> ClusterGateway::ListBackends() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> names;
    for (auto& [name, _] : backends_) names.push_back(name);
    return names;
}

// ── GetBreakerForTest ───────────────────────────────────────────

CircuitBreaker::Ptr ClusterGateway::GetBreakerForTest(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = backends_.find(serviceName);
    return (it != backends_.end()) ? it->second->breaker : nullptr;
}

// ── Private helpers ─────────────────────────────────────────────

BackendRoutePtr ClusterGateway::GetRoute(const std::string& serviceName) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = backends_.find(serviceName);
    return (it != backends_.end()) ? it->second : nullptr;
}

ServiceEntryPtr ClusterGateway::PickEndpoint(const std::string& serviceName) const {
    if (!registry_) return nullptr;
    return registry_->Pick(discovery::BalanceStrategy::ROUND_ROBIN, serviceName);
}

} // namespace gateway
} // namespace snc
