/// @file Scene.cpp
/// @brief 场景实现 — 实体容器 + 双频 Tick 驱动

#include "snc/mmorpg/legend/Scene.h"
#include "snc/mmorpg/legend/EntityMgr.h"
#include "snc/mmorpg/legend/PlayerActor.h"
#include <algorithm>

namespace snc {
namespace mmorpg {
namespace legend {

Scene::Scene(const SceneConfig& cfg)
    : config_(cfg), entity_mgr_(std::make_shared<EntityMgr>()) {}

void Scene::AddEntity(Entity::Ptr e) {
    if (!e) return;
    std::lock_guard<std::mutex> lk(mtx_);
    e->SetSceneId(config_.scene_id);
    entity_mgr_->Register(e);
}

void Scene::RemoveEntity(int32_t entityId) {
    std::lock_guard<std::mutex> lk(mtx_);
    entity_mgr_->Unregister(entityId);
}

Entity::Ptr Scene::FindEntity(int32_t entityId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return entity_mgr_->Find(entityId);
}

size_t Scene::EntityCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return entity_mgr_->TotalCount();
}

std::vector<Entity::Ptr> Scene::FindNearby(int32_t x, int32_t y,
                                            int32_t range) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<Entity::Ptr> result;
    PlayerPos ref{0, x, y};
    auto addIfNearby = [&](const std::vector<Entity::Ptr>& list) {
        for (auto& e : list) {
            if (e->Alive() && e->Pos().ManhattanDistTo(ref) <= range)
                result.push_back(e);
        }
    };
    addIfNearby(entity_mgr_->FindAllByType(EntityType::PLAYER));
    addIfNearby(entity_mgr_->FindAllByType(EntityType::MONSTER));
    addIfNearby(entity_mgr_->FindAllByType(EntityType::PET));
    return result;
}

void Scene::Broadcast(int32_t msgId, const char* data, size_t len) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& e : entity_mgr_->FindAllByType(EntityType::PLAYER)) {
        if (e->Alive()) {
            auto plr = std::dynamic_pointer_cast<PlayerActor>(e);
            if (plr) plr->Send(msgId, data, len);
        }
    }
}

void Scene::AccumAndTick(int64_t nowMs, int32_t dtMs) {
    acc_ms_ += dtMs;
    int32_t interval = TickIntervalMs();
    while (acc_ms_ >= interval) {
        acc_ms_ -= interval;
        std::lock_guard<std::mutex> lk(mtx_);
        auto tickEntity = [nowMs, interval](Entity::Ptr e) {
            if (e->Alive()) e->OnTick(nowMs, interval);
        };
        for (auto& e : entity_mgr_->FindAllByType(EntityType::PLAYER))
            tickEntity(e);
        for (auto& e : entity_mgr_->FindAllByType(EntityType::MONSTER))
            tickEntity(e);
        for (auto& e : entity_mgr_->FindAllByType(EntityType::PET))
            tickEntity(e);
    }
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
