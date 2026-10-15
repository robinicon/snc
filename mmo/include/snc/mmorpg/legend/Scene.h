#pragma once

/// @file Scene.h
/// @brief 传奇版场�?�?实体容器，支持高/低两种同步频�?
///
/// 场景分为两类�?
///   HIGH (安全�?主城)�?0fps，玩家密集交互，高频同步
///   LOW  (副本/野外)  �?0fps，事件稀疏，低频同步以节省资�?

#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/EntityMgr.h"
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace snc {
namespace mmorpg {
namespace legend {

// ── 场景同步模式 ─────────────────────────────────────────────
enum class SyncMode : int8_t {
    HIGH = 0,   // 安全�?主城�?0fps�?3ms/tick�?
    LOW  = 1,   // 副本/野外�? 10fps�?00ms/tick�?
};

// ── 场景配置 ─────────────────────────────────────────────────
struct SceneConfig {
    int32_t  scene_id   = 0;
    std::string name;
    int32_t  width      = 0;
    int32_t  height     = 0;
    bool     is_safe_zone = false;
    bool     is_dungeon   = false;
    int32_t  min_level  = 1;
    SyncMode sync_mode  = SyncMode::HIGH;
};

// ── Scene ────────────────────────────────────────────────────
class Scene : public std::enable_shared_from_this<Scene> {
public:
    using Ptr = std::shared_ptr<Scene>;

    explicit Scene(const SceneConfig& cfg);
    virtual ~Scene() = default;

    // 配置访问
    int32_t             SceneId()    const { return config_.scene_id; }
    const SceneConfig&  Cfg()        const { return config_; }
    SyncMode            SyncMode()   const { return config_.sync_mode; }
    int32_t             TickIntervalMs() const {
        return (config_.sync_mode == SyncMode::HIGH) ? 33 : 100;
    }
    bool                IsSafeZone() const { return config_.is_safe_zone; }
    bool                IsDungeon()  const { return config_.is_dungeon; }

    // 实体管理（委托给 entity_mgr_�?
    void    AddEntity(Entity::Ptr e);
    void    RemoveEntity(int32_t entityId);
    Entity::Ptr   FindEntity(int32_t entityId) const;
    size_t  EntityCount() const;

    // 区域查询
    std::vector<Entity::Ptr> FindNearby(int32_t x, int32_t y,
                                         int32_t range) const;

    // 广播（仅广播本场景内在线实体�?
    void Broadcast(int32_t msgId, const char* data, size_t len);

    // 主循环驱�?�?内部做时间累加，达到 interval 时推进一�?
    // nowMs  当前时间戳（毫秒，steady_clock�?
    // dtMs   自上次调用以来的实际帧间隔（用于日志/调试�?
    void AccumAndTick(int64_t nowMs, int32_t dtMs);

    // 获取实体管理器（�?GameService 全局索引用）
    EntityMgr::Ptr GetEntityMgr() const { return entity_mgr_; }

private:
    SceneConfig         config_;
    EntityMgr::Ptr      entity_mgr_;
    int64_t             acc_ms_ = 0;   // 未处理的时间累加�?
    mutable std::mutex  mtx_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
