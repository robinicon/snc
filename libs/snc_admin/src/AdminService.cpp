#include "snc/admin/AdminService.h"
#include "snc/auth/AuthService.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <ctime>

namespace snc {
namespace admin {

namespace {

// Build a JSON object from key-value pairs (string or numeric values).
std::string BuildJson(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::ostringstream os;
    os << "{";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) os << ",";
        os << "\"" << fields[i].first << "\":\"" << fields[i].second << "\"";
    }
    os << "}";
    return os.str();
}

std::string NumJson(const std::string& key, int64_t val) {
    std::ostringstream os;
    os << "{\"" << key << "\":" << val << "}";
    return os.str();
}

bool ExtractUserId(const http::HttpRequest& req, std::string& out) {
    auto it = req.headers.find("x-user-id");
    out = (it != req.headers.end()) ? it->second : "";
    return !out.empty();
}

} // namespace

// ── AdminService ────────────────────────────────────────────────

AdminService::AdminService(const Config& cfg) : config_(cfg) {}

void AdminService::TrimLogs_() const {
    std::lock_guard<std::mutex> lk(logs_mutex_);
    if (static_cast<int>(op_logs_.size()) > config_.max_log_entries) {
        op_logs_.erase(op_logs_.begin(),
                       op_logs_.begin() + static_cast<ptrdiff_t>(op_logs_.size() - config_.max_log_entries));
    }
}

void AdminService::AppendOpLog_(const OpLogEntry& entry) const {
    {
        std::lock_guard<std::mutex> lk(logs_mutex_);
        op_logs_.push_back(entry);
        TrimLogs_();
    }
    if (log_append_cb_) {
        log_append_cb_(entry);
    }
}

void AdminService::RegisterRoutes(http::Router& routes, auth::JwtService& jwt_svc) const {
    auto auth_mw = auth::MakeJwtMiddleware(jwt_svc);

    // GET /api/admin/player/:user_id
    routes.Get("/api/admin/player/:user_id",
        [this, &routes, auth_mw](const http::HttpRequest& req) -> http::HttpResponse {
        if (!auth_mw(const_cast<std::string&>(req.path),
                     const_cast<std::unordered_map<std::string, std::string>&>(req.headers)))
            return http::HttpResponse::Unauthorized();

        auto params = routes.ResolveParams(req.path, http::HttpMethod::HTTP_GET);
        std::string user_id = params["user_id"];
        if (user_id.empty()) return http::HttpResponse::BadRequest();

        if (!player_query_cb_)
            return http::HttpResponse::InternalError("player query handler not registered");

        auto player = player_query_cb_(user_id);
        std::ostringstream j;
        j << "{\"user_id\":\"" << player.user_id
          << "\",\"nickname\":\"" << player.nickname
          << "\",\"is_banned\":" << (player.is_banned ? "true" : "false")
          << ",\"last_login_ts\":" << player.last_login_ts << "}";
        return http::HttpResponse::Ok(j.str());
    });

    // POST /api/admin/player/:user_id/ban
    routes.Post("/api/admin/player/:user_id/ban",
        [this, &routes, auth_mw](const http::HttpRequest& req) -> http::HttpResponse {
        if (!auth_mw(const_cast<std::string&>(req.path),
                     const_cast<std::unordered_map<std::string, std::string>&>(req.headers)))
            return http::HttpResponse::Unauthorized();

        auto params = routes.ResolveParams(req.path, http::HttpMethod::HTTP_POST);
        std::string user_id = params["user_id"];
        std::string op_id;
        if (!ExtractUserId(req, op_id)) return http::HttpResponse::Unauthorized();
        if (user_id.empty()) return http::HttpResponse::BadRequest();

        if (!ban_cb_)
            return http::HttpResponse::InternalError("ban handler not registered");

        auto body = req.JsonBody();
        bool banned = (body.count("banned") && body["banned"] == "true");
        std::string reason = body.count("reason") ? body["reason"] : "";

        bool ok = ban_cb_(user_id, banned, op_id, reason);
        if (ok) {
            OpLogEntry entry{};
            entry.timestamp = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            entry.operator_id = op_id;
            entry.action = banned ? "ban" : "unban";
            entry.target_id = user_id;
            entry.detail = reason;
            AppendOpLog_(entry);
        }
        return ok ? http::HttpResponse::Ok("{\"ok\":true}")
                  : http::HttpResponse::InternalError("failed to update ban status");
    });

    // POST /api/admin/player/:user_id/resource
    routes.Post("/api/admin/player/:user_id/resource",
        [this, &routes, auth_mw](const http::HttpRequest& req) -> http::HttpResponse {
        if (!auth_mw(const_cast<std::string&>(req.path),
                     const_cast<std::unordered_map<std::string, std::string>&>(req.headers)))
            return http::HttpResponse::Unauthorized();

        auto params = routes.ResolveParams(req.path, http::HttpMethod::HTTP_POST);
        std::string user_id = params["user_id"];
        std::string op_id;
        if (!ExtractUserId(req, op_id)) return http::HttpResponse::Unauthorized();
        if (user_id.empty()) return http::HttpResponse::BadRequest();

        if (!resource_cb_)
            return http::HttpResponse::InternalError("resource handler not registered");

        auto body = req.JsonBody();
        std::string resource = body.count("resource") ? body["resource"] : "";
        int64_t delta = 0;
        if (body.count("delta")) delta = std::stoll(body["delta"]);
        if (resource.empty()) return http::HttpResponse::BadRequest();

        bool ok = resource_cb_(user_id, resource, delta, op_id);
        if (ok) {
            OpLogEntry entry{};
            entry.timestamp = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            entry.operator_id = op_id;
            entry.action = "modify_resource";
            entry.target_id = user_id;
            std::ostringstream detail;
            detail << resource << " " << (delta >= 0 ? "+" : "") << delta;
            entry.detail = detail.str();
            AppendOpLog_(entry);
        }
        return ok ? http::HttpResponse::Ok("{\"ok\":true}")
                  : http::HttpResponse::InternalError("failed to modify resource");
    });

    // GET /api/admin/stats
    routes.Get("/api/admin/stats",
        [this, auth_mw](const http::HttpRequest& req) -> http::HttpResponse {
        if (!auth_mw(const_cast<std::string&>(req.path),
                     const_cast<std::unordered_map<std::string, std::string>&>(req.headers)))
            return http::HttpResponse::Unauthorized();

        if (!stats_cb_)
            return http::HttpResponse::InternalError("stats handler not registered");

        auto stats = stats_cb_();
        std::ostringstream j;
        j << "{\"online\":" << stats.online_count
          << ",\"ws_connections\":" << stats.ws_connections
          << ",\"qps\":" << stats.http_requests_per_sec
          << ",\"avg_latency_ms\":" << stats.avg_latency_ms
          << ",\"error_rate_pct\":" << stats.error_rate_pct
          << "}";
        return http::HttpResponse::Ok(j.str());
    });

    // GET /api/admin/logs?limit=50
    routes.Get("/api/admin/logs",
        [this, auth_mw](const http::HttpRequest& req) -> http::HttpResponse {
        if (!auth_mw(const_cast<std::string&>(req.path),
                     const_cast<std::unordered_map<std::string, std::string>&>(req.headers)))
            return http::HttpResponse::Unauthorized();

        if (!log_query_cb_)
            return http::HttpResponse::InternalError("log query handler not registered");

        int limit = 50;
        auto pos = req.query_string.find("limit=");
        if (pos != std::string::npos) {
            limit = std::stoi(req.query_string.substr(pos + 6));
            if (limit > 200) limit = 200;
            if (limit < 1)  limit = 1;
        }

        auto logs = log_query_cb_(limit);
        std::ostringstream j;
        j << "[";
        for (size_t i = 0; i < logs.size(); ++i) {
            if (i > 0) j << ",";
            j << "{\"ts\":" << logs[i].timestamp
              << ",\"operator\":\"" << logs[i].operator_id << "\""
              << ",\"action\":\"" << logs[i].action << "\""
              << ",\"target\":\"" << logs[i].target_id << "\""
              << ",\"detail\":\"" << logs[i].detail << "\"}";
        }
        j << "]";
        return http::HttpResponse::Ok(j.str());
    });
}

} // namespace admin
} // namespace snc
