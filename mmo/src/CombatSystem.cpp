/// @file CombatSystem.cpp

#include "snc/mmorpg/legend/CombatSystem.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace snc {
namespace mmorpg {
namespace legend {

// ── 传奇经典伤害公式（与 combat.lua 公式一致）────────────────────
int32_t CombatSystem::PhysicalFormula(int32_t attack, int32_t defense, float ratio) const {
    if (attack <= 0) return 0;
    // 与 combat.lua: attack * ratio * 100 / (100 + defense) 对齐
    int32_t base = static_cast<int32_t>(attack * ratio * 100.0f / (100.0f + defense));
    return std::max(1, base);
}

int32_t CombatSystem::MagicFormula(int32_t magic, int32_t defense, float ratio) const {
    if (magic <= 0) return 0;
    // 与 combat.lua: magic * skill_level * 0.5 * ratio 对齐（defense 仅用于上限）
    int32_t base = static_cast<int32_t>(magic * ratio * 100.0f / (100.0f + defense));
    return std::max(1, base);
}

bool CombatSystem::RollCritical(const PlayerActor& atk, const PlayerActor& def) const {
    int atkRating = atk.TotalStats().str + atk.TotalStats().mag;
    int defRating = def.TotalStats().def;
    float critChance = std::max(0.0f, std::min(0.3f,
        static_cast<float>(atkRating - defRating) / 100.0f));
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng_) < critChance;
}

float CombatSystem::ClassMultiplier(ClassType cls, AttackType type) const {
    switch (cls) {
        case ClassType::WARRIOR:
            return (type == AttackType::PHYSICAL) ? 1.2f : 0.9f;
        case ClassType::MAGE:
            return (type == AttackType::MAGIC) ? 1.2f : 0.9f;
        case ClassType::TAOIST:
            return 1.1f; // 道士物理/法术均 +10%
        default:
            return 1.0f;
    }
}

// ── 玩家对玩家伤害 ───────────────────────────────────────────────

CombatResult CombatSystem::CalcPhysicalDamage(const PlayerActor& attacker,
                                               const PlayerActor& defender,
                                               float atkRatio) {
    CombatResult r{};
    float mult = ClassMultiplier(attacker.GetClassType(), AttackType::PHYSICAL);
    r.damage = PhysicalFormula(attacker.TotalStats().str, defender.TotalStats().def, atkRatio * mult);
    r.is_critical = RollCritical(attacker, defender);
    if (r.is_critical) r.damage = static_cast<int32_t>(r.damage * 1.5f);
    return r;
}

CombatResult CombatSystem::CalcMagicDamage(const PlayerActor& attacker,
                                            const PlayerActor& defender,
                                            float magicRatio) {
    CombatResult r{};
    float mult = ClassMultiplier(attacker.GetClassType(), AttackType::MAGIC);
    r.damage = MagicFormula(attacker.TotalStats().mag, defender.TotalStats().def, magicRatio * mult);
    r.is_critical = RollCritical(attacker, defender);
    if (r.is_critical) r.damage = static_cast<int32_t>(r.damage * 1.5f);
    return r;
}

CombatResult CombatSystem::CalcMonsterDamage(const MonsterInstance& monster,
                                              const PlayerActor& player) {
    CombatResult r{};
    r.damage = PhysicalFormula(monster.def.attack, player.TotalStats().def, 1.0f);
    return r;
}

// ── 玩家对怪物伤害 ────────────────────────────────────────────────

CombatResult CombatSystem::CalcPhysicalDamageToMonster(const PlayerActor& attacker,
                                                        const MonsterInstance& monster,
                                                        float atkRatio) {
    CombatResult r{};
    float mult = ClassMultiplier(attacker.GetClassType(), AttackType::PHYSICAL);
    r.damage = PhysicalFormula(attacker.TotalStats().str, monster.def.defense, atkRatio * mult);
    r.is_critical = false; // 怪物无暴击
    return r;
}

CombatResult CombatSystem::CalcMagicDamageToMonster(const PlayerActor& attacker,
                                                     const MonsterInstance& monster,
                                                     float magicRatio) {
    CombatResult r{};
    float mult = ClassMultiplier(attacker.GetClassType(), AttackType::MAGIC);
    r.damage = MagicFormula(attacker.TotalStats().mag, monster.def.defense, magicRatio * mult);
    r.is_critical = false;
    return r;
}

// ── 攻击处理 ─────────────────────────────────────────────────────

CombatResult CombatSystem::PlayerAttackPlayer(PlayerActor::Ptr attacker,
                                               PlayerActor::Ptr target,
                                               AttackType type) {
    if (!attacker || !target) return CombatResult{};
    // 安全区禁止 PK
    if (scene_mgr_ && IsSafeZone(target->Pos().mapId, *scene_mgr_)) {
        CombatResult r{};
        r.log = "cannot_pk_in_safe_zone";
        return r;
    }
    if (type == AttackType::MAGIC)
        return CalcMagicDamage(*attacker, *target, 1.0f);
    return CalcPhysicalDamage(*attacker, *target, 1.0f);
}

CombatResult CombatSystem::PlayerAttackMonster(PlayerActor::Ptr attacker,
                                                MonsterManager::MonsterPtr monster,
                                                AttackType type) {
    if (!attacker || !monster) return CombatResult{};
    CombatResult r = (type == AttackType::MAGIC)
        ? CalcMagicDamageToMonster(*attacker, *monster, 1.0f)
        : CalcPhysicalDamageToMonster(*attacker, *monster, 1.0f);
    // 对怪物造成伤害（简化：直接修改 current_hp）
    monster->current_hp = std::max(0, monster->current_hp - r.damage);
    // 怪物死亡 → 标记并触发死亡回调
    if (monster->current_hp <= 0 && monster->alive) {
        monster->alive = false;
        monster->state = MonsterState::DEAD;
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        monster->died_at_ts = nowMs;
        if (death_cb_) death_cb_(monster, attacker);
    }
    return r;
}

CombatResult CombatSystem::MonsterAttackPlayer(MonsterManager::MonsterPtr monster,
                                                PlayerActor::Ptr player) {
    if (!monster || !player) return CombatResult{};
    CombatResult r = CalcMonsterDamage(*monster, *player);
    player->TakeDamage(r.damage);
    return r;
}

// ── 技能使用 ─────────────────────────────────────────────────────

bool CombatSystem::CanUseSkill(const PlayerActor& caster, int32_t skillId) const {
    auto it = skills_.find(skillId);
    if (it == skills_.end()) return false;
    const auto& skill = it->second;
    if (caster.Level() < skill.min_level) return false;
    if (caster.CurrentMp() < skill.mp_cost) return false;
    return true;
}

CombatResult CombatSystem::UseSkill(PlayerActor::Ptr caster, int32_t skillId,
                                     PlayerActor::Ptr target) {
    if (!caster) return CombatResult{};
    const SkillDef* skill = GetSkill(skillId);
    if (!skill) return CombatResult{};
    CombatResult r{};

    // ── Lua 驱动（Lua 可用时委托给 combat.lua）───────────────────
#ifdef SNC_LUA_AVAILABLE
    if (lua_bridge_ && lua_bridge_->IsValid()) {
        lua_State* L = lua_bridge_->L();
        // 推 caster 到栈
        lua_bridge_->PushPlayer(caster.get());
        // 推 target（可为 nil）
        if (target) lua_bridge_->PushPlayer(target.get());
        else lua_pushnil(L);
        // 推 skill_id
        lua_pushinteger(L, skillId);
        // 调用 global use_skill(caster, target, skill_id) → 返回 {damage, is_critical, log}
        lua_getglobal(L, "use_skill");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 3, 3, 0) == 0) {
                r.damage      = (int32_t)lua_tointeger(L, -3);
                r.is_critical = lua_toboolean(L, -2) ? true : false;
                if (lua_isstring(L, -1)) r.log = lua_tostring(L, -1);
                else r.log = "lua_skill_used";
            }
        }
        lua_pop(L, 3); // clean stack
        return r;
    }
#endif
    // ── C++ 兜底路径 ─────────────────────────────────────────────
    if (skill->cls == ClassType::TAOIST && skillId == Skills::kSkillHeal) {
        if (!target) return r;
        int32_t amt = static_cast<int32_t>(caster->TotalStats().mag * 2.0f * skill->magic_ratio);
        r.damage = Heal(target, amt); // Heal returns actual healed amount
        r.log = "heal";
        return r;
    }
    // 物理/魔法技能
    if (!target) return r;
    if (skill->atk_ratio > 0) {
        r = CalcPhysicalDamage(*caster, *target, skill->atk_ratio);
    } else {
        r = CalcMagicDamage(*caster, *target, skill->magic_ratio);
    }
    r.log = skill->name;
    return r;
}

// ── 安全区判断 ────────────────────────────────────────────────────

bool CombatSystem::IsSafeZone(int32_t mapId, const SceneMgr& sceneMgr) {
    auto scene = sceneMgr.GetScene(mapId);
    return scene ? scene->IsSafeZone() : false;
}

bool CombatSystem::CanPK(const PlayerPos& a, const PlayerPos& b,
                          const SceneMgr& sceneMgr) {
    // 同地图且至少一方不在安全区才允许 PK
    if (a.mapId != b.mapId) return false;
    return !IsSafeZone(a.mapId, sceneMgr);
}

// ── 治疗/毒 ───────────────────────────────────────────────────────

int32_t CombatSystem::Heal(PlayerActor::Ptr target, int32_t amount) {
    if (!target) return 0;
    int32_t old = target->CurrentHp();
    target->SetHp(target->CurrentHp() + amount);
    return target->CurrentHp() - old;
}

int32_t CombatSystem::Poison(PlayerActor::Ptr target, int32_t tickDmg, int32_t tickCount) {
    (void)target; (void)tickDmg; (void)tickCount;
    // 占位：毒持续伤害由 Tick 主循环触发
    return 0;
}

// ── 技能表管理 ────────────────────────────────────────────────────

void CombatSystem::RegisterSkill(const SkillDef& skill) {
    skills_[skill.skill_id] = skill;
}

const SkillDef* CombatSystem::GetSkill(int32_t skillId) const {
    auto it = skills_.find(skillId);
    return (it != skills_.end()) ? &it->second : nullptr;
}

std::vector<SkillDef> CombatSystem::GetClassSkills(ClassType cls) const {
    std::vector<SkillDef> result;
    for (auto& [id, skill] : skills_)
        if (skill.cls == cls) result.push_back(skill);
    return result;
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
