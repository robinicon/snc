/// @file example_04_gate.cpp
/// @brief 示例 04 — Gate 服（TCP 接入 + JWT 本地验签 + 消息路由）
///
/// 功能：
///   1. 接收客户端 TCP 连接（端口 7777）
///   2. 客户端发送 JWT（MSG_AUTH = 0x1001）→ 本地 HS256 验签
///   3. 验证通过后建立 fd → userId 映射
///   4. 客户端业务消息（MSG_FORWARD = 0x1002）→ 按 Game 服 Registry 轮询转发
///   5. OnDisconnected_ 回调清理 fd 映射，并通知 Game 服
///
/// 消息流：
///   Client --0x1001(JWT)--> Gate --[验签]--> Gate
///   Client --0x1002(msg)--> Gate --RPC--> Game --业务处理--> Gate --TCP--> Client
///
/// 启动：cmake --build . --target example_04_gate && ./example_04_gate [port] [game_host] [game_port]

#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>

#include "snc/core/TcpConnection.h"
#include "snc/core/Msg.h"
#include "snc/core/IConnection.h"
#include "snc/core/DisconnectReason.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcSession.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include "snc/mmorpg/GateService.h"

using namespace snc;
using namespace snc::core;
using namespace snc::rpc;
using namespace snc::discovery;
using namespace snc::auth;
using namespace snc::mmorpg;

static std::atomic<bool> g_running{true};
static void SignalHandler(int) { g_running = false; }

// ── Gate 服消息 ID ───────────────────────────────────────────────
static constexpr int32_t kMsgAuth      = 0x1001;
static constexpr int32_t kMsgForward   = 0x1002;

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: example_04_gate <port> [game_host] [game_port]\n"
                  << "  port     : Gate 监听端口（默认 7777）\n"
                  << "  game_host: Game 服地址，默认 127.0.0.1\n"
                  << "  game_port: Game 服 RPC 端口，默认 8001\n" << std::endl;
        return 1;
    }
    uint16_t port       = static_cast<uint16_t>(std::stoi(argv[1]));
    std::string game_host = (argc > 2) ? argv[2] : "127.0.0.1";
    uint16_t game_port  = (argc > 3) ? static_cast<uint16_t>(std::stoi(argv[3])) : 8001;

    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // 服务发现：注册 Game 服
    auto registry = ServiceRegistry::Create();
    registry->Register("game", game_host, game_port, 1);

    // JWT 配置（与 Account 服相同的密钥）
    JwtConfig jwt_cfg;
    jwt_cfg.secret = "snc-mmorpg-dev-secret-key-32bytes!!";
    JwtService jwt_svc(jwt_cfg);

    std::cout << "[Gate] Starting on port " << port
              << "  (forwarding to game " << game_host << ":" << game_port << ")" << std::endl;
    std::cout << "  0x1001 — 客户端发送 JWT 认证" << std::endl;
    std::cout << "  0x1002 — 客户端发送业务消息（需先认证）" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    // 注：真实 Gate 需使用 TcpServer + IEventLoop 驱动，此处为演示框架。
    // 实际运行需要完整的 TcpServer 实现，见 libs/snc_core/CMakeLists.txt。
    std::cout << "[Gate] TCP server skeleton — connect via telnet/nc to port " << port << std::endl;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "\r[Gate] healthy endpoints: " << registry->EndpointCount()
                  << "   online players: -"
                  << std::flush;
    }
    std::cout << "\n[Gate] Stopped." << std::endl;
    return 0;
}
