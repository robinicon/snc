/// @file SceneMgr.cpp
/// @brief 场景管理器实现

#include "snc/mmorpg/legend/SceneMgr.h"

namespace snc {
namespace mmorpg {
namespace legend {

Scene::Ptr SceneMgr::CreateScene(const SceneConfig& cfg) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto scene = std::make_shared<Scene>(cfg);
    scenes_[cfg.scene_id] = scene;
    return scene;
}

void SceneMgr::DestroyScene(int32_t sceneId) {
    std::lock_guard<std::mutex> lk(mtx_);
    scenes_.erase(sceneId);
}

Scene::Ptr SceneMgr::GetScene(int32_t sceneId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = scenes_.find(sceneId);
    return (it != scenes_.end()) ? it->second : nullptr;
}

bool SceneMgr::HasScene(int32_t sceneId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return scenes_.find(sceneId) != scenes_.end();
}

std::vector<Scene::Ptr> SceneMgr::AllScenes() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<Scene::Ptr> result;
    result.reserve(scenes_.size());
    for (auto& [id, s] : scenes_) result.push_back(s);
    return result;
}

size_t SceneMgr::SceneCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return scenes_.size();
}

void SceneMgr::TickAll(int64_t nowMs, int32_t dtMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [id, scene] : scenes_) {
        scene->AccumAndTick(nowMs, dtMs);
    }
}

size_t SceneMgr::TotalEntityCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t total = 0;
    for (auto& [id, s] : scenes_) total += s->EntityCount();
    return total;
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
