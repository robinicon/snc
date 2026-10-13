# 17 — AccountService（账号服）

**目标**：了解 `AccountService` 如何配合 HTTP 服务器实现注册、登录、JWT 签发，以及 Gate 服如何进行本地验签。

## 概述

`AccountService` 是 MMORPG 架构中的账号服务组件，封装了三个核心流程：

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/auth/register` | POST | 注册新账号，写入数据库，返回 `user_id` |
| `/api/auth/login` | POST | 校验密码，签发 JWT token |
| `/api/auth/me` | GET | 返回当前登录用户信息（需 JWT） |

头文件入口：

```cpp
#include <snc/mmorpg/AccountService.h>
```

## 基本用法

```cpp
#include <snc/mmorpg/AccountService.h>
#include <snc/http/HttpServer.h>
#include <snc/db/Database.h>
#include <snc/auth/AuthService.h>

// 使用 StubDatabase 开发测试（生产替换为真实 MySQL 实现）
auto db   = snc::db::CreateStubDatabase();
db->Connect("stub://");

// JwtConfig：Gate 服与账号服共用同一密钥，支持本地验签
snc::auth::JwtConfig jwt_cfg{"this-is-a-32-byte-secret-key!!!!", 3600, "", "HS256"};

auto account_svc = std::make_shared<snc::mmorpg::AccountService>(db, jwt_cfg);

// 将路由注册到 HttpServer
snc::http::HttpServer server;
account_svc->RegisterRoutes(server.Routes());
server.Listen(8080);
server.Run();
```

## 注册接口

```http
POST /api/auth/register
Content-Type: application/json

{"username": "Alice", "password": "secret123"}
```

成功响应：
```json
{"success": true, "user_id": "1"}
```

已存在用户名时返回 `409 Conflict`：
```json
{"success": false, "error": "username already exists"}
```

## 登录接口

```http
POST /api/auth/login
Content-Type: application/json

{"username": "Alice", "password": "secret123"}
```

成功响应：
```json
{
  "success": true,
  "token": "eyJhbGciOiJIUzI1NiIs...",
  "user_id": "1"
}
```

密码错误时返回 `401 Unauthorized`，不返回任何 token。

## 当前用户接口

```http
GET /api/auth/me
Authorization: Bearer <token>
```

成功响应：
```json
{
  "success": true,
  "user_id": "1",
  "username": "Alice"
}
```

## Gate 服本地验签

Gate 服持有与账号服相同的 `JwtConfig`，可以直接在本地验签，无需每次回调账号服：

```cpp
#include <snc/mmorpg/AccountService.h>

// Gate 服持有同一 JwtConfig 创建的 JwtService
snc::auth::JwtConfig jwt_cfg{"this-is-a-32-byte-secret-key!!!!", 3600, "", "HS256"};
snc::auth::JwtService jwt(jwt_cfg);

// 解析 token 获取用户 ID
auto token = jwt.Verify(bearer_token);
if (!token) {
    // token 无效或已过期
    return;
}
std::string user_id = token->subject;  // JWT 的 sub 声明即为用户 ID
```

## 自定义数据库后端

`AccountService` 构造函数支持传入自定义 `DbFactory`，用于注入真实 MySQL 等后端：

```cpp
auto db_factory = []() -> std::shared_ptr<snc::db::IDatabase> {
    auto mysql_db = std::make_shared<MySqlDatabase>();
    mysql_db->Connect("mysql://game:pass@10.0.0.5:3306/game_db");
    return mysql_db;
};

snc::mmorpg::AccountService svc(
    nullptr,            // db=nullptr，将由 factory 创建
    jwt_cfg,
    db_factory          // 自定义工厂
);
```

## 注意事项

- **密码以明文存储**：`AccountService` 内部对密码做了简单哈希（`HashPassword_()`），但生产环境应使用 bcrypt/argon2 等强哈希算法
- **JwtConfig.secret 必须 ≥ 32 字节**：否则 HS256 签名强度不足，建议生产使用随机生成的长密钥
- **Gate 服验签与账号服签发使用同一密钥**：两端 `JwtConfig.secret` 必须完全一致
- **`RegisterRoutes()` 在 `HttpServer` 上注册三个端点**，不要重复注册同名路由

参见 [05 - JWT 认证](05-jwt-auth.md)、[15 - 数据库适配器](15-mysql-database.md)
