// example_04_gate.cpp — Gate 服完整示例
//
// ⚠️ 本文件是概念演示草稿，暂不可编译。
// GateService 的实际 API 与示例中使用的 API 存在差异：
//   - 无 OnPlayerLogin() 公有方法（认证在 Start() 内部处理）
//   - 无 OnPlayerDisconnect() 公有方法（断开连接在内部处理）
//   - 示例中的消息路由逻辑需要适配实际 API
//
// 正确的使用方式请参考教程文档中的 Gate 服示例。
//
// 编译：
//   cmake --preset=normal -DBUILD_EXAMPLES=ON
//   cmake --build --config Release
//
// 运行（需先启动 Master 服 example_02_master）：
//   example_04_gate <master_host> <master_rpc_port> [jwt_secret]

#include "snc/core/TcpServer.h"
#include "snc/core/MsgRouter.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcSession.h"
#include "snc/rpc/Entity.h"
#include "snc/auth/AuthService.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/mmorpg/GateService.h"
#include "snc/base/Log.h"
#include <iostream>
#include <map>
#include <mutex>
#include <csignal>
#include <thread>
#include <atomic>

using namespace snc;
using namespace snc::core;
using namespace snc::rpc;
using namespace snc::auth;
using namespace snc::discovery;
using namespace snc::mmorpg;

static std::atomic<bool> g_running{true};
void SignalHandler(int) { g_running = false; }

// 消息 ID 定义（遵循 SNC 协议规划）
static constexpr int32_t kMsgAuth       = 0x1001;  // 客户端登录握手
static constexpr int32_t kMsgForward    = 0x1002;  // 客户端业务消息转发

int main(int argc, char* argv[])
{
    base::InitLogger(base::LogLevel::INFO);
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::string master_host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t    master_rpc_port = (argc > 2) ? static_cast<uint16_t>(std::stoi(argv[2])) : 9000;
    std::string jwt_secret = (argc > 3) ? argv[3] : "snc-dev-jwt-secret-key-must-be-at-least-32-bytes!";
    // ⚠️ 生产环境请使用 secure random 生成 ≥32 字节密钥，并通过环境变量传入

    // ── 1. 创建基础设施 ──────────────────────────────────────
    auto eventLoop   = CreateEventLoop();
    auto registry    = ServiceRegistry::Create();
    auto tcp_port    = static_cast<uint16_t>(9001);

    SNC_LOG_INFO("Gate", "Master RPC: %s:%d  TCP: %d",
                 master_host.c_str(), master_rpc_port, tcp_port);

    // ── 2. 建立到 Master 的 RPC 连接（演示用，简化版）──────────
    // 实际场景需要通过 Acceptor/Connector 建立真实 TCP 连接
    auto entity      = std::make_shared<Entity>(nullptr);
    auto session     = std::make_shared<RpcSession>(entity);
    auto master_rpc  = std::make_shared<RpcClient>(session, eventLoop.get(), nullptr);

    // ── 3. 服务发现注册 ───────────────────────────────────────
    registry->Register("gate", master_host, tcp_port, 1);

    // ── 4. JWT 配置 ───────────────────────────────────────────
    JwtConfig jwtCfg;
    jwtCfg.secret = jwt_secret;
    jwtCfg.expireSecs = 7200;

    // ── 5. 构造 GateService ───────────────────────────────────
    GateService gateSvc(registry, jwtCfg);

    // ── 6. 使用 TcpServer 接入客户端 ──────────────────────────
    TcpServer tcpServer(*eventLoop);

    // 维护 userId → Game 服 fd 的映射
    std::mutex                    fdMapMutex_;
    std::unordered_map<int32_t, int> userId_to_fd_;

    tcpServer.SetCallbacks(
        // onNewConnection
        [&gateSvc, &userId_to_fd_, &fdMapMutex_](EntityHandle<IConnection> conn) {
            auto c = conn.lock();
            if (!c) return;
            SNC_LOG_INFO("Gate", "New client connected fd=%d", c->Fd());
        },
        // onMessage
        [&gateSvc, &userId_to_fd_, &fdMapMutex_, master_rpc](EntityHandle<IConnection> conn, Msg::Ptr msg) {
            auto c = conn.lock();
            if (!c) return;

            int32_t msgId = msg->MsgId();
            const char* payload = msg->Payload();
            int32_t payloadLen = msg->PayloadLen();

            if (msgId == kMsgAuth) {
                // 登录消息：解析 userId，路由到对应 Game 服
                // 简化：假设 payload 是 userId 字符串
                int32_t userId = 0;
                if (payloadLen >= sizeof(int32_t)) {
                    std::memcpy(&userId, payload, sizeof(int32_t));
                }

                {
                    std::lock_guard<std::mutex> lock(fdMapMutex_);
                    userId_to_fd_[userId] = c->Fd();
                }
                gateSvc.OnPlayerLogin(c, userId);
                SNC_LOG_INFO("Gate", "Player login: userId=%d fd=%d", userId, c->Fd());

                // 发送登录成功响应
                int32_t ok = 1;
                auto resp = Msg::Create(0x1101, reinterpret_cast<const char*>(&ok), sizeof(ok));
                c->Send(resp);

            } else if (msgId == kMsgForward) {
                // 业务消息：根据 userId 转发到 Game 服
                int32_t userId = 0;
                if (payloadLen >= sizeof(int32_t)) {
                    std::memcpy(&userId, payload, sizeof(int32_t));
                }

                int targetFd;
                {
                    std::lock_guard<std::mutex> lock(fdMapMutex_);
                    auto it = userId_to_fd_.find(userId);
                    targetFd = (it != userId_to_fd_.end()) ? it->second : -1;
                }

                if (targetFd >= 0) {
                    // 转发到目标 Game 服（简化：直接通过 master_rpc 转发）
                    master_rpc->Call(0x3002, payload, payloadLen,
                        [c](RpcError err, const uint8_t* resp, size_t len) {
                            if (err == RpcError::OK && len > 0) {
                                c->Send(Msg::Create(0x1102,
                                    reinterpret_cast<const char*>(resp), len));
                            }
                        });
                } else {
                    SNC_LOG_WARN("Gate", "Player %d not found, dropping msg", userId);
                }
            }
        },
        // onClose
        [&gateSvc, &userId_to_fd_, &fdMapMutex_](IConnection::Ptr conn, DisconnectReason reason) {
            int fd = conn->Fd();
            SNC_LOG_INFO("Gate", "Client disconnected fd=%d reason=%d", fd, (int)reason);

            // 清理 userId → fd 映射
            std::vector<int32_t> toRemove;
            {
                std::lock_guard<std::mutex> lock(fdMapMutex_);
                for (auto& [uid, f] : userId_to_fd_) {
                    if (f == fd) toRemove.push_back(uid);
                }
                for (auto uid : toRemove) userId_to_fd_.erase(uid);
            }
            // GateService 内部已处理断开连接清理，无需额外调用
        }
    );

    // 设置 WorkerPool 分派（可选）
    auto workerPool = WorkerPool::Create(4);
    tcpServer.SetDispatchToWorkerPool(workerPool);

    if (!tcpServer.Listen(tcp_port)) {
        SNC_LOG_ERROR("Gate", "Failed to listen on port %d", tcp_port);
        return 1;
    }
    tcpServer.Start();

    // ── 7. 启动 Gate 服务内部 RPC ───────────────────────────
    gateSvc.Start("0.0.0.0", tcp_port);

    SNC_LOG_INFO("Gate", "Gate service started. Press Ctrl+C to stop.");

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        SNC_LOG_INFO("Gate", "Active connections: %zu", tcpServer.ActiveCount());
    }

    tcpServer.Stop();
    gateSvc.Stop();
    workerPool->Stop();
    SNC_LOG_INFO("Gate", "Shutdown complete.");
    return 0;
}
