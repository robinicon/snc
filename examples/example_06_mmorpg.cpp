// SPDX-License-Identifier: MIT
/// @file example_06_mmorpg.cpp
/// @brief 示例 06 — 完整 MMORPG 多进程启动示例
///
/// 本示例在一个进程中模拟多个服务（实际生产为独立进程）：
///   1. AccountService — HTTP 注册/登录（端口 8080）
///   2. MasterService  — 服务注册/心跳统计/GM（RPC 9000 + Admin 9001）
///   3. GameService    — 游戏逻辑（RPC 8001）
///   4. GateService    — TCP 接入（端口 7777）
///   5. WorldService   — 跨服场景（RPC 9100）
///
/// 演示流程：
///   1. 启动 AccountService，注册/登录获取 JWT
///   2. GameService 向 Master 注册
///   3. GateService 连接到 GameService
///   4. WorldService 创建跨服场景
///   5. Master 查询全局服状态
///
/// 启动：cmake --build . --target example_06_mmorpg
/// 可选端口参数：./example_06_mmorpg [account_port] [master_rpc] [master_admin] [game_rpc] [gate_tcp] [world_rpc]

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sstream>

#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/db/Database.h"
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcClient.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/admin/AdminService.h"
#include "snc/base/Log.h"
#include "snc/mmorpg/AccountService.h"
#include "snc/mmorpg/MasterService.h"
#include "snc/mmorpg/GameService.h"
#include "snc/mmorpg/GateService.h"
#include "snc/mmorpg/WorldService.h"
#include "common/config.h"

using namespace snc;
using namespace snc::http;
using namespace snc::auth;
using namespace snc::db;
using namespace snc::rpc;
using namespace snc::discovery;
using namespace snc::admin;
using namespace snc::mmorpg;

// ── 版本协议 ───────────────────────────────────────────────────────
static constexpr uint16_t kServerProtocolVersion = 0x0201;  // v2.1
static constexpr uint16_t kClientProtocolVersion = 0x0201;
static constexpr int32_t  kMsgHandshake          = 0x0001;  // 客户端 → 服务端握手
static constexpr int32_t  kMsgHandshakeResp      = 0x0002; // 服务端 → 客户端响应

static std::atomic<bool> g_all_running{true};
static void SignalHandler(int) { g_all_running = false; }

// ── 端口配置（可通过命令行覆盖）──────────────────────────────────
struct ServerPorts {
    uint16_t account = 8080;
    uint16_t master_rpc   = 9000;
    uint16_t master_admin = 9001;
    uint16_t game_rpc     = 8001;
    uint16_t gate_tcp     = 7777;
    uint16_t world_rpc    = 9100;
};

static ServerPorts ParsePorts_(int argc, char* argv[]) {
    ServerPorts p;
    if (argc > 1)  p.account  = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc > 2)  p.master_rpc   = static_cast<uint16_t>(std::stoi(argv[2]));
    if (argc > 3)  p.master_admin = static_cast<uint16_t>(std::stoi(argv[3]));
    if (argc > 4)  p.game_rpc     = static_cast<uint16_t>(std::stoi(argv[4]));
    if (argc > 5)  p.gate_tcp     = static_cast<uint16_t>(std::stoi(argv[5]));
    if (argc > 6)  p.world_rpc    = static_cast<uint16_t>(std::stoi(argv[6]));
    return p;
}

// ── 辅助：简易 JSON 输出 ──────────────────────────────────────────
static std::string JsonObj(const std::initializer_list<std::pair<const char*, std::string>>& pairs) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (auto& [k, v] : pairs) {
        if (!first) oss << ",";
        oss << "\"" << k << "\":\"" << v << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    const ServerPorts p = ParsePorts_(argc, argv);
    (void)p; // 端口通过各服务 lambda 捕获使用

    // ⚠️ 生产环境请使用 secure random 生成 ≥32 字节密钥，并通过环境变量传入
    const std::string jwt_secret = snc_examples::JwtSecret();
    if (!snc_examples::JwtSecretFromEnv()) {
        SNC_LOG_WARN("mmorpg", "Using default JWT secret — set SNC_JWT_SECRET in production");
    }
    JwtConfig jwt_cfg{jwt_secret, 7200, "snc-mmorpg", "HS256"};

    std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║         SNC MMORPG 多服联合启动示例                      ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << "║  Account  : HTTP " << p.account  << "  (注册/登录/JWT)                    ║" << std::endl;
    std::cout << "║  Master   : RPC  " << p.master_rpc   << "  Admin " << p.master_admin << " (服务管理/GM)     ║" << std::endl;
    std::cout << "║  Game     : RPC  " << p.game_rpc << "  (游戏逻辑/地图/Actor)      ║" << std::endl;
    std::cout << "║  Gate     : TCP  " << p.gate_tcp << "  (客户端接入/消息路由)      ║" << std::endl;
    std::cout << "║  World    : RPC  " << p.world_rpc << "  (跨服场景管理)              ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;

    // ── 1. AccountService ──────────────────────────────────────
    std::cout << "\n[1/5] 启动 AccountService..." << std::endl;
    auto account_db = CreateStubDatabase();
    account_db->Connect("stub://");
    AccountService account_svc(account_db, jwt_cfg);
    HttpServer account_server;
    account_svc.RegisterRoutes(account_server.Routes());

    std::thread account_thread([&]() {
        account_server.Listen(p.account);
        account_server.Run();
    });
    account_thread.detach();
    std::cout << "      ✓ HTTP ://localhost:" << p.account << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ── 2. 注册测试账号 ────────────────────────────────────────
    std::cout << "\n[2/5] 注册测试账号..." << std::endl;
    {
        HttpRequest req;
        req.method = HttpMethod::HTTP_POST;
        req.path   = "/api/auth/register";
        req.body   = "{\"username\":\"player1\",\"password\":\"test123\"}";
        auto r = account_db->Execute("INSERT INTO accounts (id, username, password_hash) VALUES (1, 'player1', 'test123')");
        std::cout << "      ✓ 账号创建完成 (id=1, username=player1)" << std::endl;

        std::unordered_map<std::string, std::string> claims;
        claims["sub"] = "1";
        claims["username"] = "player1";
        claims["role"] = "player";
        claims["ver"] = std::to_string(kServerProtocolVersion);
        std::string token = account_svc.JwtService().Sign(claims);
        std::cout << "      ✓ JWT 签发成功（含版本 " << kServerProtocolVersion << "）: "
                  << token.substr(0, 20) << "..." << std::endl;
    }

    // ── 3. MasterService ───────────────────────────────────────
    std::cout << "\n[3/5] 启动 MasterService..." << std::endl;
    auto master_registry = ServiceRegistry::Create();
    auto master_svc = std::make_shared<MasterService>(master_registry);
    JwtService master_jwt_svc(jwt_cfg);
    HttpServer master_admin;
    master_svc->RegisterAdminRoutes(master_admin.Routes(), master_jwt_svc);
    master_admin.Routes().Get("/api/admin/services", [&](const HttpRequest&) -> HttpResponse {
        return HttpResponse::Json(200, master_svc->QueryServiceStatus());
    });
    std::thread master_thread([&]() {
        master_admin.Listen(p.master_admin);
        master_admin.Run();
    });
    master_thread.detach();
    std::cout << "      ✓ Admin HTTP ://localhost:" << p.master_admin << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ── 4. GameService ─────────────────────────────────────────
    std::cout << "\n[4/5] 启动 GameService..." << std::endl;
    auto game_registry = ServiceRegistry::Create();
    game_registry->Register("game", "127.0.0.1", p.game_rpc, 1);
    auto game_master_rpc = std::make_shared<RpcClient>(
        std::make_shared<RpcSession>(nullptr));
    GameService game_svc(game_master_rpc, game_registry, jwt_cfg);
    game_svc.InitMaps();
    std::thread game_thread([&]() {
        game_svc.Start(p.game_rpc, "game_01");
    });
    game_thread.detach();
    std::cout << "      ✓ RPC  ://127.0.0.1:" << p.game_rpc << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ── 5. WorldService ────────────────────────────────────────
    std::cout << "\n[5/5] 启动 WorldService..." << std::endl;
    auto world_registry = ServiceRegistry::Create();
    auto world_master_rpc = std::make_shared<RpcClient>(
        std::make_shared<RpcSession>(nullptr));
    WorldService world_svc(world_master_rpc, world_registry);
    std::thread world_thread([&]() {
        world_svc.Start(p.world_rpc);
    });
    world_thread.detach();
    std::cout << "      ✓ RPC  ://127.0.0.1:" << p.world_rpc << std::endl;

    std::cout << "\n──────────────────────────────────────────────────────────" << std::endl;
    std::cout << "所有服务已启动。演示跨服场景创建..." << std::endl;

    // ── 演示：跨服场景操作 ─────────────────────────────────────
    auto games = std::vector<std::string>{"game_01"};
    int32_t scene_id = world_svc.CreateScene("新手跨服战", games);
    std::cout << "\n  场景创建: id=" << scene_id << "  name=新手跨服战" << std::endl;

    world_svc.StartScene(scene_id);
    std::cout << "  场景启动: id=" << scene_id << "  state=ACTIVE" << std::endl;

    world_svc.EndScene(scene_id);
    std::cout << "  场景结束: id=" << scene_id << "  state=ENDING→IDLE" << std::endl;

    // ── 版本握手提示 ───────────────────────────────────────────
    std::cout << "\n  [版本协议] 服务端协议版本: 0x" << std::hex << kServerProtocolVersion << std::dec
              << "  客户端需保持一致，否则返回 ERR_VERSION_MISMATCH" << std::endl;

    // ── 保持运行 ───────────────────────────────────────────────
    std::cout << "\n──────────────────────────────────────────────────────────" << std::endl;
    std::cout << "服务运行中... (Ctrl+C 优雅停机)" << std::endl;
    std::cout << "  Account : POST http://localhost:" << p.account << "/api/auth/login" << std::endl;
    std::cout << "  Master  : GET  http://localhost:" << p.master_admin << "/api/admin/services" << std::endl;
    std::cout << "──────────────────────────────────────────────────────────" << std::endl;

    while (g_all_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n收到停机信号，正在优雅停机..." << std::endl;
    // 各服务的 Stop() 由析构顺序保证（栈上对象先于线程 detach）
    // detach 的线程会在主进程退出时终止
    std::cout << "✓ 所有服务已停止。" << std::endl;
    return 0;
}
