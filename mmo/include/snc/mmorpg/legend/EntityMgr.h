#pragma once

/// @file EntityMgr.h
/// @brief 传奇版实体管理器 — 全局实体索引，按 ID 和类型快速查询

#include "snc/mmorpg/legend/Entity.h"
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

namespace snc {
namespace mmorpg {
namespace legend {

// ── EntityMgr ────────────────────────────────────────────────
class EntityMgr {
public:
    using Ptr = std::shared_ptr<EntityMgr>;
    using FindNearbyCb = std::function<void(Entity::Ptr)>;

    EntityMgr() = default;
    ~EntityMgr() = default;

    // 注册/注销（由 Scene 或外部调用）
    void Register(Entity::Ptr e);
    void Unregister(int32_t entityId);

    // 按 ID 查询
    Entity::Ptr   Find(int32_t entityId) const;
    Entity::Ptr   FindByType(int32_t entityId, EntityType t) const;

    // 类型索引查询（高效遍历同类型全部实体）
    std::vector<Entity::Ptr> FindAllByType(EntityType t) const;
    size_t                   CountByType(EntityType t) const;

    // 区域内就近查询（广播/攻击范围用）
    void ForEachNearby(int32_t x, int32_t y, int32_t range,
                       EntityType t, const FindNearbyCb& cb) const;

    // 整体计数
    size_t TotalCount() const;

private:
    mutable std::mutex          mtx_;
    std::unordered_map<int32_t, Entity::Ptr> by_id_;
    std::unordered_map<EntityType,
               std::unordered_map<int32_t, Entity::Ptr>> by_type_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
