/// @file example_03_game.cpp
/// @brief 示例 03 — Game 服（游戏逻辑 + Actor 管理 + 地图 + 优雅停机）
///
/// 功能：
///   - 注册到 Master 服（RPC 0x3000 心跳）
///   - 接受 Gate 转发的消息（RPC 0x2000 起始）
///   - 玩家 Actor：登录、移动、广播
///   - MapManager：创建地图、广播、传送
///   - 优雅停机：SIGTERM → 等待玩家退出 → 注销
///
/// 启动：cmake --build . --target example_03_game && ./example_03_game [rpc_port] [master_host] [master_port]

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcSession.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/core/IConnection.h"
#include "snc/core/Msg.h"
#include "snc/core/DisconnectReason.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include "snc/mmorpg/GameService.h"
#include "snc/mmorpg/MapManager.h"

using namespace snc;
using namespace snc::rpc;
using namespace snc::discovery;
using namespace snc::core;
using namespace snc::mmorpg;

// ── 简易 TCP 服务器包装（演示用）────────────────────────────────
// 生产代码应使用 snc::core::Acceptor + EventLoop；此处仅展示示例结构。
#include "snc/core/IEventLoop.h"

static std::atomic<bool> g_running{true};
static void SignalHandler(int) { g_running = false; }

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: example_03_game <rpc_port> [master_host] [master_port]\n"
                  << "  rpc_port       : 本服 RPC 监听端口（Gate 转发到此）\n"
                  << "  master_host    : Master 服地址，默认 127.0.0.1\n"
                  << "  master_port    : Master 服 RPC 端口，默认 9000\n" << std::endl;
        return 1;
    }
    uint16_t rpc_port  = static_cast<uint16_t>(std::stoi(argv[1]));
    std::string master_host = (argc > 2) ? argv[2] : "127.0.0.1";
    uint16_t master_port = (argc > 3) ? static_cast<uint16_t>(std::stoi(argv[3])) : 9000;

    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    auto registry = ServiceRegistry::Create();
    auto master_rpc = std::make_shared<RpcClient>(
        std::make_shared<RpcSession>(nullptr));  // 临时，将在连接建立后设置

    // 连接 Master 服
    auto master_conn_factory = [&]() -> IConnection::Ptr {
        // 简化：此处演示目的，不实际建立连接
        return nullptr;
    };
    (void)master_conn_factory;

    GameService svc(master_rpc, registry, auth::JwtConfig{});

    std::cout << "[Game] Starting on RPC port " << rpc_port
              << "  (Master: " << master_host << ":" << master_port << ")" << std::endl;
    std::cout << "  RPC 0x3000 — 向 Master 注册/心跳" << std::endl;
    std::cout << "  RPC 0x2000 — 接收 Gate 转发的客户端消息" << std::endl;
    std::cout << "  RPC 0x2100 — 向 Gate 发送响应" << std::endl;
    std::cout << "Press Ctrl+C for graceful shutdown." << std::endl;

    // 初始化地图
    svc.InitMaps();

    // 启动 Game 服
    svc.Start(rpc_port, "game_01");

    // 事件循环（模拟）
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "\r[Game] online players: " << svc.OnlinePlayerCount()
                  << "   maps loaded: " << svc.GetMapManager()->TotalPlayerCount()
                  << std::flush;
    }

    std::cout << "\n[Game] Shutdown requested, waiting for players to logout (30s timeout)..."
              << std::endl;
    svc.Shutdown();
    std::cout << "[Game] Shutdown complete." << std::endl;
    return 0;
}
