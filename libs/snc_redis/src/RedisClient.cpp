// Redis client — stub implementation.
// Compile with SNC_REDIS_CLIENT=ON to enable the real hiredis backend.
#include "snc/redis/Client.h"

#include <algorithm>
#include <map>
#include <mutex>

#ifdef SNC_REDIS_CLIENT
#include <hiredis/hiredis.h>
#endif

namespace snc {
namespace redis {

// ── Stub implementation ──────────────────────────────────────────

class StubClient : public IClient {
public:
    bool Connect(const std::string&, int) override { return true; }
    void Disconnect() override {}
    bool IsConnected() const override { return true; }

    std::string Set(const std::string& key, const std::string& value, int) override {
        store_[key] = value;
        return "OK";
    }
    std::string Get(const std::string& key) override {
        auto it = store_.find(key);
        return (it != store_.end()) ? it->second : "";
    }
    bool Del(const std::string& key) override { return store_.erase(key) > 0; }
    bool Exists(const std::string& key) override { return store_.count(key) > 0; }
    int64_t Incr(const std::string& key) override { return ++counter_[key]; }
    bool Expire(const std::string&, int) override { return true; }

    bool HSet(const std::string& key, const std::string& field, const std::string& value) override {
        hashes_[key][field] = value;
        return true;
    }
    std::string HGet(const std::string& key, const std::string& field) override {
        auto kit = hashes_.find(key);
        if (kit == hashes_.end()) return "";
        auto fit = kit->second.find(field);
        return (fit != kit->second.end()) ? fit->second : "";
    }
    bool HDel(const std::string& key, const std::string& field) override {
        auto kit = hashes_.find(key);
        if (kit == hashes_.end()) return false;
        return kit->second.erase(field) > 0;
    }
    std::vector<std::string> HMGet(const std::string& key,
                                   const std::vector<std::string>& fields) override {
        std::vector<std::string> out;
        auto kit = hashes_.find(key);
        for (auto& f : fields) {
            if (kit != hashes_.end()) {
                auto it = kit->second.find(f);
                out.push_back((it != kit->second.end()) ? it->second : "");
            } else {
                out.push_back("");
            }
        }
        return out;
    }

    int64_t LPush(const std::string&, const std::string&) override { return 0; }
    std::string RPop(const std::string&) override { return ""; }
    int64_t LLen(const std::string&) override { return 0; }

    bool ZAdd(const std::string&, double, const std::string&) override { return false; }
    std::string ZScore(const std::string&, const std::string&) override { return ""; }
    int64_t ZRem(const std::string&, const std::string&) override { return 0; }
    std::vector<std::string> ZRevRangeByScore(const std::string&, double, double, int, int) override {
        return {};
    }

private:
    std::map<std::string, std::string> store_;
    std::map<std::string, std::map<std::string, std::string>> hashes_;
    std::map<std::string, int64_t> counter_;
};

// ── Real hiredis implementation ──────────────────────────────────

#ifdef SNC_REDIS_CLIENT

class HiredisClient : public IClient {
public:
    bool Connect(const std::string& host, int port) override {
        context_ = redisConnect(host.c_str(), port);
        if (!context_ || context_->err) {
            Disconnect();
            return false;
        }
        connected_ = true;
        return true;
    }

    void Disconnect() override {
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        connected_ = false;
    }

    bool IsConnected() const override { return connected_ && context_ && !context_->err; }

    std::string Set(const std::string& key, const std::string& value, int ttlSeconds) override {
        if (!EnsureConnected()) return "";
        redisReply* reply = nullptr;
        if (ttlSeconds > 0) {
            reply = (redisReply*)redisCommand(context_, "SET %s %s EX %d",
                                              key.c_str(), value.c_str(), ttlSeconds);
        } else {
            reply = (redisReply*)redisCommand(context_, "SET %s %s",
                                              key.c_str(), value.c_str());
        }
        std::string result = replyString(reply);
        freeReplyObject(reply);
        return result;
    }

    std::string Get(const std::string& key) override {
        if (!EnsureConnected()) return "";
        redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
        std::string result = replyString(reply);
        freeReplyObject(reply);
        return result;
    }

    bool Del(const std::string& key) override {
        if (!EnsureConnected()) return false;
        redisReply* reply = (redisReply*)redisCommand(context_, "DEL %s", key.c_str());
        bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
        freeReplyObject(reply);
        return ok;
    }

    bool Exists(const std::string& key) override {
        if (!EnsureConnected()) return false;
        redisReply* reply = (redisReply*)redisCommand(context_, "EXISTS %s", key.c_str());
        bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
        freeReplyObject(reply);
        return ok;
    }

    int64_t Incr(const std::string& key) override {
        if (!EnsureConnected()) return 0;
        redisReply* reply = (redisReply*)redisCommand(context_, "INCR %s", key.c_str());
        int64_t val = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
        freeReplyObject(reply);
        return val;
    }

    bool Expire(const std::string& key, int seconds) override {
        if (!EnsureConnected()) return false;
        redisReply* reply = (redisReply*)redisCommand(context_, "EXPIRE %s %d", key.c_str(), seconds);
        bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
        freeReplyObject(reply);
        return ok;
    }

    bool HSet(const std::string& key, const std::string& field, const std::string& value) override {
        if (!EnsureConnected()) return false;
        redisReply* reply = (redisReply*)redisCommand(context_, "HSET %s %s %s",
                                                      key.c_str(), field.c_str(), value.c_str());
        bool ok = reply && reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return ok;
    }

    std::string HGet(const std::string& key, const std::string& field) override {
        if (!EnsureConnected()) return "";
        redisReply* reply = (redisReply*)redisCommand(context_, "HGET %s %s",
                                                      key.c_str(), field.c_str());
        std::string result = replyString(reply);
        freeReplyObject(reply);
        return result;
    }

    bool HDel(const std::string& key, const std::string& field) override {
        if (!EnsureConnected()) return false;
        redisReply* reply = (redisReply*)redisCommand(context_, "HDEL %s %s",
                                                      key.c_str(), field.c_str());
        bool ok = reply && reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return ok;
    }

    std::vector<std::string> HMGet(const std::string& key,
                                   const std::vector<std::string>& fields) override {
        if (!EnsureConnected()) return {};
        // Use individual HGET commands for each field
        std::vector<std::string> results;
        for (auto& f : fields) {
            results.push_back(HGet(key, f));
        }
        return results;
    }

    int64_t LPush(const std::string& key, const std::string& value) override {
        if (!EnsureConnected()) return 0;
        redisReply* reply = (redisReply*)redisCommand(context_, "LPUSH %s %s",
                                                      key.c_str(), value.c_str());
        int64_t len = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
        freeReplyObject(reply);
        return len;
    }

    std::string RPop(const std::string& key) override {
        if (!EnsureConnected()) return "";
        redisReply* reply = (redisReply*)redisCommand(context_, "RPOP %s", key.c_str());
        std::string result = replyString(reply);
        freeReplyObject(reply);
        return result;
    }

    int64_t LLen(const std::string& key) override {
        if (!EnsureConnected()) return 0;
        redisReply* reply = (redisReply*)redisCommand(context_, "LLEN %s", key.c_str());
        int64_t len = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
        freeReplyObject(reply);
        return len;
    }

    bool ZAdd(const std::string& key, double score, const std::string& member) override {
        if (!EnsureConnected()) return false;
        char scoreStr[64];
        snprintf(scoreStr, sizeof(scoreStr), "%f", score);
        redisReply* reply = (redisReply*)redisCommand(context_, "ZADD %s %s %s",
                                                      key.c_str(), scoreStr, member.c_str());
        bool ok = reply && reply->type == REDIS_REPLY_INTEGER;
        freeReplyObject(reply);
        return ok;
    }

    std::string ZScore(const std::string& key, const std::string& member) override {
        if (!EnsureConnected()) return "";
        redisReply* reply = (redisReply*)redisCommand(context_, "ZSCORE %s %s",
                                                      key.c_str(), member.c_str());
        std::string result = replyString(reply);
        freeReplyObject(reply);
        return result;
    }

    int64_t ZRem(const std::string& key, const std::string& member) override {
        if (!EnsureConnected()) return 0;
        redisReply* reply = (redisReply*)redisCommand(context_, "ZREM %s %s",
                                                      key.c_str(), member.c_str());
        int64_t n = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
        freeReplyObject(reply);
        return n;
    }

    std::vector<std::string> ZRevRangeByScore(const std::string& key,
                                              double maxScore, double minScore,
                                              int offset, int count) override {
        if (!EnsureConnected()) return {};
        char maxStr[64], minStr[64];
        snprintf(maxStr, sizeof(maxStr), "%f", maxScore);
        snprintf(minStr, sizeof(minStr), "%f", minScore);
        redisReply* reply = (redisReply*)redisCommand(context_,
            "ZREVRANGEBYSCORE %s %s %s WITHSCORES LIMIT %d %d",
            key.c_str(), maxStr, minStr, offset, count);
        std::vector<std::string> result;
        if (reply && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                result.push_back(reply->element[i]->str
                    ? std::string(reply->element[i]->str, reply->element[i]->strlen) : "");
            }
        }
        freeReplyObject(reply);
        return result;
    }

private:
    bool EnsureConnected() {
        if (!connected_ || !context_ || context_->err) {
            return false;
        }
        return true;
    }

    static std::string replyString(redisReply* reply) {
        if (!reply) return "";
        if (reply->type == REDIS_REPLY_STRING && reply->str) {
            return std::string(reply->str, reply->strlen);
        }
        if (reply->type == REDIS_REPLY_NIL) return "";
        return "";
    }

    redisContext* context_ = nullptr;
    bool connected_ = false;
};

#endif // SNC_REDIS_CLIENT

// ── Factory ──────────────────────────────────────────────────────

std::shared_ptr<IClient> IClient::Create() {
#ifdef SNC_REDIS_CLIENT
    return std::make_shared<HiredisClient>();
#else
    return std::make_shared<StubClient>();
#endif
}

} // namespace redis
} // namespace snc
