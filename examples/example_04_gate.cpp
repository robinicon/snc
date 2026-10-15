// example_04_gate.cpp — Gate 服完整示例
//
// 编译：cmake --preset=normal -DBUILD_EXAMPLES=ON && cmake --build --config Release
// 运行：./example_04_gate [host] [port] [master_host] [master_port] [jwt_secret]
//
// Gate 服职责：
//   1. TCP 监听客户端连接，启动心跳
//   2. 本地 HS256 验签，建立 fd → userId 映射
//   3. 按 Game 服 ServiceRegistry 轮询转发 0x2000 范围消息
//   4. OnDisconnect 时清理映射并通知 Game 服

#include "snc/mmorpg/GateService.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"

#include <csignal>
#include <iostream>
#include <thread>
#include <atomic>

using namespace snc;
using namespace snc::discovery;
using namespace snc::auth;
using namespace snc::mmorpg;

static std::atomic<bool> g_running{true};
static void SignalHandler(int) { g_running = false; }

// ── 端口配置（可通过命令行覆盖）──────────────────────────────────
struct GatePorts {
    uint16_t gate_tcp   = 7777;
    uint16_t master_rpc = 9000;
};

static GatePorts ParsePorts_(int argc, char* argv[]) {
    GatePorts p;
    if (argc > 1)  p.gate_tcp   = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc > 2)  p.master_rpc = static_cast<uint16_t>(std::stoi(argv[2]));
    return p;
}

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    base::InitLogger(base::LogLevel::INFO);
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    const GatePorts p     = ParsePorts_(argc, argv);
    const char*   master_host = (argc > 3) ? argv[3] : "127.0.0.1";
    uint16_t      master_port = (argc > 4) ? static_cast<uint16_t>(std::stoi(argv[4])) : 9000;
    std::string   jwt_secret  = (argc > 5) ? argv[5]
        : []() -> std::string {
              const char* env = std::getenv("SNC_JWT_SECRET");
              return (env && env[0]) ? env : "snc-dev-jwt-secret-key-must-be-at-least-32-bytes!";
          }();

    if (!std::getenv("SNC_JWT_SECRET")) {
        SNC_LOG_WARN("Gate", "Using default JWT secret — set SNC_JWT_SECRET in production");
    }

    JwtConfig jwtCfg{jwt_secret, 7200, "snc-mmorpg", "HS256"};

    std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║               Gate 服启动                                  ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << "║  Gate TCP : 0.0.0.0:" << p.gate_tcp   << "  (客户端接入)                 ║" << std::endl;
    std::cout << "║  Master RPC:" << p.master_rpc << "  (" << master_host << ":" << master_port << ")          ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;

    // ── 1. 服务发现注册 ────────────────────────────────────────
    auto registry = ServiceRegistry::Create();
    registry->Register("gate", "0.0.0.0", p.gate_tcp, 1);
    // 注册 Master 服地址（Gate 通过 Master 查找 Game 服）
    registry->Register("master", master_host, master_port, 10);

    // ── 2. 构造 GateService ───────────────────────────────────
    GateService gateSvc(registry, jwtCfg);

    // ── 3. 启动 Gate 服（阻塞）───────────────────────────────
    if (!gateSvc.Start("0.0.0.0", p.gate_tcp)) {
        SNC_LOG_ERROR("Gate", "Failed to start Gate service on port %d", p.gate_tcp);
        return 1;
    }
    std::cout << "\n✓ Gate 服已启动，监听端口 " << p.gate_tcp << std::endl;
    std::cout << "  通过 Master RPC " << master_host << ":" << master_port << " 路由到 Game 服" << std::endl;
    std::cout << "──────────────────────────────────────────────────────────" << std::endl;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n收到停机信号，正在关闭 Gate 服..." << std::endl;
    gateSvc.Stop();
    std::cout << "✓ Gate 服已停止。在线玩家: " << gateSvc.OnlinePlayerCount() << std::endl;
    return 0;
}
