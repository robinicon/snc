# 06 — 管理后台 API（Admin Service）

**目标**：了解如何用 `snc_admin` 快速搭建一个带鉴权的 HTTP 管理后台，以及业务层如何通过回调注入自己的逻辑。

## 概述

`snc_admin` 提供一套通用的管理后台 HTTP API 脚手架，包含：

- 玩家信息查询
- 封禁 / 解封
- 资源修改（金币、钻石等）
- 服务器状态统计
- 操作日志查询

所有数据操作通过 **回调函数（std::function）** 注入，库本身不访问任何数据库或内存存储。业务逻辑在应用层实现。

## 配置 CMake

```cmake
option(SNC_ENABLE_ADMIN "Enable Admin API module" ON)
add_subdirectory(libs/snc_admin)
target_link_libraries(my_app PRIVATE snc_admin snc_auth snc_http snc_base)
```

头文件入口：

```cpp
#include <snc/admin/AdminService.h>
```

## 核心 API

```cpp
class AdminService {
public:
    struct Config {
        std::string admin_role = "admin";   // JWT claims 中需要的角色字段值
        int max_log_entries  = 10000;       // 内存日志最大条数
    };

    // 各类业务回调类型
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

    // 注册回调（必须在 RegisterRoutes 之前调用）
    void SetPlayerQueryCb(PlayerQueryCb cb);
    void SetBanCb(BanCallback cb);
    void SetResourceCb(ResourceCb cb);
    void SetStatsCb(StatsCb cb);
    void SetLogAppendCb(LogAppendCb cb);
    void SetLogQueryCb(LogQueryCb cb);

    // 注册路由（绑定到 http::Router，使用 JwtService 做鉴权）
    void RegisterRoutes(http::Router& routes, auth::JwtService& jwt_svc) const;
};
```

### 数据结构

```cpp
struct PlayerRecord {
    std::string user_id;
    std::string nickname;
    std::unordered_map<std::string, int64_t> resources;   // 任意资源名称 → 数量
    std::unordered_map<std::string, std::string> extra;   // 扩展字段
    bool is_banned = false;
    int64_t last_login_ts = 0;
    std::string register_time;
};

struct ServerStats {
    int  online_count            = 0;
    int  ws_connections          = 0;
    int  http_requests_per_sec   = 0;
    int  avg_latency_ms          = 0;
    int  error_rate_pct          = 0;
};

struct OpLogEntry {
    int64_t timestamp;
    std::string operator_id;
    std::string action;
    std::string target_id;
    std::string detail;
};
```

## 完整示例

```cpp
#include <snc/http/HttpServer.h>
#include <snc/auth/AuthService.h>
#include <snc/admin/AdminService.h>
#include <iostream>

// ── 业务层：模拟玩家存储（实际应用请替换为数据库调用）────────────
std::unordered_map<std::string, snc::admin::PlayerRecord> g_players;

snc::admin::PlayerRecord QueryPlayer(const std::string& uid) {
    auto it = g_players.find(uid);
    if (it != g_players.end()) return it->second;
    return snc::admin::PlayerRecord{uid, "<unknown>", {}, {}, false, 0, ""};
}

// ── 主函数 ─────────────────────────────────────────────────────
int main() {
    // 1. JWT 鉴权服务
    snc::auth::JwtConfig jwt_cfg{"admin-secret-key-must-be-32-bytes!!", 7200, "", "HS256"};
    snc::auth::JwtService jwt(jwt_cfg);

    // 2. AdminService 实例
    snc::admin::AdminService admin({
        .admin_role     = "admin",
        .max_log_entries = 10000,
    });

    // 3. 注入业务回调
    admin.SetPlayerQueryCb([](const std::string& uid) {
        return QueryPlayer(uid);   // 业务层实现
    });

    admin.SetBanCb([](const std::string& uid, bool banned,
                      const std::string& op_id, const std::string& reason) -> bool {
        std::cout << "[" << op_id << "] " << (banned ? "封禁" : "解封")
                  << " 玩家 " << uid << ": " << reason << "\n";
        // TODO: 调用游戏逻辑层执行封禁
        return true;
    });

    admin.SetResourceCb([](const std::string& uid, const std::string& resource,
                           int64_t delta, const std::string& op_id) -> bool {
        std::cout << "[" << op_id << "] " << resource << " "
                  << (delta > 0 ? "+" : "") << delta << "  玩家=" << uid << "\n";
        // TODO: 调用游戏逻辑层修改资源
        return true;
    });

    admin.SetStatsCb([]() {
        snc::admin::ServerStats s{};
        s.online_count     = 123;
        s.ws_connections   = 123;
        s.http_requests_per_sec = 45;
        s.avg_latency_ms   = 12;
        s.error_rate_pct   = 0;
        return s;
    });

    admin.SetLogAppendCb([](const snc::admin::OpLogEntry& log) {
        // 可选：同时写入外部日志系统（文件、ELK、云服务等）
        (void)log;
    });

    // 4. 挂载路由
    snc::http::HttpServer server;
    auto& routes = server.Routes();
    admin.RegisterRoutes(routes, jwt);

    // 5. 启动
    if (!server.Listen(9090)) {
        std::cerr << "Failed to listen on :9090\n";
        return 1;
    }
    std::cout << "Admin server on :9090\n";
    server.Run();
    return 0;
}
```

## 注册的 API 端点

| 方法 | 路径 | 说明 | 需要 JWT |
|------|------|------|---------|
| GET | `/api/admin/player/:user_id` | 查询玩家信息 | ✅ |
| POST | `/api/admin/player/:user_id/ban` | 封禁/解封玩家 | ✅ |
| POST | `/api/admin/player/:user_id/resource` | 修改资源 | ✅ |
| GET | `/api/admin/stats` | 服务器统计 | ✅ |
| GET | `/api/admin/logs?limit=50` | 操作日志 | ✅ |

## 操作流程说明

```
管理员登录 ──POST /login──> JwtService.Sign() ──> 拿到 JWT
                                    │
                                    ▼
  携带 Authorization: Bearer <token>
                                    │
                                    ▼
  ┌──────────────────────────────────────────────┐
  │  AdminService.RegisterRoutes()               │
  │   ├─ MakeJwtMiddleware(jwt)  →  验证 Token   │
  │   ├─ 调用业务 callback（SetXxxCb）           │
  │   └─ 返回 JSON 响应                          │
  └──────────────────────────────────────────────┘
```

## 注意事项

- **必须先注册所有回调再调用 `RegisterRoutes()`**，否则对应接口会返回 500。
- `resources` 和 `extra` 是泛型字段，业务层自行约定 key 名称（如 `"gold"`、`"diamond"`）。
- 操作日志默认存在内存中，超过 `max_log_entries` 会自动截断（仅保留最新 N 条）。
- 如需持久化日志，请调用 `SetLogAppendCb` 将日志同时写入外部存储，并通过 `SetLogQueryCb` 提供查询接口。

参见 [05-jwt-auth.md](./05-jwt-auth.md)
