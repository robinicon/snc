/// @file WorldService.cpp
/// @brief World 服 — 跨服场景管理 + 场景锁状态机（stub 实现）。

#include "snc/mmorpg/WorldService.h"
#include "snc/rpc/RpcServer.h"
#include "snc/base/Log.h"

#include <sstream>
#include <chrono>

namespace snc {
namespace mmorpg {

WorldService::WorldService(rpc::RpcClient::Ptr master_rpc,
                           discovery::ServiceRegistry::Ptr registry)
    : master_rpc_(std::move(master_rpc))
    , registry_(std::move(registry)) {}

void WorldService::Start(uint16_t port) {
    (void)port;
    running_ = true;
}

void WorldService::Stop() {
    running_ = false;
}

int32_t WorldService::CreateScene(const std::string& sceneName,
                                   const std::vector<std::string>& gameList) {
    (void)gameList;
    std::lock_guard<std::mutex> lk(scenes_mtx_);
    int32_t id = next_scene_id_++;
    auto scene = std::make_shared<SceneInfo>();
    scene->sceneId   = id;
    scene->name      = sceneName;
    scene->state     = SceneState::CREATING;
    scene->create_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    scenes_[id] = scene;
    NotifyMaster_(id, SceneState::CREATING);
    SNC_LOG_INFO("world", "Scene created id=%d name=%s", id, sceneName.c_str());
    return id;
}

bool WorldService::StartScene(int32_t sceneId) {
    std::lock_guard<std::mutex> lk(scenes_mtx_);
    auto it = scenes_.find(sceneId);
    if (it == scenes_.end()) return false;
    if (!Transition_(sceneId, SceneState::CREATING, SceneState::ACTIVE) &&
        !Transition_(sceneId, SceneState::IDLE,     SceneState::ACTIVE)) return false;
    NotifyMaster_(sceneId, SceneState::ACTIVE);
    return true;
}

bool WorldService::EndScene(int32_t sceneId) {
    std::lock_guard<std::mutex> lk(scenes_mtx_);
    auto it = scenes_.find(sceneId);
    if (it == scenes_.end()) return false;
    if (!Transition_(sceneId, SceneState::ACTIVE, SceneState::ENDING)) return false;
    Transition_(sceneId, SceneState::ENDING, SceneState::IDLE);
    NotifyMaster_(sceneId, SceneState::IDLE);
    return true;
}

std::string WorldService::QueryScene(int32_t sceneId) const {
    std::lock_guard<std::mutex> lk(scenes_mtx_);
    auto it = scenes_.find(sceneId);
    if (it == scenes_.end()) return "not_found";
    return std::string("scene_id=") + std::to_string(sceneId)
         + " state=" + std::to_string((int)it->second->state)
         + " name="  + it->second->name;
}

std::vector<std::string> WorldService::ListScenes() const {
    std::lock_guard<std::mutex> lk(scenes_mtx_);
    std::vector<std::string> out;
    for (auto& [id, s] : scenes_)
        out.push_back(s->name);
    return out;
}

void WorldService::RegisterRpcHandlers(rpc::RpcServer& rpcServer) {
    using namespace rpc;
    rpcServer.RegisterHandler(0x3100,
        [this](IConnection::Ptr conn, RpcSession::Ptr session, Msg::Ptr msg,
               std::function<void(RpcError, const uint8_t*, size_t)> respond) {
            OnSceneRequest_(conn, session, std::move(msg), std::move(respond));
        });
}

bool WorldService::Transition_(int32_t sceneId, SceneState from, SceneState to) {
    auto it = scenes_.find(sceneId);
    if (it == scenes_.end() || it->second->state != from) return false;
    it->second->state = to;
    return true;
}

void WorldService::NotifyMaster_(int32_t sceneId, SceneState newState) {
    (void)sceneId; (void)newState;
    std::ostringstream oss;
    oss << "{\"event\":\"scene_state_change\",\"scene_id\":" << sceneId
        << ",\"state\":" << (int)newState << "}";
    SNC_LOG_DEBUG("world", "NotifyMaster: %s", oss.str().c_str());
}

void WorldService::NotifyGameServers_(int32_t sceneId, SceneState newState,
                                       const std::vector<int32_t>& gameIds) {
    std::ostringstream oss;
    if (newState == SceneState::ACTIVE) {
        oss << "{\"event\":\"scene_begin\",\"scene_id\":" << sceneId << "}";
    } else if (newState == SceneState::ENDING || newState == SceneState::IDLE) {
        oss << "{\"event\":\"scene_end\",\"scene_id\":" << sceneId << "}";
    }
    std::string payload = oss.str();
    std::lock_guard<std::mutex> lk(cross_mtx_);
    auto sit = cross_scene_clients_.find(sceneId);
    if (sit == cross_scene_clients_.end()) return;
    for (int32_t gid : gameIds) {
        auto git = sit->second.find(gid);
        if (git != sit->second.end() && git->second) {
            git->second->Call(0x3100, payload.c_str(), payload.size(), nullptr);
        }
    }
}

int32_t WorldService::CreateCrossScene(const std::string& sceneName,
                                        const std::vector<int32_t>& gameServerIds) {
    int32_t id = CreateScene(sceneName, {});
    if (id < 0) return -1;
    // 记录跨服路由表（从 ServiceRegistry 解析 Game 服地址）
    std::lock_guard<std::mutex> lk(cross_mtx_);
    cross_scene_clients_[id] = {};
    if (registry_) {
        auto endpoints = registry_->GetHealthy("game");
        for (auto& ep : endpoints) {
            for (int32_t gid : gameServerIds) {
                // 简化：用 endpoint host:port 作为标识，实际应传入 game 服务名
                (void)gid; (void)ep;
                // 真实实现需要通过 registry 查询具体 game_XX 实例
            }
        }
    }
    SNC_LOG_INFO("world.cross", "Cross-scene %d created for %zu game servers",
                 id, gameServerIds.size());
    return id;
}

void WorldService::OnGameServerDisconnected(int32_t gameServerId, int32_t sceneId) {
    (void)gameServerId;
    std::lock_guard<std::mutex> lk(scenes_mtx_);
    auto it = scenes_.find(sceneId);
    if (it == scenes_.end()) return;
    // 标记该服玩家离线（当前 stub：仅记录日志）
    SNC_LOG_WARN("world.cross", "Game server %d disconnected from scene %d — players marked offline",
                 gameServerId, sceneId);
    // 继续场景至结束：不立即终止，允许剩余服完成场景
}

bool WorldService::EndCrossScene(int32_t sceneId) {
    bool ok = EndScene(sceneId);
    if (!ok) return false;
    // 通知所有参与 Game 服
    std::vector<int32_t> gameIds;
    std::lock_guard<std::mutex> lk(cross_mtx_);
    auto it = cross_scene_clients_.find(sceneId);
    if (it != cross_scene_clients_.end()) {
        for (auto& [gid, _] : it->second) gameIds.push_back(gid);
    }
    NotifyGameServers_(sceneId, SceneState::IDLE, gameIds);
    return true;
}

void WorldService::OnSceneRequest_(core::IConnection::Ptr conn,
                                    rpc::RpcSession::Ptr session,
                                    core::Msg::Ptr msg,
                                    std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session;
    if (!msg || !msg->Payload()) {
        respond(rpc::RpcError::UNKNOWN, nullptr, 0);
        return;
    }
    std::string s(msg->Payload(), msg->PayloadLen());
    auto get_int = [&](const char* key) -> int {
        std::string kv = "\"" + std::string(key) + "\":";
        auto p = s.find(kv);
        if (p == std::string::npos) return -1;
        try { return std::stoi(s.substr(p + kv.size())); } catch (...) { return -1; }
    };
    int32_t scene_id = get_int("scene_id");
    std::string action = s;
    if (action.find("\"action\":\"create\"") != std::string::npos) {
        int32_t id = CreateScene("跨服场景", {});
        std::string resp = "{\"action\":\"create\",\"scene_id\":" + std::to_string(id) + "}";
        respond(rpc::RpcError::OK,
                reinterpret_cast<const uint8_t*>(resp.c_str()), resp.size());
    } else if (action.find("\"action\":\"start\"") != std::string::npos) {
        bool ok = StartScene(scene_id);
        std::string resp = "{\"action\":\"start\",\"scene_id\":" + std::to_string(scene_id)
                         + ",\"ok\":" + std::string(ok ? "true" : "false") + "}";
        respond(ok ? rpc::RpcError::OK : rpc::RpcError::UNKNOWN,
                reinterpret_cast<const uint8_t*>(resp.c_str()), resp.size());
    } else if (action.find("\"action\":\"end\"") != std::string::npos) {
        bool ok = EndScene(scene_id);
        std::string resp = "{\"action\":\"end\",\"scene_id\":" + std::to_string(scene_id)
                         + ",\"ok\":" + std::string(ok ? "true" : "false") + "}";
        respond(ok ? rpc::RpcError::OK : rpc::RpcError::UNKNOWN,
                reinterpret_cast<const uint8_t*>(resp.c_str()), resp.size());
    } else {
        std::string err = "{\"error\":\"unknown_action\"}";
        respond(rpc::RpcError::UNKNOWN,
                reinterpret_cast<const uint8_t*>(err.c_str()), err.size());
    }
}

} // namespace mmorpg
} // namespace snc
