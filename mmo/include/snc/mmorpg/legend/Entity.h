#pragma once

/// @file Entity.h
/// @brief 传奇版实体基�?�?所有场景内实体的统一基类
///
/// 派生类：PlayerActor / MonsterInstance / NpcInstance / PetInstance

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "snc/mmorpg/PlayerPos.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── 玩家位置（复用框架层定义）───────────────────────────────────
using PlayerPos = snc::mmorpg::PlayerPos;

// ── 实体类型枚举 ─────────────────────────────────────────────
enum class EntityType : int8_t {
    PLAYER = 0,
    MONSTER = 1,
    NPC   = 2,
    PET   = 3,
};

// ── Entity 基类 ──────────────────────────────────────────────
class Entity {
public:
    using Ptr     = std::shared_ptr<Entity>;
    using WeakPtr = std::weak_ptr<Entity>;

    virtual ~Entity() = default;

    // 类型查询（多态）
    virtual EntityType Type() const = 0;

    // 标识
    int32_t  EntityId()  const { return entity_id_; }
    int32_t  SceneId()   const { return scene_id_; }
    void     SetSceneId(int32_t id) { scene_id_ = id; }
    void     SetEntityId(int32_t id) { entity_id_ = id; }

    // 位置
    PlayerPos& Pos()        { return pos_; }
    const PlayerPos& Pos()  const { return pos_; }
    void SetPos(int32_t mapId, int32_t x, int32_t y) {
        pos_.mapId = mapId; pos_.x = x; pos_.y = y;
    }

    // 存活状�?
    bool     Alive()      const { return alive_; }
    void     SetAlive(bool a) { alive_ = a; }

    // 每帧逻辑（各派生类实现具体行为）
    virtual void OnTick(int64_t nowMs, int32_t dtMs) {}

protected:
    int32_t    entity_id_ = 0;
    int32_t    scene_id_  = 0;
    PlayerPos  pos_;
    bool       alive_     = true;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
