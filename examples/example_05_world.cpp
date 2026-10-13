/// @file example_05_world.cpp
/// @brief 示例 05 — World 服（跨服场景管理 + 场景锁状态机）
///
/// 场景锁状态机（ADR-003）：
///   IDLE      → CREATING → ACTIVE → ENDING → IDLE
///   创建时拒绝新玩家，运行中允许进出，结束时等待玩家退出
///
/// 功能：
///   - RPC 创建跨服场景（通知参与 Game 服）
///   - RPC 启动/结束场景
///   - 状态转换验证
///   - 向 Master 服上报状态变更
///
/// 启动：cmake --build . --target example_05_world && ./example_05_world [port]

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
#include "snc/base/Log.h"
#include "snc/mmorpg/WorldService.h"

using namespace snc;
using namespace snc::rpc;
using namespace snc::discovery;
using namespace snc::core;
using namespace snc::mmorpg;

static std::atomic<bool> g_running{true};
static void SignalHandler(int) { g_running = false; }

// ── 简易 JSON 解析（演示用）───────────────────────────────────────
static std::string JsonStr(const std::string& s) {
    return "\"" + s + "\"";
}
static std::string JsonInt(int v) { return std::to_string(v); }
static std::string JsonBool(bool b) { return b ? "true" : "false"; }

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    uint16_t port = 9100;
    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    auto registry = ServiceRegistry::Create();
    // 模拟 Master RPC 客户端（为空，演示状态下不实际上报）
    auto master_rpc = std::make_shared<RpcClient>(
        std::make_shared<RpcSession>(nullptr));

    WorldService world(master_rpc, registry);

    // ── 本地 RPC 服务器（接收 Game 服的跨服请求）─────────────────
    RpcServer rpc_server;
    world.RegisterRpcHandlers(rpc_server);
    (void)rpc_server;  // 实际需在 TcpServer 中驱动

    std::cout << "[World] Starting on port " << port << std::endl;
    std::cout << "  Scene state machine: IDLE→CREATING→ACTIVE→ENDING→IDLE" << std::endl;
    std::cout << "  RPC handlers registered (see WorldService::RegisterRpcHandlers)" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    // 演示场景操作
    std::cout << "\n--- Demo: Create & Manage Scenes ---" << std::endl;

    // 创建场景
    auto games = std::vector<std::string>{"game_01", "game_02"};
    int32_t sid = world.CreateScene("跨服副本-巨龙巢穴", games);
    std::cout << "  Created scene id=" << sid << std::endl;

    // 查询状态
    std::cout << "  Status: " << world.QueryScene(sid) << std::endl;

    // 启动场景
    world.StartScene(sid);
    std::cout << "  Started scene id=" << sid << std::endl;
    std::cout << "  Status: " << world.QueryScene(sid) << std::endl;

    // 结束场景
    world.EndScene(sid);
    std::cout << "  Ended scene id=" << sid << std::endl;
    std::cout << "  Final status: " << world.QueryScene(sid) << std::endl;

    std::cout << "\nAll scenes: " << world.ListScenes().size() << std::endl;

    // 事件循环
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "\n[World] Stopped." << std::endl;
    return 0;
}
