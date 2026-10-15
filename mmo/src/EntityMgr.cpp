/// @file EntityMgr.cpp
/// @brief 实体管理器实现

#include "snc/mmorpg/legend/EntityMgr.h"

namespace snc {
namespace mmorpg {
namespace legend {

void EntityMgr::Register(Entity::Ptr e) {
    if (!e) return;
    std::lock_guard<std::mutex> lk(mtx_);
    by_id_[e->EntityId()] = e;
    by_type_[e->Type()][e->EntityId()] = e;
}

void EntityMgr::Unregister(int32_t entityId) {
    std::lock_guard<std::mutex> lk(mtx_);
    by_id_.erase(entityId);
    for (auto& [type, map] : by_type_) {
        map.erase(entityId);
    }
}

Entity::Ptr EntityMgr::Find(int32_t entityId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = by_id_.find(entityId);
    return (it != by_id_.end()) ? it->second : nullptr;
}

Entity::Ptr EntityMgr::FindByType(int32_t entityId, EntityType t) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto tit = by_type_.find(t);
    if (tit == by_type_.end()) return nullptr;
    auto it = tit->second.find(entityId);
    return (it != tit->second.end()) ? it->second : nullptr;
}

std::vector<Entity::Ptr> EntityMgr::FindAllByType(EntityType t) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = by_type_.find(t);
    if (it == by_type_.end()) return {};
    std::vector<Entity::Ptr> result;
    result.reserve(it->second.size());
    for (auto& [id, e] : it->second) result.push_back(e);
    return result;
}

size_t EntityMgr::CountByType(EntityType t) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = by_type_.find(t);
    return (it != by_type_.end()) ? it->second.size() : 0;
}

void EntityMgr::ForEachNearby(int32_t x, int32_t y, int32_t range,
                               EntityType t, const FindNearbyCb& cb) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto tit = by_type_.find(t);
    if (tit == by_type_.end()) return;
    PlayerPos ref{0, x, y};
    for (auto& [id, e] : tit->second) {
        if (e->Alive() && e->Pos().ManhattanDistTo(ref) <= range) {
            cb(e);
        }
    }
}

size_t EntityMgr::TotalCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return by_id_.size();
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
