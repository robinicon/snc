// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace snc {
namespace redis {

/// Unified Redis client interface.
/// Real implementation (snc_redis_client): uses hiredis.
/// Stub implementation (snc_redis_stub): no-op, all ops succeed silently.
class IClient {
public:
    virtual ~IClient() = default;

    virtual bool Connect(const std::string& host, int port) = 0;
    virtual void Disconnect() {}
    virtual bool IsConnected() const = 0;

    // ── Core commands ────────────────────────────────────────────
    virtual std::string Set(const std::string& key, const std::string& value, int ttlSeconds = 0) = 0;
    virtual std::string Get(const std::string& key) = 0;
    virtual bool Del(const std::string& key) = 0;
    virtual bool Exists(const std::string& key) = 0;
    virtual int64_t Incr(const std::string& key) = 0;
    virtual bool Expire(const std::string& key, int seconds) = 0;
    virtual std::string Ping() { return "PONG"; }

    // ── Hash commands ────────────────────────────────────────────
    virtual bool HSet(const std::string& key, const std::string& field, const std::string& value) = 0;
    virtual std::string HGet(const std::string& key, const std::string& field) = 0;
    virtual bool HDel(const std::string& key, const std::string& field) = 0;
    virtual std::vector<std::string> HMGet(const std::string& key,
                                           const std::vector<std::string>& fields) = 0;

    // ── List commands ────────────────────────────────────────────
    virtual int64_t LPush(const std::string& key, const std::string& value) = 0;
    virtual std::string RPop(const std::string& key) = 0;
    virtual int64_t LLen(const std::string& key) = 0;

    // ── Sorted set commands ──────────────────────────────────────
    virtual bool ZAdd(const std::string& key, double score, const std::string& member) = 0;
    virtual std::string ZScore(const std::string& key, const std::string& member) = 0;
    virtual int64_t ZRem(const std::string& key, const std::string& member) = 0;
    // ZREVRANGEBYSCORE with LIMIT: returns members in descending score order
    virtual std::vector<std::string> ZRevRangeByScore(const std::string& key,
                                                       double maxScore, double minScore,
                                                       int offset, int count) = 0;

    // ── Pub/Sub (async callback) ─────────────────────────────────
    using MessageCallback = std::function<void(const std::string& channel,
                                                const std::string& message)>;
    virtual bool Subscribe(const std::string& channel, MessageCallback cb) { (void)channel; (void)cb; return false; }
    virtual bool Unsubscribe(const std::string& channel) { (void)channel; return false; }

    /// Create a real client when hiredis is available, otherwise a stub.
    static std::shared_ptr<IClient> Create();
};

} // namespace redis
} // namespace snc
