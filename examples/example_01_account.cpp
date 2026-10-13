/// @file example_01_account.cpp
/// @brief 示例 01 — 账号服（Account Service）
///
/// 功能：
///   - HTTP 注册：POST /api/auth/register  {username, password} → 写入 StubDatabase，返回 user_id
///   - HTTP 登录：POST /api/auth/login    {username, password} → 校验密码，签发 HS256 JWT
///   - HTTP 查询：GET  /api/auth/me                            → 需携带有效 JWT，返回用户信息
///   - Admin 接口：GET  /api/admin/services                      → Master 调用，列出已注册服
///
/// 启动：cmake --build . --target example_01_account && ./example_01_account
/// 测试：
///   curl -X POST http://localhost:8080/api/auth/register -d '{"username":"test","password":"123456"}'
///   curl -X POST http://localhost:8080/api/auth/login    -d '{"username":"test","password":"123456"}'
///   curl -H "Authorization: Bearer <token>" http://localhost:8080/api/auth/me

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/db/Database.h"
#include "snc/admin/AdminService.h"
#include "snc/base/Log.h"

using namespace snc;
using namespace snc::http;
using namespace snc::auth;
using namespace snc::db;
using namespace snc::admin;

// ── 简易内存账号存储（生产环境替换为真实 DB）──────────────────────
// 这里直接使用 StubDatabase，模拟 MySQL/SQLite 行为。

class AccountStore {
public:
    AccountStore() : db_(CreateStubDatabase()) {
        db_->Connect("stub://");
    }

    /// 注册：username 已存在返回 false；成功返回 user_id（字符串形式）。
    bool Register(const std::string& username, const std::string& password,
                  std::string& out_user_id) {
        std::lock_guard<std::mutex> lk(mtx_);
        // 检查是否存在
        auto r = db_->Query("SELECT * FROM accounts WHERE username = '" + username + "'");
        if (r.ok && !r.rows.empty()) return false;

        // 生成简单 user_id（实际应使用 UUID）
        int id = static_cast<int>(accounts_.size()) + 1;
        out_user_id = std::to_string(id);
        accounts_[out_user_id] = {username, HashPw(password)};

        // 写入数据库
        db_->Execute("INSERT INTO accounts (id, username, password_hash) VALUES ("
                     + std::to_string(id) + ", '" + username + "', '" + HashPw(password) + "')");
        return true;
    }

    /// 登录：密码错误返回空 token；成功返回 JWT payload 中的 user_id。
    std::pair<bool, std::string> Login(const std::string& username,
                                       const std::string& password) const {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [uid, acc] : accounts_) {
            if (acc.username == username && acc.pw_hash == HashPw(password)) {
                return {true, uid};
            }
        }
        return {false, ""};
    }

    std::string GetUsername(const std::string& user_id) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = accounts_.find(user_id);
        return (it != accounts_.end()) ? it->second.username : "";
    }

private:
    struct Account {
        std::string username;
        std::string pw_hash;
    };

    static std::string HashPw(const std::string& pw) {
        // 演示用途：简易哈希（生产用 bcrypt/argon2）
        std::string h = pw;
        for (auto& c : h) c = static_cast<char>(c ^ 0x5A);
        return h;
    }

    mutable std::mutex mtx_;
    std::shared_ptr<IDatabase> db_;
    std::unordered_map<std::string, Account> accounts_;
};

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

    // JWT 配置（Gate 服复用同一密钥进行本地验签）
    // ⚠️ 生产环境请使用 secure random 生成 ≥32 字节密钥，并通过环境变量传入
    JwtConfig jwt_cfg;
    jwt_cfg.secret = "snc-dev-jwt-secret-key-must-be-at-least-32-bytes!";  // 示例密钥，仅用于演示
    jwt_cfg.expireSecs = 7200;  // 2 小时
    JwtService jwt_svc(jwt_cfg);

    AccountStore store;

    HttpServer server;

    // ── POST /api/auth/register ──────────────────────────────────
    server.Routes().Post("/api/auth/register", [&](const HttpRequest& req) -> HttpResponse {
        auto body = req.JsonBody();
        auto get_val = [&](const std::string& k) -> std::string {
            auto it = body.find(k); return (it != body.end()) ? it->second : "";
        };
        std::string username = get_val("username");
        std::string password = get_val("password");

        if (username.empty() || password.empty()) {
            return HttpResponse::BadRequest();
        }
        std::string user_id;
        if (!store.Register(username, password, user_id)) {
            return HttpResponse::Json(409,
                "{\"error\":\"username_already_exists\",\"username\":\"" + username + "\"}");
        }
        SNC_LOG_INFO("account", "registered user: %s (id=%s)", username.c_str(), user_id.c_str());
        return HttpResponse::Json(201,
            "{\"user_id\":\"" + user_id + "\",\"username\":\"" + username + "\"}");
    });

    // ── POST /api/auth/login ─────────────────────────────────────
    server.Routes().Post("/api/auth/login", [&](const HttpRequest& req) -> HttpResponse {
        auto body = req.JsonBody();
        auto get_val = [&](const std::string& k) -> std::string {
            auto it = body.find(k); return (it != body.end()) ? it->second : "";
        };
        std::string username = get_val("username");
        std::string password = get_val("password");

        if (username.empty() || password.empty()) {
            return HttpResponse::BadRequest();
        }
        auto [ok, user_id] = store.Login(username, password);
        if (!ok) {
            return HttpResponse::Unauthorized();
        }
        // 签发 JWT（sub = user_id）
        std::unordered_map<std::string, std::string> claims;
        claims["sub"]      = user_id;
        claims["username"] = username;
        claims["role"]     = "player";
        std::string token = jwt_svc.Sign(claims);

        SNC_LOG_INFO("account", "login OK: %s (id=%s)", username.c_str(), user_id.c_str());
        return HttpResponse::Json(200,
            "{\"token\":\"" + token + "\",\"user_id\":\"" + user_id +
            "\",\"username\":\"" + username + "\"}");
    });

    // ── GET /api/auth/me（需要 JWT）──────────────────────────────
    auto jwt_middleware = MakeJwtMiddleware(jwt_svc);
    server.Use([&jwt_middleware](HttpRequest& req, HttpResponse&) -> bool {
        return jwt_middleware(req.body, req.headers);
    });

    server.Routes().Get("/api/auth/me", [&](const HttpRequest& req) -> HttpResponse {
        std::string user_id = req.GetHeader("x-user-id");
        std::string username = store.GetUsername(user_id);
        return HttpResponse::Json(200,
            "{\"user_id\":\"" + user_id + "\",\"username\":\"" + username + "\"}");
    });

    // ── 启动服务器 ───────────────────────────────────────────────
    SNC_LOG_INFO("account", "AccountService starting on port %d", port);
    if (!server.Listen(port)) {
        SNC_LOG_ERROR("account", "failed to listen on port %d", port);
        return 1;
    }
    std::cout << "[Account] Listening on http://0.0.0.0:" << port << std::endl;
    std::cout << "  POST /api/auth/register  {\"username\":\"...\",\"password\":\"...\"}" << std::endl;
    std::cout << "  POST /api/auth/login     {\"username\":\"...\",\"password\":\"...\"}" << std::endl;
    std::cout << "  GET  /api/auth/me        (需 Authorization: Bearer <token>)" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    server.Run();
    return 0;
}
