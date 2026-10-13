#pragma once

/// @file AccountService.h
/// @brief 账号服服务 — HTTP 注册/登录 + JWT 签发 + StubDatabase 存储。
///
/// 流程：
///   1. POST /api/auth/register  {username, password} → 写入 DB，返回 user_id
///   2. POST /api/auth/login     {username, password} → 校验密码，签发 JWT
///   3. GET  /api/auth/me        (需 JWT)          → 返回当前用户信息
///
/// Gate 服持有同一 JwtConfig，本地 HS256 验签，无需回调账号服。
///
/// 使用方式：
///   auto svc = std::make_shared<AccountService>(db, jwtCfg);
///   HttpServer srv;
///   srv.Listen(8080);
///   svc->RegisterRoutes(srv.Routes());
///   srv.Run();

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/db/Database.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {

// ── 账号服请求/响应结构 ──────────────────────────────────────────
struct RegisterReq {
    std::string username;
    std::string password;
};

struct LoginReq {
    std::string username;
    std::string password;
};

struct LoginResp {
    bool   success = false;
    std::string token;    ///< JWT token
    std::string user_id;  ///< 用户 ID（字符串形式）
    std::string error;
};

// ── AccountService ───────────────────────────────────────────────
class AccountService {
public:
    /// factory 参数用于创建数据库实例（默认使用 StubDatabase）。
    using DbFactory = std::function<std::shared_ptr<db::IDatabase>()>;

    AccountService(std::shared_ptr<db::IDatabase> db,
                   const auth::JwtConfig& jwtCfg,
                   DbFactory dbFactory = nullptr)
        : db_(db ? db : (dbFactory ? dbFactory() : db::CreateStubDatabase()))
        , jwt_(jwtCfg) {
        if (!db_->IsConnected()) {
            db_->Connect("stub://");
        }
        InitTables_();
    }

    /// 注册用户；已存在返回 false。
    bool Register(const RegisterReq& req, std::string& out_user_id);

    /// 登录；密码错误返回失败，成功返回 JWT token。
    LoginResp Login(const LoginReq& req);

    /// 从路由注册所有 API 端点。
    void RegisterRoutes(http::Router& routes);

    /// 获取 JWT 服务引用（Gate 复用同一实例进行本地验签）。
    const auth::JwtService& JwtService() const { return jwt_; }

private:
    void InitTables_();
    bool CheckUserExists_(const std::string& username);
    std::string HashPassword_(const std::string& pwd) const;

    std::shared_ptr<db::IDatabase> db_;
    auth::JwtService               jwt_;
};

} // namespace mmorpg
} // namespace snc
