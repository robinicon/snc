// SPDX-License-Identifier: MIT
/// @file GateService.cpp
/// @brief Gate 服 — TCP/WebSocket 接入、JWT 本地验签、消息路由。

#include "snc/mmorpg/GateService.h"
#include "snc/rpc/RpcClient.h"
#include "snc/core/SocketWrap.h"
#include "snc/core/TcpConnection.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {

// ── 公共接口 ──────────────────────────────────────────────────────

GateService::GateService(discovery::ServiceRegistry::Ptr registry,
                         const auth::JwtConfig& jwtCfg)
    : registry_(std::move(registry))
    , jwt_service_(jwtCfg)
#ifdef SNC_USE_ASIO
    , io_ctx_()
    , event_loop_(std::make_unique<EventLoop>())
    , server_(std::make_unique<TcpServer>(*event_loop_, io_ctx_))
#else
    , event_loop_(std::make_unique<EventLoop>())
    , acceptor_(std::make_unique<Acceptor>())
#endif
{
}

bool GateService::Start(const std::string& host, uint16_t port) {
#ifdef SNC_USE_ASIO
    if (!server_->Listen(port, host)) {
        SNC_LOG_ERROR("gate", "Failed to listen on %s:%d", host.c_str(), port);
        return false;
    }
    server_->SetCallbacks(
        [this](core::EntityHandle<Conn> eh) { OnNewConn_(eh); },
        [this](core::EntityHandle<Conn> eh, Msg::Ptr msg) { OnMsg_(eh, std::move(msg)); },
        [this](Conn::Ptr conn, core::DisconnectReason reason) { OnClose_(conn, reason); }
    );
    server_->Start();
    // 在独立线程运行 io_context，避免阻塞调用方
    loop_thread_ = std::thread([this] { io_ctx_.run(); });
#else
    if (!acceptor_->Listen(host.c_str(), port)) {
        SNC_LOG_ERROR("gate", "Failed to listen on %s:%d", host.c_str(), port);
        return false;
    }
    acceptor_->SetNewConnectionCallback(
        [this](Conn::Ptr conn) {
            if (!conn) return;
            // 绑定本连接的 message/close 回调
            auto handle = conn->GetConnHandle();
            conn->SetMessageCallback([this, handle](core::EntityHandle<Conn> eh, Msg::Ptr msg) {
                auto c = eh.lock();
                if (c) OnMsg_(eh, std::move(msg));
            });
            conn->SetCloseCallback([this, handle](Conn::Ptr c, core::DisconnectReason r) {
                OnClose_(c, r);
            });
            // 注册到 event loop（心跳、fd 管理由 loop 负责）
            event_loop_->AddConnection(conn);
            OnNewConn_(handle);
        });
    // 将 accept 回调注入 event loop 的 accept loop
    event_loop_->SetAcceptCallback([this] { acceptor_->AcceptOnce(); });
#endif
    running_ = true;
    SNC_LOG_INFO("gate", "Gate started on %s:%d", host.c_str(), port);
    return true;
}

void GateService::Stop() {
    running_ = false;
#ifdef SNC_USE_ASIO
    io_ctx_.stop();
    if (loop_thread_.joinable()) loop_thread_.join();
    if (server_) server_->Stop();
#else
    if (acceptor_) acceptor_->Shutdown();
    if (event_loop_) event_loop_->Stop();
#endif
    SNC_LOG_INFO("gate", "Gate stopped");
}

void GateService::Run() {
#ifdef SNC_USE_ASIO
    // Asio 后端：io_context 已在独立线程中运行，此处无需额外操作。
    // 调用方应等待 loop_thread_ 自然退出（Stop() 触发）。
    (void)event_loop_;
#else
    if (event_loop_) event_loop_->Run();
#endif
}

// ── 后端无关回调 ──────────────────────────────────────────────────

void GateService::OnNewConn_(core::EntityHandle<Conn> eh) {
    auto conn = eh.lock();
    if (!conn) return;
    SNC_LOG_INFO("gate", "New connection fd=%d", conn->Fd());
    online_players_++;
}

void GateService::OnMsg_(core::EntityHandle<Conn> eh, Msg::Ptr msg) {
    auto conn = eh.lock();
    if (!conn) return;
    OnClientMessage_(conn, std::move(msg));
}

void GateService::OnClose_(Conn::Ptr conn, core::DisconnectReason /*reason*/) {
    if (!conn) return;
    online_players_--;
    SNC_LOG_INFO("gate", "Client disconnected fd=%d", conn->Fd());
    std::lock_guard<std::mutex> lk(fd_mtx_);
    fd_to_player_.erase(conn->Fd());
}

// ── 消息处理 ──────────────────────────────────────────────────────

void GateService::OnClientMessage_(Conn::Ptr conn, Msg::Ptr msg) {
    if (!msg || !msg->Payload()) return;
    int32_t msgId = msg->MsgId();
    if (msgId == kMsgGateAuth) {
        HandleAuth_(conn, msg->Payload(), msg->PayloadLen());
    } else if (msgId == kMsgGateForward) {
        HandleForward_(conn, msg->Payload(), msg->PayloadLen());
    } else {
        SNC_LOG_WARN("gate", "Unknown msgId=%d from fd=%d", msgId, conn->Fd());
    }
}

void GateService::HandleAuth_(Conn::Ptr conn, const char* payload, size_t len) {
    std::string token(payload, len);
    auto jwt_token = jwt_service_.Verify(token);
    if (!jwt_token) {
        SNC_LOG_WARN("gate", "Auth failed: invalid token from fd=%d", conn->Fd());
        conn->Send(Msg::Create(0x1FFF, "auth_failed", 11));
        return;
    }
    std::string user_id = jwt_token->subject.empty() ? "unknown" : jwt_token->subject;
    {
        std::lock_guard<std::mutex> lk(fd_mtx_);
        auto info = std::make_shared<PlayerInfo>();
        info->user_id = user_id;
        fd_to_player_[conn->Fd()] = info;
    }
    SNC_LOG_INFO("gate", "Auth ok: fd=%d user=%s", conn->Fd(), user_id.c_str());
    conn->Send(Msg::Create(0x1FFE, "auth_ok", 7));
}

void GateService::HandleForward_(Conn::Ptr conn, const char* payload, size_t len) {
    int fd = conn->Fd();
    {
        std::lock_guard<std::mutex> lk(fd_mtx_);
        auto it = fd_to_player_.find(fd);
        if (it == fd_to_player_.end()) {
            conn->Send(Msg::Create(0x1FFE, "not_authed", 10));
            return;
        }
    }
    ForwardToGame_(conn, 0x2000, payload, len);
}

// ── 游戏服 RPC 连接管理 ───────────────────────────────────────────

/// 建立一个到指定 host:port 的出站 TCP 连接，注册到 event_loop_，返回 Conn::Ptr。
GateService::Conn::Ptr GateService::ConnectToGameServer_(const std::string& host, uint16_t port) {
    // 统一使用 core::SocketWrap + 阻塞 Connect（跨平台兼容），两种后端均走同一路径。
    core::SocketWrap sock = core::SocketWrap::CreateTCP();
    if (!sock.Valid()) {
        SNC_LOG_ERROR("gate", "Failed to create TCP socket for game server %s:%d",
                      host.c_str(), port);
        return nullptr;
    }
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = inet_addr(host.c_str());
#else
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#endif
    if (addr.sin_addr.s_addr == 0) {
        SNC_LOG_ERROR("gate", "Invalid game server address %s", host.c_str());
        return nullptr;
    }
    if (!sock.Connect(addr)) {
        SNC_LOG_ERROR("gate", "Failed to connect to game %s:%d", host.c_str(), port);
        return nullptr;
    }
    sock.SetNoDelay();
    sock.SetNonBlocking();
    auto conn = std::make_shared<core::TcpConnection>(std::move(sock));
    if (!event_loop_) {
        SNC_LOG_ERROR("gate", "Event loop is null — cannot add connection to game server %s:%d",
                      host.c_str(), port);
        return nullptr;
    }
    event_loop_->AddConnection(conn);
    return conn;
}

/// 连接关闭回调：从 game_conns_ 中清理，若注册了 rpc_client 则触发重连
void GateService::OnGameConnClose_(const std::string& /*key*/, Conn::Ptr /*conn*/) {
    // RpcClient 内部已处理重连，这里只需记录日志；
    // 若 rpc_client 也被销毁（从 map 移除），则不再重连
}

/// 获取或创建一个到指定 game server 的 RpcClient。
/// 使用 game_conns_ 按 host:port 缓存连接和 RpcClient。
rpc::RpcClient::Ptr GateService::GetOrCreateGameRpcClient_(const std::string& host, uint16_t port) {
    std::string key = host + ":" + std::to_string(port);

    // 先快速检查是否已有连接
    {
        std::lock_guard<std::mutex> lk(game_conns_mtx_);
        auto it = game_conns_.find(key);
        if (it != game_conns_.end() && it->second->conn && it->second->rpc_client) {
            return it->second->rpc_client;
        }
    }

    // 没有连接或连接已断开，重建
    auto conn = ConnectToGameServer_(host, port);
    if (!conn) return nullptr;

    // 构造 Entity → RpcSession → RpcClient 链
    auto entity  = std::make_shared<rpc::Entity>(conn);
    auto session = std::make_shared<rpc::RpcSession>(entity);

    // ConnFactory：断线重连时重新建立连接
    std::string host_copy = host;
    uint16_t    port_copy = port;
    auto factory = [this, host_copy, port_copy]() -> Conn::Ptr {
        return ConnectToGameServer_(host_copy, port_copy);
    };

    rpc::RpcClient::ReconnectOptions opts;
    opts.maxRetries    = 5;
    opts.baseDelayMs   = 500;
    opts.maxDelayMs    = 10000;

    // 先构建 RpcClient，再设置连接（触发 OnDisconnect 回调）
    auto rpc_client = std::make_shared<rpc::RpcClient>(session, event_loop_.get(), factory, opts);
    rpc_client->SetConnection(conn);

    // 保存 close 回调以便清理 map
    auto handle = conn->GetConnHandle();
    conn->SetCloseCallback([this, key, handle](Conn::Ptr c, core::DisconnectReason reason) {
        (void)reason;
        SNC_LOG_WARN("gate", "Game conn closed %s, fd=%d", key.c_str(), c ? c->Fd() : -1);
        std::lock_guard<std::mutex> lk(game_conns_mtx_);
        auto it = game_conns_.find(key);
        if (it != game_conns_.end() && it->second->conn == c) {
            it->second->conn.reset();
            it->second->rpc_client.reset();
            game_conns_.erase(it);
        }
    });

    // 存入 map
    {
        std::lock_guard<std::mutex> lk(game_conns_mtx_);
        auto gc = std::make_shared<GameConn>();
        gc->conn      = conn;
        gc->rpc_client = rpc_client;
        game_conns_[key] = gc;
    }

    SNC_LOG_INFO("gate", "Connected to game server %s", key.c_str());
    return rpc_client;
}

// ── 路由到 Game 服 ────────────────────────────────────────────────

void GateService::ForwardToGame_(Conn::Ptr client_conn, int32_t msgId,
                                  const char* payload, size_t len) {
    if (!client_conn) return;

    // 1. 从 ServiceRegistry 查找一个健康的 Game 服端点
    auto endpoints = registry_->GetHealthy(game_service_name_);
    if (endpoints.empty()) {
        SNC_LOG_WARN("gate", "No healthy game server endpoint found for forwarding msgId=%d", msgId);
        client_conn->Send(Msg::Create(0x1FFE, "no_game_server", 14));
        return;
    }

    // 随机选择一个端点（负载均衡）
    auto entry = registry_->Pick(discovery::BalanceStrategy::RANDOM, game_service_name_);
    if (!entry) {
        SNC_LOG_WARN("gate", "Failed to pick game server endpoint");
        client_conn->Send(Msg::Create(0x1FFE, "no_game_server", 14));
        return;
    }

    // 2. 获取或创建到该 Game 服的 RpcClient
    auto rpc_client = GetOrCreateGameRpcClient_(entry->host, entry->port);
    if (!rpc_client) {
        SNC_LOG_WARN("gate", "Failed to connect to game server %s:%d",
                     entry->host.c_str(), entry->port);
        client_conn->Send(Msg::Create(0x1FFE, "game_conn_fail", 14));
        return;
    }

    // 3. 通过 RpcClient 发送消息到 Game 服
    //    reply 回调：将 Game 服的响应原样转发回客户端
    auto weak_conn = std::weak_ptr<Conn>(client_conn);
    rpc_client->Call(msgId, payload, len,
        [weak_conn](rpc::RpcError err, const uint8_t* data, size_t data_len) {
            auto conn = weak_conn.lock();
            if (!conn) return;
            if (err != rpc::RpcError::OK) {
                SNC_LOG_WARN("gate", "Game server RPC error for msgId=%d: %d",
                             0x2000, static_cast<int>(err));
                conn->Send(Msg::Create(0x1FFF, "game_error", 10));
                return;
            }
            // 将 Game 服的响应作为业务消息转发给客户端（msgId 保持原值）
            if (data && data_len > 0) {
                auto resp_msg = Msg::Create(0x2001, reinterpret_cast<const char*>(data), data_len);
                conn->Send(resp_msg);
            }
        });
}

} // namespace mmorpg
} // namespace snc
