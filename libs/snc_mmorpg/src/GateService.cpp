/// @file GateService.cpp
/// @brief Gate 服 — TCP/WebSocket 接入、JWT 本地验签、消息路由（stub 实现）。

#include "snc/mmorpg/GateService.h"
#include "snc/rpc/RpcClient.h"
#include "snc/base/Log.h"

#include <chrono>
#include <thread>

namespace snc {
namespace mmorpg {

GateService::GateService(discovery::ServiceRegistry::Ptr registry,
                         const auth::JwtConfig& jwtCfg)
    : registry_(std::move(registry))
    , jwt_service_(jwtCfg) {
    event_loop_  = std::make_unique<EventLoop>();
    acceptor_    = std::make_unique<Acceptor>();
}

bool GateService::Start(const std::string& host, uint16_t port) {
    if (!acceptor_->Listen(host.c_str(), port)) {
        SNC_LOG_ERROR("gate", "Failed to listen on %s:%d", host.c_str(), port);
        return false;
    }
    acceptor_->SetNewConnectionCallback(
        [this](Conn::Ptr conn) { OnNewConnection_(std::move(conn)); });
    running_ = true;
    SNC_LOG_INFO("gate", "Gate started on %s:%d", host.c_str(), port);
    return true;
}

void GateService::Stop() {
    running_ = false;
    if (event_loop_) event_loop_->Stop();
}

void GateService::OnNewConnection_(Conn::Ptr conn) {
    SNC_LOG_INFO("gate", "New connection fd=%d", conn->Fd());
    online_players_++;
    conn->SetMessageCallback([this, conn](core::EntityHandle<Conn> eh, Msg::Ptr msg) {
        auto c = eh.lock();
        if (!c) return;
        OnClientMessage_(c, std::move(msg));
    });
    conn->SetCloseCallback([this, conn](Conn::Ptr c, core::DisconnectReason reason) {
        (void)reason;
        online_players_--;
        SNC_LOG_INFO("gate", "Client disconnected fd=%d", c->Fd());
        event_loop_->RemoveConnection(c);
        std::lock_guard<std::mutex> lk(fd_mtx_);
        fd_to_player_.erase(c->Fd());
    });
    event_loop_->AddConnection(conn);
}

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
    std::shared_ptr<PlayerInfo> plr;
    {
        std::lock_guard<std::mutex> lk(fd_mtx_);
        auto it = fd_to_player_.find(fd);
        if (it == fd_to_player_.end()) {
            conn->Send(Msg::Create(0x1FFE, "not_authed", 10));
            return;
        }
        plr = it->second;
    }
    ForwardToGame_(conn, 0x2000, payload, len);
}

void GateService::ForwardToGame_(Conn::Ptr conn, int32_t msgId,
                                  const char* payload, size_t len) {
    (void)conn; (void)msgId; (void)payload; (void)len;
    (void)registry_;
    SNC_LOG_DEBUG("gate", "Forwarding msgId=%d to game (stub)", msgId);
}

} // namespace mmorpg
} // namespace snc
