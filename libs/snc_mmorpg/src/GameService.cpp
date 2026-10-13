/// @file GameService.cpp
/// @brief Game 服 — 游戏逻辑、Actor 管理、地图、心跳、优雅停机（stub 实现）。

#include "snc/mmorpg/GameService.h"
#include "snc/rpc/RpcServer.h"
#include "snc/discovery/ServiceRegistry.h"
#include "snc/base/Log.h"

#include <thread>
#include <chrono>
#include <sstream>

namespace snc {
namespace mmorpg {

// ── PlayerActor ────────────────────────────────────────────────────

void PlayerActor::OnMessage(int32_t msgId, const char* payload, size_t len) {
    (void)msgId; (void)payload; (void)len;
}

void PlayerActor::Tick(long long elapsedMs) {
    (void)elapsedMs;
}

// ── GameService ────────────────────────────────────────────────────

GameService::GameService(rpc::RpcClient::Ptr master_rpc,
                         discovery::ServiceRegistry::Ptr registry,
                         const auth::JwtConfig& jwtCfg)
    : master_rpc_(std::move(master_rpc))
    , registry_(std::move(registry))
    , jwt_cfg_(jwtCfg)
    , map_mgr_(std::make_shared<MapManager>()) {}

void GameService::Start(uint16_t rpcPort, const std::string& serviceName) {
    service_name_ = serviceName;
    running_ = true;
    if (registry_)
        registry_->Register(serviceName, "127.0.0.1", rpcPort, 1);
    HeartbeatLoop_();
}

void GameService::Shutdown() {
    running_ = false;
    if (heartbeat_thread_.joinable())
        heartbeat_thread_.join();
    SNC_LOG_INFO("game", "Service %s shut down. players=%zu",
                 service_name_.c_str(), players_.size());
}

void GameService::InitMaps() {
    struct MapDef { int id; const char* name; int w, h; };
    constexpr MapDef defs[] = {
        {1,  "新手村",   100, 100},
        {2,  "主城广场", 200, 200},
        {3,  "副本-巨龙巢穴", 150, 150},
    };
    for (auto& d : defs) {
        MapInfo info{};
        info.mapId   = d.id;
        info.name    = d.name;
        info.width   = d.w;
        info.height  = d.h;
        map_mgr_->CreateMap(info);
    }
    SNC_LOG_INFO("game", "Initialized %zu maps", sizeof(defs)/sizeof(defs[0]));
}

GameService::PlayerPtr GameService::FindPlayer(int32_t player_id) const {
    std::lock_guard<std::mutex> lk(players_mtx_);
    auto it = players_.find(player_id);
    return (it != players_.end()) ? it->second : nullptr;
}

void GameService::OnFromGate_(core::IConnection::Ptr conn,
                              rpc::RpcSession::Ptr session,
                              core::Msg::Ptr msg,
                              std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session;
    if (!msg || !msg->Payload()) {
        respond(rpc::RpcError::UNKNOWN, nullptr, 0);
        return;
    }
    std::string s(msg->Payload(), msg->PayloadLen());
    int32_t player_id = 0, msg_id = 0;
    auto extract_int = [&](const char* key) {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = s.find(kv);
        if (p == std::string::npos) return 0;
        try { return std::stoi(s.substr(p + kv.size())); } catch (...) { return 0; }
    };
    player_id = extract_int("player_id");
    msg_id    = extract_int("msg_id");

    PlayerPtr plr = FindPlayer(player_id);
    if (plr) {
        plr->OnMessage(msg_id, msg->Payload(), msg->PayloadLen());
        std::string resp = "{\"status\":\"ok\",\"player\":" + std::to_string(player_id) + "}";
        respond(rpc::RpcError::OK,
                reinterpret_cast<const uint8_t*>(resp.c_str()), resp.size());
    } else {
        std::string err = "{\"error\":\"player_not_found\",\"player_id\":" + std::to_string(player_id) + "}";
        respond(rpc::RpcError::UNKNOWN,
                reinterpret_cast<const uint8_t*>(err.c_str()), err.size());
    }
}

GameService::PlayerPtr GameService::CreatePlayer_(int32_t playerId, const std::string& name,
                                                   core::IConnection::Ptr conn) {
    auto plr = std::make_shared<PlayerActor>(playerId, name, conn, map_mgr_);
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        players_[playerId] = plr;
    }
    auto map_inst = map_mgr_->GetMap(1);
    if (map_inst) map_inst->AddPlayer(playerId, conn_handle_t::from(conn));
    SNC_LOG_INFO("game", "Player %s (id=%d) logged in", name.c_str(), playerId);
    return plr;
}

void GameService::RemovePlayer_(int32_t playerId) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    auto it = players_.find(playerId);
    if (it == players_.end()) return;
    auto map_inst = map_mgr_->GetMap(1);
    if (map_inst) map_inst->RemovePlayer(playerId);
    players_.erase(it);
    SNC_LOG_INFO("game", "Player %d logged out", playerId);
}

void GameService::HeartbeatLoop_() {
    heartbeat_thread_ = std::thread([this]() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (!running_.load()) break;
            std::ostringstream oss;
            oss << "{\"service_name\":\"" << service_name_
                << "\",\"type\":0,\"player_count\":" << players_.size()
                << ",\"map_count\":3,\"uptime_secs\":0}";
            SNC_LOG_DEBUG("game", "Heartbeat sent: %s", oss.str().c_str());
        }
    });
}

} // namespace mmorpg
} // namespace snc
