// SPDX-License-Identifier: MIT
// Redis bridge for cross-process service discovery.
//
// Stub mode (default): all methods are no-ops. Use the in-memory
// ServiceRegistry directly.
//
// Real mode (SNC_REDIS_CLIENT): requires hiredis compiled into the
// project. Set SNC_REDIS_CLIENT=ON and link against hiredis.
#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace snc {
namespace discovery {

/// Minimal Redis command interface. Implementations:
///   - StubRedisClient: no-op, used when hiredis is unavailable
///   - HiredisClient: real implementation (requires hiredis)
class IRedisClient {
public:
    virtual ~IRedisClient() = default;
    virtual bool Connect(const std::string& host, int port) = 0;
    virtual void Disconnect() {}
    virtual std::string Set(const std::string& key, const std::string& value, int ttlSeconds = 30) = 0;
    virtual std::string Get(const std::string& key) = 0;
    virtual bool Del(const std::string& key) = 0;
    virtual bool Exists(const std::string& key) = 0;
    virtual bool Ping() { return false; }
    virtual bool IsConnected() const { return false; }
};

/// Stub implementation — all operations succeed silently.
class StubRedisClient : public IRedisClient {
public:
    bool Connect(const std::string&, int) override { return true; }
    void Disconnect() override {}
    std::string Set(const std::string&, const std::string&, int) override { return "OK"; }
    std::string Get(const std::string&) override { return ""; }
    bool Del(const std::string&) override { return true; }
    bool Exists(const std::string&) override { return false; }
    bool Ping() override { return true; }
    bool IsConnected() const override { return true; }
};

/// Factory — returns a real client if SNC_REDIS_CLIENT is defined,
/// otherwise returns a stub client.
inline std::shared_ptr<IRedisClient> CreateRedisClient() {
    return std::make_shared<StubRedisClient>();
}

} // namespace discovery
} // namespace snc
