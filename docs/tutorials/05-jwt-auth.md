# 05 — JWT 认证

**目标**：了解如何用 `snc_auth` 签发、验证、刷新 JWT，以及如何将其集成到 HTTP 中间件链中。

## 概述

`snc_auth` 提供通用的 JWT（JSON Web Token）工具，不绑定任何登录渠道（微信、手机号等）。
业务层的登录流程（OAuth、账号密码、游戏内登录等）由调用方自行实现，本库只负责：

1. 用 HS256 签名/验签 JWT
2. 提供 `MakeJwtMiddleware`，自动从 `Authorization: Bearer <token>` 中提取并验证 Token
3. 将解析出的 `user_id` 写入 `request.headers["x-user-id"]`

## 配置 CMake

```cmake
option(SNC_ENABLE_AUTH "Enable JWT auth module" ON)
add_subdirectory(libs/snc_auth)
target_link_libraries(my_app PRIVATE snc_auth snc_base)
```

头文件入口：

```cpp
#include <snc/auth/AuthService.h>
```

## JwtService

```cpp
struct JwtConfig {
    std::string secret;       // HMAC-SHA256 签名密钥，建议 ≥ 32 字节
    int         expireSecs   = 3600;
};

class JwtService {
public:
    explicit JwtService(const JwtConfig& config);

    // 签发 token，claims 是自定义字段（会被编码进 payload）
    std::string Sign(const std::unordered_map<std::string, std::string>& claims) const;

    // 验证 token，返回 nullptr 表示无效或已过期
    std::shared_ptr<JwtToken> Verify(const std::string& token) const;

    // 用旧 token 签出新 token（续期），失败返回空字符串
    std::string Refresh(const std::string& old_token) const;

    const JwtConfig& Config() const { return config_; }
};
```

### JwtToken 结构

```cpp
struct JwtToken {
    std::string user_id;
    int64_t     issued_at;   // Unix 时间戳（秒）
    int64_t     expires_at;  // Unix 时间戳（秒）
};
```

## 中间件：MakeJwtMiddleware

`MakeJwtMiddleware` 返回一个 `AuthPreHandler`，可直接挂载到 `HttpServer::Use()` 或路由 Lambda 中。

```cpp
using AuthPreHandler = std::function<bool(std::string&, std::unordered_map<std::string, std::string>&)>;
AuthPreHandler MakeJwtMiddleware(JwtService& svc);
```

**行为**：
1. 从 `headers["authorization"]` 提取 `Bearer <token>`
2. 调用 `JwtService::Verify(token)`
3. 验签通过则写入 `headers["x-user-id"] = token->user_id`，返回 `true`
4. 验签失败返回 `false`，响应状态码由调用方决定（通常为 401）

## 完整示例

```cpp
#include <snc/http/HttpServer.h>
#include <snc/auth/AuthService.h>
#include <iostream>

int main() {
    // 1. 创建 JwtService（密钥必须 ≥ 32 字节）
    snc::auth::JwtConfig jwt_cfg{"this-is-a-32-byte-secret-key!!!!", 3600, "", "HS256"};
    snc::auth::JwtService jwt(jwt_cfg);

    // 2. 创建 HTTP 服务器
    snc::http::HttpServer server;
    auto& routes = server.Routes();
    auto  auth_mw = snc::auth::MakeJwtMiddleware(jwt);

    // 3. 登录接口（业务层自己实现 token 签发，这里只演示用法）
    routes.Post("/login", [&jwt](const snc::http::HttpRequest& req)
        -> snc::http::HttpResponse {
        auto body = req.JsonBody();
        // TODO: 业务层验证账号密码 / 微信 code
        std::string user_id = body.count("user_id") ? body["user_id"] : "";
        if (user_id.empty())
            return snc::http::HttpResponse::Unauthorized();

        // 签发 token，claims 放入业务需要的字段
        std::unordered_map<std::string, std::string> claims{{"user_id", user_id}};
        std::string token = jwt.Sign(claims);
        return snc::http::HttpResponse::Ok("{\"token\":\"" + token + "\"}");
    });

    // 4. 受保护接口（所有 /api/* 都需要携带 Bearer Token）
    routes.Get("/api/player/:user_id", [&jwt, &routes, auth_mw](const snc::http::HttpRequest& req)
        -> snc::http::HttpResponse {
        // 先跑 JWT 中间件
        if (!auth_mw(const_cast<std::string&>(req.path),
                     const_cast<std::unordered_map<std::string, std::string>&>(req.headers)))
            return snc::http::HttpResponse::Unauthorized();

        auto params = routes.ResolveParams(req.path, snc::http::HttpMethod::HTTP_GET);
        std::string uid = params["user_id"];

        // TODO: 业务层查询玩家数据
        return snc::http::HttpResponse::Ok("{\"user_id\":\"" + uid + "\"}");
    });

    // 5. 刷新 token
    routes.Post("/refresh", [&jwt](const snc::http::HttpRequest& req)
        -> snc::http::HttpResponse {
        auto auth = req.headers.count("authorization") ? req.headers.at("authorization") : "";
        std::string token = (auth.size() > 7) ? auth.substr(7) : "";
        std::string new_token = jwt.Refresh(token);
        if (new_token.empty())
            return snc::http::HttpResponse::Unauthorized();
        return snc::http::HttpResponse::Ok("{\"token\":\"" + new_token + "\"}");
    });

    server.Listen(8080);
    std::cout << "Auth server on :8080\n";
    server.Run();
    return 0;
}
```

## 注意事项

1. **密钥长度必须 ≥ 32 字节**：HS256 使用 HMAC-SHA256，密钥过短（如 `"secret"`）会导致签名强度严重不足，且部分 JWT 库对过短密钥会直接报错；建议生产环境使用随机生成的 32+ 字节密钥（如 `openssl rand -base64 32`）
2. **Token 传输必须使用 HTTPS**：否则 Token 可被中间人截获并 replay
3. **有效期建议 ≤ 24 小时**：短生命周期 Token + Refresh 机制更安全；生产环境根据业务需要调整
4. **JWT payload 是 Base64URL 编码（可还原）**：不要存放密码、密钥等敏感信息，只放业务标识（如 `user_id`）
5. **`JwtConfig` 字段说明**：
   - `secret` — HMAC-SHA256 签名密钥
   - `expireSecs` — Token 有效期（秒），默认 3600
   - `algorithm` — 签名算法，默认 `"HS256"`，其他值未实现

## 与 snc_admin 的关系

`snc_admin` 的 `RegisterRoutes` 接受 `JwtService&` 作为参数，内部复用 `MakeJwtMiddleware` 做鉴权，无需重复实现。

参见 [06-admin-api.md](./06-admin-api.md)
