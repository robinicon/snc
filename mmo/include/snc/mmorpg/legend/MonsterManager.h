// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file MonsterManager.h
/// @brief 怪物管理系统 — AI 状态机、刷新、掉落触发
///
/// AI 状态机：
///   IDLE → PATROL → CHASE → ATTACK → RETURN → DEAD
///
/// 职责：
///   - 管理地图内怪物实例
///   - 定时刷新（死亡后重生）
///   - AI Tick（状态转换 + 移动）
///   - 触发掉落

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include <chrono>
#include <functional>

#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace mmorpg {
namespace legend {

class LuaBridge;  // 前向声明（避免循环依赖）

// ── 怪物 AI 状态 ──────────────────────────────────────────────────
enum class MonsterState : int8_t {
    IDLE    = 0,   // 静止等待
    PATROL  = 1,   // 巡逻
    CHASE   = 2,   // 追击玩家
    ATTACK  = 3,   // 攻击中
    RETURN  = 4,   // 返回出生点
    DEAD    = 5,   // 死亡（等待重生）
};

constexpr const char* MonsterStateName(MonsterState s) {
    switch (s) {
        case MonsterState::IDLE:   return "IDLE";
        case MonsterState::PATROL: return "PATROL";
        case MonsterState::CHASE:  return "CHASE";
        case MonsterState::ATTACK: return "ATTACK";
        case MonsterState::RETURN: return "RETURN";
        case MonsterState::DEAD:   return "DEAD";
        default:                   return "UNKNOWN";
    }
}

// ── 怪物定义 ─────────────────────────────────────────────────────
struct MonsterDef {
    int32_t  monster_id = 0;
    std::string name;
    int32_t  hp       = 50;
    int32_t  attack   = 5;
    int32_t  defense  = 2;
    int32_t  exp      = 10;   // 击杀获得经验
    int32_t  speed    = 2;    // 移动速度
    int32_t  atkRange = 1;    // 攻击范围（格子）
    int32_t  patrolRange = 5; // 巡逻范围（格子）
    int64_t  respawnMs  = 30000; // 重生时间（毫秒）
    // 掉落表（MonsterManager 单独管理）
    int32_t  drop_table_id = 0;
};

// ── 怪物实例 ─────────────────────────────────────────────────────
struct MonsterInstance {
    MonsterDef def;
    PlayerPos  pos;              // 当前位置
    PlayerPos  spawn_pos;        // 出生点（用于 RETURN）
    PlayerPos  target_pos;       // 目标玩家位置（CHASE/ATTACK 时使用）
    int32_t    current_hp = 0;
    MonsterState state = MonsterState::IDLE;
    int32_t    target_player_id = 0; // 当前攻击目标
    int64_t    last_atk_ts = 0;       // 上次攻击时间戳
    int64_t    died_at_ts = 0;        // 死亡时间戳（用于重生计时）
    bool       alive = true;
};

// ══════════════════════════════════════════════════════════════════
// MonsterManager
// ══════════════════════════════════════════════════════════════════
class MonsterManager {
public:
    using Ptr       = std::shared_ptr<MonsterManager>;
    using MonsterPtr = std::shared_ptr<MonsterInstance>;
    /// 玩家被怪物攻击时的回调
    using HitCallback = std::function<void(PlayerActor::Ptr attacker,
                                            PlayerActor::Ptr victim,
                                            int32_t damage)>;
    /// 怪物死亡回调（触发掉落）
    using DeathCallback = std::function<void(MonsterPtr monster,
                                              PlayerActor::Ptr killer)>;

    MonsterManager() = default;
    ~MonsterManager() = default;

    /// 注入 LuaBridge（用于 Lua AI 决策，无 Lua 时为 no-op）
    void SetLuaBridge(LuaBridge* bridge) { lua_bridge_ = bridge; }

    // ── 怪物定义注册 ────────────────────────────────────────────
    void RegisterMonster(const MonsterDef& def);
    const MonsterDef* GetMonsterDef(int32_t monsterId) const;

    // ── 地图怪物管理 ────────────────────────────────────────────
    /// 在地图上生成怪物（返回生成的怪物实例）
    MonsterPtr SpawnMonster(int32_t mapId, int32_t monsterId, int32_t x, int32_t y);
    /// 移除并销毁怪物
    void DestroyMonster(int32_t mapId, int32_t monsterId);
    /// 批量生成（用于地图初始化）
    void InitMapMonsters(int32_t mapId, const std::vector<std::pair<int32_t, std::pair<int32_t,int32_t>>>& spawns);

    // ── AI Tick（每帧调用）─────────────────────────────────────
    /// 更新所有地图的怪物 AI，返回本次处理过的怪物数量
    int TickAll(long long elapsedMs, HitCallback hitCb, DeathCallback deathCb);
    /// 更新单个地图的怪物
    int TickMap(int32_t mapId, long long elapsedMs, HitCallback hitCb, DeathCallback deathCb);

    // ── 查询 ────────────────────────────────────────────────────
    MonsterPtr GetMonster(int32_t mapId, int32_t monsterId);
    std::vector<MonsterPtr> GetMonstersInRadius(int32_t mapId, int32_t x, int32_t y, int32_t range);
    size_t MonsterCount(int32_t mapId) const;

private:
    // 状态转换辅助
    void UpdateState(MonsterInstance& m, long long elapsedMs, int64_t nowMs);
    void MoveToward(MonsterInstance& m, int32_t tx, int32_t ty, long long elapsedMs);
    bool IsInAttackRange(const MonsterInstance& m, const PlayerPos& targetPos) const;
    void RespawnIfNeeded(MonsterInstance& m, int32_t mapId);

    mutable std::mutex mtx_;
    // mapId -> (monsterId -> MonsterPtr)
    std::unordered_map<int32_t, std::unordered_map<int32_t, MonsterPtr>> map_monsters_;
    std::unordered_map<int32_t, MonsterDef> monster_defs_;
    LuaBridge* lua_bridge_ = nullptr;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
