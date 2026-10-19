// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file CombatSystem.h
/// @brief 战斗计算系统 �?伤害公式、暴击、技能、安全区 PK 判断
///
/// 伤害公式（传奇经典）�?
///   physical_damage = max(1, attack × 100 / (100 + defense))
///   magic_damage    = magic_attack × skill_level × 0.5（简化）
///   critical_chance = (attack_rating - target_defense) / 100  (clamp 0~0.3)
///   critical_mult   = 1.5×
///
/// 职业加成�?
///   战士：物理攻�?+20%
///   法师：法术攻�?+20%，物理攻�?-10%
///   道士：法�?物理�?+10%

#include <cstdint>
#include <random>
#include <algorithm>
#include <memory>

#include "snc/mmorpg/legend/PlayerActor.h"
#include "snc/mmorpg/legend/MonsterManager.h"
#include "snc/mmorpg/legend/SceneMgr.h"
#include "snc/mmorpg/legend/LuaBridge.h"

namespace snc {
namespace mmorpg {
namespace legend {

using DeathCallback = typename MonsterManager::DeathCallback;

// ── 攻击类型 ─────────────────────────────────────────────────────
enum class AttackType : int8_t {
    PHYSICAL = 0,  // 物理攻击
    MAGIC    = 1,  // 魔法攻击
    DOG_ATTACK = 2, // 狗狗攻击（道士召唤物�?
};

// ── 战斗结果 ─────────────────────────────────────────────────────
struct CombatResult {
    int32_t damage     = 0;
    bool    is_critical = false;
    bool    blocked     = false;
    std::string log;
};

// ── 技能定�?─────────────────────────────────────────────────────
struct SkillDef {
    int32_t  skill_id    = 0;
    std::string name;
    ClassType cls        = ClassType::WARRIOR; // 所属职�?
    int32_t  min_level   = 1;  // 学习最低等�?
    int32_t  mp_cost     = 0;  // 耗蓝
    int32_t  cooldown_ms = 1000; // 冷却时间
    int32_t  range       = 1;  // 施法范围（格子）
    // 伤害参数（物理技能）
    float    atk_ratio   = 1.0f; // 攻击倍数
    // 伤害参数（魔法技能）
    float    magic_ratio = 1.0f;
    bool     is_aoe      = false; // 是否范围攻击
};

// ── 传奇经典技能预定义 ────────────────────────────────────────────
namespace Skills {
    // 战士技�?
    constexpr int32_t kSkillCrazyKick   = 101; // 野蛮冲撞
    constexpr int32_t kSkillWhirlwind   = 102; // 半月弯刀
    constexpr int32_t kSkillFireSlash   = 103; // 烈火剑法
    // 法师技�?
    constexpr int32_t kSkillThunderbolt = 201; // 雷电�?
    constexpr int32_t kSkillFireWall    = 202; // 火墙
    constexpr int32_t kSkillIceNova     = 203; // 冰咆�?
    // 道士技�?
    constexpr int32_t kSkillHeal        = 301; // 治愈�?
    constexpr int32_t kSkillPoison      = 302; // 施毒�?
    constexpr int32_t kSkillSummonDog   = 303; // 召唤神兽
}

// ══════════════════════════════════════════════════════════════════
// CombatSystem
// ══════════════════════════════════════════════════════════════════
class CombatSystem {
public:
    using Ptr = std::shared_ptr<CombatSystem>;

    CombatSystem(SceneMgr::Ptr sceneMgr = nullptr) : scene_mgr_(std::move(sceneMgr)), rng_(std::random_device{}()) {}
    ~CombatSystem() = default;

    // ── 伤害计算 ────────────────────────────────────────────────
    /// 计算玩家对目标的物理伤害
    CombatResult CalcPhysicalDamage(const PlayerActor& attacker,
                                     const PlayerActor& defender,
                                     float atkRatio = 1.0f);
    /// 计算玩家对目标的魔法伤害
    CombatResult CalcMagicDamage(const PlayerActor& attacker,
                                  const PlayerActor& defender,
                                  float magicRatio = 1.0f);
    /// 计算玩家对怪物的物理伤�?
    CombatResult CalcPhysicalDamageToMonster(const PlayerActor& attacker,
                                              const MonsterInstance& monster,
                                              float atkRatio = 1.0f);
    /// 计算玩家对怪物的魔法伤�?
    CombatResult CalcMagicDamageToMonster(const PlayerActor& attacker,
                                           const MonsterInstance& monster,
                                           float magicRatio = 1.0f);
    /// 计算怪物对玩家的伤害
    CombatResult CalcMonsterDamage(const MonsterInstance& monster,
                                    const PlayerActor& player);

    // ── 攻击处理 ────────────────────────────────────────────────
    /// 玩家攻击玩家（含 PK 规则判断�?
    CombatResult PlayerAttackPlayer(PlayerActor::Ptr attacker,
                                     PlayerActor::Ptr target,
                                     AttackType type = AttackType::PHYSICAL);
    /// 玩家攻击怪物
    CombatResult PlayerAttackMonster(PlayerActor::Ptr attacker,
                                      MonsterManager::MonsterPtr monster,
                                      AttackType type = AttackType::PHYSICAL);
    /// 怪物攻击玩家
    CombatResult MonsterAttackPlayer(MonsterManager::MonsterPtr monster,
                                      PlayerActor::Ptr player);

    // ── 技能使�?────────────────────────────────────────────────
    /// 使用技能，返回伤害结果（空=失败�?
    CombatResult UseSkill(PlayerActor::Ptr caster, int32_t skillId,
                           PlayerActor::Ptr target = nullptr);
    bool CanUseSkill(const PlayerActor& caster, int32_t skillId) const;

    // ── 死亡回调注册 ────────────────────────────────────────────
    void SetDeathCallback(DeathCallback cb) { death_cb_ = std::move(cb); }
    void SetLuaBridge(LuaBridge* lb) { lua_bridge_ = lb; }

    // ── 安全区判�?──────────────────────────────────────────────
    /// 目标地图是否为安全区
    static bool IsSafeZone(int32_t mapId, const SceneMgr& sceneMgr);
    /// 两个位置之间是否可以 PK
    static bool CanPK(const PlayerPos& a, const PlayerPos& b,
                      const SceneMgr& sceneMgr);

    // ── 治疗 ────────────────────────────────────────────────────
    int32_t Heal(PlayerActor::Ptr target, int32_t amount);
    int32_t Poison(PlayerActor::Ptr target, int32_t tickDmg, int32_t tickCount);

    // ── 技能表管理 ──────────────────────────────────────────────
    void RegisterSkill(const SkillDef& skill);
    const SkillDef* GetSkill(int32_t skillId) const;
    std::vector<SkillDef> GetClassSkills(ClassType cls) const;

private:
    // 核心公式
    int32_t PhysicalFormula(int32_t attack, int32_t defense, float ratio) const;
    int32_t MagicFormula(int32_t magic, int32_t defense, float ratio) const;
    bool    RollCritical(const PlayerActor& atk, const PlayerActor& def) const;
    float   ClassMultiplier(ClassType cls, AttackType type) const;

    std::unordered_map<int32_t, SkillDef> skills_;
    SceneMgr::Ptr scene_mgr_;
    mutable std::mt19937 rng_;
    LuaBridge* lua_bridge_ = nullptr;
    DeathCallback death_cb_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
