// SPDX-License-Identifier: MIT
#pragma once

/// @file PetInstance.h
/// @brief 宠物实体 — 道士召唤神兽等 Pet 系统
///
/// Pet 继承 Entity，拥有独立的 AI 状态机（复用 MonsterManager Lua 决策）。
/// 每帧 Tick 时朝主人位置移动（跟随逻辑）。

#include <cstdint>
#include <string>
#include <memory>
#include <chrono>

#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/PlayerActor.h"
#include "snc/mmorpg/legend/MonsterManager.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── Pet 状态 ────────────────────────────────────────────────────────
enum class PetState : int8_t {
    FOLLOW = 0,   // 跟随主人
    IDLE   = 1,   // 静止等待
    ATTACK = 2,   // 攻击敌人
};

constexpr const char* PetStateName(PetState s) {
    switch (s) {
        case PetState::FOLLOW: return "FOLLOW";
        case PetState::IDLE:   return "IDLE";
        case PetState::ATTACK: return "ATTACK";
        default:               return "UNKNOWN";
    }
}

// ── Pet 定义 ────────────────────────────────────────────────────────
struct PetDef {
    int32_t pet_id       = 0;   // 宠物类型 ID（映射到 MonsterDef）
    int32_t hp           = 100;
    int32_t attack       = 10;
    int32_t defense      = 5;
    int32_t speed        = 3;
    int32_t atkRange     = 1;
    int64_t lifetimeMs   = 60000; // 持续时间（毫秒），0 = 永久
    std::string name;
};

// ── Pet 实例 ────────────────────────────────────────────────────────
class PetInstance : public Entity {
public:
    using Ptr = std::shared_ptr<PetInstance>;

    PetInstance(int32_t id, const PetDef& def, PlayerActor::Ptr owner)
        : Entity()
        , def_(def)
        , owner_(std::move(owner))
        , current_hp_(def.hp)
        , state_(PetState::FOLLOW)
        , summoned_at_(std::chrono::steady_clock::now()) {
        SetEntityId(id);
        pos_.mapId = owner_->Pos().mapId;
        pos_.x     = owner_->Pos().x;
        pos_.y     = owner_->Pos().y;
        spawn_pos_ = pos_;
    }

    ~PetInstance() override = default;

    // ── Entity 接口 ─────────────────────────────────────────────
    EntityType Type() const override { return EntityType::PET; }
    void OnTick(int64_t nowMs, int32_t dtMs) override {
        Tick(dtMs);
    }

    // ── 访问器 ──────────────────────────────────────────────────
    int32_t       PetId()       const { return EntityId(); }
    int32_t       OwnerId()     const { return owner_ ? owner_->PlayerId() : 0; }
    PlayerActor::Ptr GetOwner() const { return owner_; }
    const PetDef& Def()         const { return def_; }
    int32_t       CurrentHp()   const { return current_hp_; }
    int32_t       MaxHp()       const { return def_.hp; }
    PetState      State()       const { return state_; }
    bool          IsAlive()     const { return current_hp_ > 0; }

    // ── 移动 ────────────────────────────────────────────────────
    bool MoveTo(int32_t mapId, int32_t x, int32_t y) {
        if (mapId <= 0 || x < 0 || x >= kMapMaxCoord || y < 0 || y >= kMapMaxCoord) return false;
        pos_.mapId = mapId;
        pos_.x     = x;
        pos_.y     = y;
        return true;
    }
    const PlayerPos& Pos()     const { return pos_; }
    const PlayerPos& SpawnPos()const { return spawn_pos_; }

    // ── 伤害 ────────────────────────────────────────────────────
    void TakeDamage(int32_t dmg) {
        current_hp_ = std::max(0, current_hp_ - dmg);
        if (current_hp_ <= 0) state_ = PetState::IDLE;
    }
    void SetHp(int32_t hp) { current_hp_ = std::min(def_.hp, std::max(0, hp)); }

    // ── 状态设置 ─────────────────────────────────────────────────
    void SetState(PetState s) { state_ = s; }

    // ── 调试序列化 ──────────────────────────────────────────────
    std::string DumpState() const;

private:
    void Tick(long long elapsedMs);
    bool IsExpired() const;

    PetDef     def_;
    PlayerActor::Ptr owner_;  // 主人（弱引用更安全，但 shared_ptr 便于持有）
    PlayerPos  pos_;
    PlayerPos  spawn_pos_;
    int32_t    current_hp_;
    PetState   state_;
    std::chrono::steady_clock::time_point summoned_at_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
