/// @file MapManager.cpp
/// @brief 地图实例管理器实现

#include "snc/mmorpg/MapManager.h"
#include <algorithm>

namespace snc {
namespace mmorpg {

// ── MapInstance ───────────────────────────────────────────────────

bool MapInstance::AddPlayer(int32_t playerId, conn_handle_t conn) {
    if (players_.count(playerId)) return false;
    players_[playerId] = std::move(conn);
    return true;
}

void MapInstance::RemovePlayer(int32_t playerId) {
    players_.erase(playerId);
}

void MapInstance::Broadcast(int32_t msgId, const char* data, size_t len) {
    for (auto& [pid, conn] : players_) {
        auto c = conn.lock();
        if (c) c->Send(core::Msg::Create(msgId, data, len));
    }
}

conn_handle_t MapInstance::FindPlayer(int32_t playerId) const {
    auto it = players_.find(playerId);
    return (it != players_.end()) ? it->second : conn_handle_t{};
}

// ── MapManager ────────────────────────────────────────────────────

bool MapManager::CreateMap(const MapInfo& info) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (maps_.count(info.mapId)) return false;
    auto inst = factory_ ? factory_(info) : std::make_shared<MapInstance>(info);
    maps_[info.mapId] = inst;
    return true;
}

void MapManager::DestroyMap(int32_t mapId) {
    std::lock_guard<std::mutex> lk(mtx_);
    maps_.erase(mapId);
}

MapInstance::Ptr MapManager::GetMap(int32_t mapId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = maps_.find(mapId);
    return (it != maps_.end()) ? it->second : nullptr;
}

const MapInfo* MapManager::GetMapInfo(int32_t mapId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = maps_.find(mapId);
    return (it != maps_.end()) ? &it->second->Info() : nullptr;
}

size_t MapManager::BroadcastToMap(int32_t mapId, int32_t msgId, const char* data, size_t len) {
    auto mapInst = GetMap(mapId);
    if (!mapInst) return 0;
    mapInst->Broadcast(msgId, data, len);
    return mapInst->PlayerCount();
}

size_t MapManager::Teleport(int32_t playerId, int32_t targetMapId, int32_t x, int32_t y) {
    std::lock_guard<std::mutex> lk(mtx_);
    // 从当前地图移除（通过 MapInstance 的公开接口）
    for (auto& [mid, inst] : maps_) {
        inst->RemovePlayer(playerId);
        if (!inst->IsEmpty()) break;
    }
    // 加入目标地图（conn 句柄由上层传入，简化版本仅统计）
    auto target = maps_.find(targetMapId);
    if (target == maps_.end()) return 0;
    (void)x; (void)y;
    return target->second->PlayerCount();
}

size_t MapManager::TotalPlayerCount() {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t total = 0;
    for (auto& [id, m] : maps_) total += m->PlayerCount();
    return total;
}

std::vector<int32_t> MapManager::ListMapIds() const {
    std::vector<int32_t> ids;
    for (auto& [id, _] : maps_) ids.push_back(id);
    return ids;
}

} // namespace mmorpg
} // namespace snc
