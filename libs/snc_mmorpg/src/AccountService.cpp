/// @file AccountService.cpp
/// @brief 账号服 — HTTP 注册/登录 + JWT 签发（stub 实现）。

#include "snc/mmorpg/AccountService.h"
#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/db/Database.h"
#include "snc/base/Log.h"

#include <algorithm>

namespace snc {
namespace mmorpg {

// ── SQL 字符串转义（防止注入）─────────────────────────────────
static std::string EscapeSql_(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\'')  { out += "''"; }
        else if (c == '\\') { out += "\\\\"; }
        else { out += c; }
    }
    return out;
}

void AccountService::InitTables_() {
    db_->Execute("CREATE TABLE IF NOT EXISTS accounts (id INT PRIMARY KEY, username TEXT, password_hash TEXT)");
}

bool AccountService::CheckUserExists_(const std::string& username) {
    auto result = db_->Query("SELECT id FROM accounts WHERE username='" + EscapeSql_(username) + "'");
    return !result.rows.empty();
}

std::string AccountService::HashPassword_(const std::string& pwd) const {
    return auth::HashSha256(pwd);
}

bool AccountService::Register(const RegisterReq& req, std::string& out_user_id) {
    if (CheckUserExists_(req.username)) return false;
    std::string hash = HashPassword_(req.password);
    db_->Execute("INSERT INTO accounts (id, username, password_hash) VALUES (NULL, '"
                 + EscapeSql_(req.username) + "', '" + hash + "')");
    auto result = db_->Query("SELECT id FROM accounts WHERE username='" + EscapeSql_(req.username) + "'");
    out_user_id = result.rows.empty() ? "0" : result.rows[0]["id"].ToString();
    SNC_LOG_INFO("account", "Registered user=%s id=%s", req.username.c_str(), out_user_id.c_str());
    return true;
}

LoginResp AccountService::Login(const LoginReq& req) {
    LoginResp resp;
    auto result = db_->Query("SELECT id, password_hash FROM accounts WHERE username='"
                             + EscapeSql_(req.username) + "'");
    if (result.rows.empty()) { resp.error = "user_not_found"; return resp; }
    std::string stored_hash = result.rows[0]["password_hash"].str_val;
    std::string calc_hash   = HashPassword_(req.password);
    if (stored_hash != calc_hash) { resp.error = "wrong_password"; return resp; }

    resp.success = true;
    resp.user_id = result.rows[0]["id"].ToString();
    std::unordered_map<std::string, std::string> claims;
    claims["sub"]     = resp.user_id;
    claims["username"] = req.username;
    claims["role"]    = "player";
    resp.token = jwt_.Sign(claims);
    SNC_LOG_INFO("account", "Login success user=%s", req.username.c_str());
    return resp;
}

void AccountService::RegisterRoutes(http::Router& routes) {
    routes.Post("/api/auth/register", [this](const http::HttpRequest& req) -> http::HttpResponse {
        auto body = req.JsonBody();
        auto gv = [&](const std::string& k) {
            auto it = body.find(k); return (it != body.end()) ? it->second : std::string();
        };
        RegisterReq r;
        r.username = gv("username");
        r.password = gv("password");
        if (r.username.empty() || r.password.empty())
            return http::HttpResponse::BadRequest();
        std::string uid;
        if (!Register(r, uid))
            return http::HttpResponse::Json(409, "{\"error\":\"user_exists\"}");
        return http::HttpResponse::Json(201, "{\"user_id\":\"" + uid + "\"}");
    });

    routes.Post("/api/auth/login", [this](const http::HttpRequest& req) -> http::HttpResponse {
        auto body = req.JsonBody();
        auto gv = [&](const std::string& k) {
            auto it = body.find(k); return (it != body.end()) ? it->second : std::string();
        };
        LoginReq lr;
        lr.username = gv("username");
        lr.password = gv("password");
        if (lr.username.empty() || lr.password.empty())
            return http::HttpResponse::BadRequest();
        LoginResp login_resp = Login(lr);
        if (!login_resp.success)
            return http::HttpResponse::Json(401, "{\"error\":\"" + login_resp.error + "\"}");
        return http::HttpResponse::Json(200,
            "{\"token\":\"" + login_resp.token
            + "\",\"user_id\":\"" + login_resp.user_id + "\"}");
    });

    routes.Get("/api/auth/me", [this](const http::HttpRequest& req) -> http::HttpResponse {
        std::string auth = req.GetHeader("Authorization");
        if (auth.size() < 7 || auth.substr(0, 7) != "Bearer ")
            return http::HttpResponse::Unauthorized();
        std::string token = auth.substr(7);
        auto jwt_token = jwt_.Verify(token);
        if (!jwt_token) return http::HttpResponse::Unauthorized();
        return http::HttpResponse::Json(200,
            "{\"user_id\":\"" + jwt_token->subject + "\"}");
    });
}

} // namespace mmorpg
} // namespace snc
