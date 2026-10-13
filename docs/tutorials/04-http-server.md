# 04 — HTTP Server

**目标**：了解如何用 `snc_http` 注册路由、挂载中间件、处理 GET/POST 请求。

## 概述

`snc_http` 是一个轻量级 HTTP 服务器，适用于游戏管理后台、运营 API、健康检查等场景。
它不依赖任何第三方网络库，在 Windows 使用 `select`，在 Linux 使用 `epoll`（若 `SNC_USE_EPOLL=ON`）。

> **注意**：此模块用于 HTTP 管理接口，不参与游戏业务逻辑。业务处理应在业务 C++ 代码或 Lua 脚本中完成。

## 配置 CMake

```cmake
# CMakeLists.txt
option(SNC_ENABLE_HTTP "Enable HTTP server module" ON)

# 主工程根 CMakeLists.txt 中启用：
# add_subdirectory(libs/snc_http)
```

头文件入口：

```cpp
#include <snc/http/HttpServer.h>
```

## 核心 API

| 类 / 函数 | 说明 |
|-----------|------|
| `HttpServer` | HTTP 服务器，持有路由表和中间件链 |
| `http::Router` | 路由注册表，支持路径参数（`:param`） |
| `http::HttpResponse` | 响应对象，提供静态工厂方法 |
| `HttpRequest::JsonBody()` | 解析 POST body 为 key-value map |
| `HttpRequest::headers` | 所有 HTTP 头（key 已转小写） |

## 快速示例

```cpp
#include <snc/http/HttpServer.h>
#include <snc/auth/AuthService.h>
#include <iostream>

int main() {
    snc::http::HttpServer server;
    auto& routes = server.Routes();

    // ── 1. 注册一个 GET 路由，返回固定 JSON ──────────────────
    routes.Get("/health", [](const snc::http::HttpRequest&)
        -> snc::http::HttpResponse {
        return snc::http::HttpResponse::Ok(R"({"status":"ok"})");
    });

    // ── 2. 带路径参数的路由 ──────────────────────────────────
    routes.Get("/api/player/:user_id", [](const snc::http::HttpRequest& req)
        -> snc::http::HttpResponse {
        auto params = routes.ResolveParams(req.path, snc::http::HttpMethod::HTTP_GET);
        std::string uid = params["user_id"];

        // TODO: 业务层实现玩家查询
        // auto player = MyGameLogic::QueryPlayer(uid);

        std::ostringstream j;
        j << R"({"user_id":")" << uid
          << R"(","nickname":"<game>","is_banned":false})";
        return snc::http::HttpResponse::Ok(j.str());
    });

    // ── 3. POST 请求 + JSON Body 解析 ────────────────────────
    routes.Post("/api/player/:user_id/resource", [](const snc::http::HttpRequest& req)
        -> snc::http::HttpResponse {
        auto params = routes.ResolveParams(req.path, snc::http::HttpMethod::HTTP_POST);
        std::string uid   = params["user_id"];
        auto body         = req.JsonBody();

        std::string resource = body.count("resource") ? body["resource"] : "";
        int64_t     delta    = 0;
        if (body.count("delta")) delta = std::stoll(body["delta"]);

        // TODO: 业务层实现资源变更
        // bool ok = MyGameLogic::ModifyResource(uid, resource, delta);

        if (resource.empty())
            return snc::http::HttpResponse::BadRequest();
        return snc::http::HttpResponse::Ok(R"({"ok":true})");
    });

    // ── 4. 启动服务 ──────────────────────────────────────────
    if (!server.Listen(8080)) {
        std::cerr << "Failed to listen on :8080\n";
        return 1;
    }
    std::cout << "HTTP server started on :8080\n";
    server.Run();  // blocking until Stop() is called
    return 0;
}
```

## 中间件（Middleware）

中间件是拦截请求的函数，可插入路由处理之前。常用场景：鉴权、日志、限流。

```cpp
#include <snc/auth/AuthService.h>

// 先用 JwtConfig 构建服务
snc::auth::JwtService jwt(snc::auth::JwtConfig{"my-secret-key-at-least-32-bytes", 3600, "", "HS256"});
auto auth_mw = snc::auth::MakeJwtMiddleware(jwt);

// 包装为 Middleware 类型（返回 false 中断请求链），挂载到 /api/* 前
server.Use([&auth_mw](snc::http::HttpRequest& req, snc::http::HttpResponse&) -> bool {
    return auth_mw(req.body, req.headers);
});

routes.Get("/api/admin/stats", [](const snc::http::HttpRequest& req)
    -> snc::http::HttpResponse {
    // auth_mw 已将 x-user-id 写入 req.headers
    std::string op_id = req.GetHeader("x-user-id");
    // TODO: 业务层实现统计查询
    return snc::http::HttpResponse::Ok(R"({"online":123})");
});
```

## 响应状态码速查

```cpp
snc::http::HttpResponse::Ok(body)               // 200
snc::http::HttpResponse::Created(body)          // 201
snc::http::HttpResponse::BadRequest()           // 400
snc::http::HttpResponse::Unauthorized()         // 401
snc::http::HttpResponse::Forbidden()            // 403
snc::http::HttpResponse::NotFound()             // 404
snc::http::HttpResponse::InternalError(msg)     // 500
```

## 与 snc_auth 的关系

`/api/admin/*` 端点通常需要对 JWT 鉴权：

```
客户端 ──[JWT Bearer Token]──> HttpServer
                                  │
                             auth::MakeJwtMiddleware
                                  │
                             JwtService::Verify()
                                  │
                         headers["x-user-id"] = userId
```

参见 [05-jwt-auth.md](./05-jwt-auth.md)
