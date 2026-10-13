#pragma once

// snc_admin — Admin HTTP API scaffolding.
//
// Provides generic route registration and JSON response helpers.
// All business operations (player queries, resource changes, etc.)
// are driven by user-provided callbacks — no game-specific logic here.

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"

namespace snc {
namespace admin {

// ── Generic player record ───────────────────────────────────────
// Fields are intentionally generic. Business semantics (gold/diamond/exp)
// are defined in the application layer.

struct PlayerRecord {
    std::string user_id;
    std::string nickname;
    std::unordered_map<std::string, int64_t> resources;  // key = resource name
    std::unordered_map<std::string, std::string> extra;  // any additional fields
    bool is_banned = false;
    int64_t last_login_ts = 0;
    std::string register_time;
};

// ── Server stats (generic) ──────────────────────────────────────

struct ServerStats {
    int  online_count      = 0;
    int  ws_connections    = 0;
    int  http_requests_per_sec = 0;
    int  avg_latency_ms    = 0;
    int  error_rate_pct    = 0;
};

// ── Operation log ───────────────────────────────────────────────

struct OpLogEntry {
    int64_t timestamp;
    std::string operator_id;
    std::string action;
    std::string target_id;
    std::string detail;
};

// ── AdminService ────────────────────────────────────────────────
//
// All data operations are provided via callbacks. The service only:
//   1. Registers HTTP routes
//   2. Parses requests
//   3. Calls user callbacks
//   4. Returns JSON responses

class AdminService {
public:
    struct Config {
        // Role required in JWT claims to access admin endpoints.
        std::string admin_role = "admin";
        // Max log entries to keep in memory (capped).
        int max_log_entries = 10000;
    };

    explicit AdminService(const Config& cfg);

    using PlayerQueryCb = std::function<PlayerRecord(const std::string& user_id)>;
    using BanCallback   = std::function<bool(const std::string& user_id, bool banned,
                                             const std::string& operator_id,
                                             const std::string& reason)>;
    using ResourceCb    = std::function<bool(const std::string& user_id,
                                             const std::string& resource,
                                             int64_t delta,
                                             const std::string& operator_id)>;
    using StatsCb       = std::function<ServerStats()>;
    using LogAppendCb   = std::function<void(const OpLogEntry&)>;
    using LogQueryCb    = std::function<std::vector<OpLogEntry>(int limit)>;

    // Register all callbacks before calling RegisterRoutes().
    void SetPlayerQueryCb(PlayerQueryCb cb)  { player_query_cb_ = std::move(cb); }
    void SetBanCb(BanCallback cb)             { ban_cb_ = std::move(cb); }
    void SetResourceCb(ResourceCb cb)         { resource_cb_ = std::move(cb); }
    void SetStatsCb(StatsCb cb)               { stats_cb_ = std::move(cb); }
    void SetLogAppendCb(LogAppendCb cb)       { log_append_cb_ = std::move(cb); }
    void SetLogQueryCb(LogQueryCb cb)         { log_query_cb_ = std::move(cb); }

    // Register admin routes on the given router.
    // auth_svc is used only to create the JWT middleware; no business coupling.
    void RegisterRoutes(http::Router& routes, auth::JwtService& jwt_svc) const;

    const Config& GetConfig() const { return config_; }

private:
    // Append an operation log entry to the local buffer and invoke the callback.
    void AppendOpLog_(const OpLogEntry& entry) const;
    void TrimLogs_() const;

    Config config_;
    PlayerQueryCb player_query_cb_;
    BanCallback   ban_cb_;
    ResourceCb    resource_cb_;
    StatsCb       stats_cb_;
    LogAppendCb   log_append_cb_;
    LogQueryCb    log_query_cb_;

    // Local log buffer (kept in-memory as a convenience; app can also
    // store logs externally and provide its own query callback).
    mutable std::vector<OpLogEntry> op_logs_;
    mutable std::mutex              logs_mutex_;
};

} // namespace admin
} // namespace snc
