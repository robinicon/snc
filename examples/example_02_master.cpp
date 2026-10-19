// SPDX-License-Identifier: MIT
/// @file example_02_master.cpp
/// @brief 示例 02 — Master 服（服务状态管理 + GM 命令分发）
///
/// 功能：
///   - RPC 0x3000：接收 Game/Gate/World 服心跳注册
///   - RPC 0x3001：处理 GM 命令（kick_player / list_players / shutdown）
///   - RPC 0x3002：响应服列表查询
///   - HTTP Admin：GET /api/admin/services（列出所有注册服）
///
/// Master 不主动判死（H1 修订），由外部监控系统检测超时。
///
/// 启动：cmake --build . --target example_02_master && ./example_02_master [rpc_port] [admin_port]

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "snc/rpc/RpcServer.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/core/Msg.h"
#include "snc/core/IConnection.h"
#include "snc/admin/AdminService.h"
#include "snc/auth/AuthService.h"
#include "snc/http/HttpServer.h"
#include "snc/base/Log.h"
#include "snc/mmorpg/MasterService.h"
#include "common/config.h"

using namespace snc;
using namespace snc::rpc;
using namespace snc::discovery;
using namespace snc::core;
using namespace snc::mmorpg;
using namespace snc::auth;
using namespace snc::http;

// ── 解析 HeartbeatPayload ─────────────────────────────────────────
static bool ParseHeartbeat(const char* data, size_t len, HeartbeatPayload& out) {
    // 简化 JSON 解析（演示用途）
    std::string s(data, len);
    auto get_val = [&](const std::string& key) -> std::string {
        std::string kv = "\"" + key + "\":\"";
        auto pos = s.find(kv);
        if (pos == std::string::npos) return "";
        pos += kv.size();
        auto end = s.find('"', pos);
        return (end != std::string::npos) ? s.substr(pos, end - pos) : "";
    };
    auto get_int = [&](const std::string& key) -> int {
        std::string kv = "\"" + key + "\":";
        auto pos = s.find(kv);
        if (pos == std::string::npos) return 0;
        pos += kv.size();
        return std::stoi(s.substr(pos));
    };
    out.serviceName = get_val("service_name");
    out.type        = static_cast<ServiceType>(get_int("type"));
    out.playerCount = get_int("player_count");
    out.mapCount    = get_int("map_count");
    out.uptimeSecs  = get_int("uptime_secs");
    return !out.serviceName.empty();
}

// ── 解析 GmCommand ────────────────────────────────────────────────
static bool ParseGmCommand(const char* data, size_t len, GmCommand& out) {
    std::string s(data, len);
    auto get_val = [&](const std::string& key) -> std::string {
        std::string kv = "\"" + key + "\":\"";
        auto pos = s.find(kv);
        if (pos == std::string::npos) return "";
        pos += kv.size();
        auto end = s.find('"', pos);
        return (end != std::string::npos) ? s.substr(pos, end - pos) : "";
    };
    out.command  = get_val("command");
    out.target   = get_val("target");
    out.args     = get_val("args");
    out.operator_id = get_val("operator_id");
    return !out.command.empty();
}

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    uint16_t rpc_port   = 9000;
    uint16_t admin_port = 9001;
    if (argc > 1) rpc_port   = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc > 2) admin_port = static_cast<uint16_t>(std::stoi(argv[2]));

    auto registry = ServiceRegistry::Create();
    auto master   = std::make_shared<MasterService>(registry);

    // ── RPC 服务器 ────────────────────────────────────────────────
    RpcServer rpc_server;
    master->RegisterRpcHandlers(rpc_server);

    // 手动启动 RpcServer（此处为演示，实际通过 TcpServer 监听）
    // 使用 IEventLoop + TcpServer 的完整链路在此简化为：
    // 直接注册 handler 并通过示例代码展示用法。
    SNC_LOG_INFO("master", "MasterService RPC handlers registered (port %d)", rpc_port);

    // ── HTTP Admin 服务器 ─────────────────────────────────────────
    const std::string jwt_secret = snc_examples::JwtSecret();
    if (!snc_examples::JwtSecretFromEnv()) {
        SNC_LOG_WARN("master", "Using default JWT secret — set SNC_JWT_SECRET in production");
    }
    JwtConfig jwt_cfg{jwt_secret, 7200, "snc-mmorpg", "HS256"};
    JwtService jwt_svc(jwt_cfg);

    HttpServer admin_server;
    master->RegisterAdminRoutes(admin_server.Routes(), jwt_svc);

    // Admin 路由：列出所有服
    admin_server.Routes().Get("/api/admin/services", [&](const HttpRequest&) -> HttpResponse {
        std::string status = master->QueryServiceStatus();
        return HttpResponse::Json(200, status);
    });

    // Admin 路由：执行 GM 命令（POST）
    admin_server.Routes().Post("/api/admin/gm", [&](const HttpRequest& req) -> HttpResponse {
        auto body = req.JsonBody();
        auto get_val = [&](const std::string& k) {
            return body.count(k) ? body.at(k) : std::string();
        };
        GmCommand cmd;
        cmd.command   = get_val("command");
        cmd.target    = get_val("target");
        cmd.args      = get_val("args");
        cmd.operator_id = get_val("operator_id");
        if (cmd.command.empty()) return HttpResponse::BadRequest();
        std::string result = master->ExecuteGm(cmd);
        return HttpResponse::Json(200, result);
    });

    if (!admin_server.Listen(admin_port)) {
        SNC_LOG_ERROR("master", "failed to listen on admin port %d", admin_port);
        return 1;
    }
    SNC_LOG_INFO("master", "MasterService starting: RPC=%d  Admin=%d", rpc_port, admin_port);
    std::cout << "[Master] RPC port: " << rpc_port
              << "  Admin port: " << admin_port << std::endl;
    std::cout << "  GET  /api/admin/services   → 列出所有注册服" << std::endl;
    std::cout << "  POST /api/admin/gm         {\"command\":\"list_players\",\"operator_id\":\"gm1\"}" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    // 注意：真实部署中 RpcServer 由 TcpServer 驱动，此处仅启动 Admin HTTP。
    // 心跳注册需在 Game/Gate/World 服中通过 RpcClient 主动发起。
    admin_server.Run();
    return 0;
}
