// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file SceneMgr.h
/// @brief 传奇版场景管理器 �?替代�?MapManager，统一场景生命周期

#include "snc/mmorpg/legend/Scene.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace snc {
namespace mmorpg {
namespace legend {

// ── 传奇经典地图 ID（从�?Maps 命名空间迁移）──────────────────
namespace Maps {
    // 安全�?
    constexpr int32_t kMapBegin      = 1;  // 新手�?
    constexpr int32_t kMapBeij       = 2;  // 比奇�?
    constexpr int32_t kMapAlmaty     = 3;  // 盟重�?
    constexpr int32_t kMapShabake    = 4;  // 沙巴�?
    constexpr int32_t kMapQuanzhou   = 5;  // 泉州（传送点�?
    // 副本/野外
    constexpr int32_t kMapCowMac      = 11; // 牛魔寺庙
    constexpr int32_t kMapWolfMountain = 12; // 沃玛森林
    constexpr int32_t kMapThrone      = 13; // 祖玛寺庙
    constexpr int32_t kMapIronRoom    = 14; // 石墓�?
    constexpr int32_t kMapDragonNest  = 15; // 龙墓
}

// ── 地图尺寸常量 ─────────────────────────────────────────────
constexpr int32_t kMapMaxCoord = 200;  // 地图坐标最大值（坐标范围 0..199�?

// ── SceneMgr ─────────────────────────────────────────────────
class SceneMgr {
public:
    using Ptr = std::shared_ptr<SceneMgr>;

    SceneMgr() = default;
    ~SceneMgr() = default;

    // 场景 CRUD
    Scene::Ptr CreateScene(const SceneConfig& cfg);
    void       DestroyScene(int32_t sceneId);
    Scene::Ptr GetScene(int32_t sceneId) const;
    bool       HasScene(int32_t sceneId) const;

    // 遍历
    std::vector<Scene::Ptr> AllScenes() const;
    size_t                  SceneCount() const;

    // 主循环驱�?�?所有场景按各自 sync_mode 独立推进
    void TickAll(int64_t nowMs, int32_t dtMs);

    // 兼容接口（返回场景数量，供统计用�?
    size_t TotalEntityCount() const;

private:
    mutable std::mutex              mtx_;
    std::unordered_map<int32_t, Scene::Ptr> scenes_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
