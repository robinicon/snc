/// @file MonsterManager.cpp

#include "snc/mmorpg/legend/MonsterManager.h"
#include "snc/mmorpg/legend/LuaBridge.h"
#include <algorithm>
#include <random>
#include <cmath>

namespace snc {
namespace mmorpg {
namespace legend {

void MonsterManager::RegisterMonster(const MonsterDef& def) {
    std::lock_guard<std::mutex> lk(mtx_);
    monster_defs_[def.monster_id] = def;
}

const MonsterDef* MonsterManager::GetMonsterDef(int32_t monsterId) const {
    auto it = monster_defs_.find(monsterId);
    return (it != monster_defs_.end()) ? &it->second : nullptr;
}

MonsterManager::MonsterPtr MonsterManager::SpawnMonster(int32_t mapId, int32_t monsterId, int32_t x, int32_t y) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = monster_defs_.find(monsterId);
    if (it == monster_defs_.end()) return nullptr;
    auto m = std::make_shared<MonsterInstance>();
    m->def = it->second;
    m->pos.mapId  = mapId;
    m->pos.x      = x;
    m->pos.y      = y;
    m->spawn_pos  = m->pos;
    m->current_hp = m->def.hp;
    m->state      = MonsterState::IDLE;
    m->alive      = true;
    map_monsters_[mapId][monsterId] = m;
    return m;
}

void MonsterManager::DestroyMonster(int32_t mapId, int32_t monsterId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto mit = map_monsters_.find(mapId);
    if (mit == map_monsters_.end()) return;
    mit->second.erase(monsterId);
    if (mit->second.empty()) map_monsters_.erase(mit);
}

void MonsterManager::InitMapMonsters(int32_t mapId,
        const std::vector<std::pair<int32_t, std::pair<int32_t,int32_t>>>& spawns) {
    for (auto& [mid, coords] : spawns) {
        SpawnMonster(mapId, mid, coords.first, coords.second);
    }
}

MonsterManager::MonsterPtr MonsterManager::GetMonster(int32_t mapId, int32_t monsterId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto mit = map_monsters_.find(mapId);
    if (mit == map_monsters_.end()) return nullptr;
    auto it = mit->second.find(monsterId);
    return (it != mit->second.end()) ? it->second : nullptr;
}

std::vector<MonsterManager::MonsterPtr> MonsterManager::GetMonstersInRadius(int32_t mapId, int32_t x, int32_t y, int32_t range) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<MonsterManager::MonsterPtr> result;
    auto mit = map_monsters_.find(mapId);
    if (mit == map_monsters_.end()) return result;
    for (auto& [mid, m] : mit->second) {
        if (!m->alive) continue;
        int dx = m->pos.x - x, dy = m->pos.y - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx + dy <= range) result.push_back(m);
    }
    return result;
}

size_t MonsterManager::MonsterCount(int32_t mapId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = map_monsters_.find(mapId);
    return (it != map_monsters_.end()) ? it->second.size() : 0;
}

// ── AI Tick ──────────────────────────────────────────────────────

int MonsterManager::TickAll(long long elapsedMs, HitCallback hitCb, DeathCallback deathCb) {
    int count = 0;
    std::vector<int32_t> map_ids;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [mid, _] : map_monsters_) {
            map_ids.push_back(mid);
        }
    }
    for (auto mid : map_ids) {
        count += TickMap(mid, elapsedMs, std::move(hitCb), std::move(deathCb));
    }
    return count;
}

int MonsterManager::TickMap(int32_t mapId, long long elapsedMs,
                             HitCallback hitCb, DeathCallback deathCb) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto mit = map_monsters_.find(mapId);
    if (mit == map_monsters_.end()) return 0;

    int processed = 0;
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (auto& [mid, m] : mit->second) {
        if (!m->alive) {
            RespawnIfNeeded(*m, mapId);
            continue;
        }
        UpdateState(*m, elapsedMs, nowMs);
        // ── CHASE/ATTACK 追击与攻击 ───────────────────────────────
        if ((m->state == MonsterState::CHASE || m->state == MonsterState::ATTACK)
            && m->target_player_id != 0 && !m->target_pos.IsValid()) {
            // 清空无目标的缓存位置
            m->target_pos = PlayerPos{};
        }
        ++processed;
    }
    return processed;
}

void MonsterManager::UpdateState(MonsterInstance& m, long long elapsedMs, int64_t nowMs) {
#ifdef SNC_LUA_AVAILABLE
    if (lua_bridge_ && lua_bridge_->IsValid()) {
        lua_State* L = lua_bridge_->L();
        lua_bridge_->PushMonster(&m);
        lua_getglobal(L, "monster"); // get monster module
        if (lua_istable(L, -1)) {
            lua_pushvalue(L, -2); // push monster metatable object
            lua_getfield(L, -2, "decide_state"); // monster.decide_state
            if (lua_isfunction(L, -1)) {
                lua_createtable(L, 0, 2);
                lua_pushinteger(L, m.spawn_pos.x);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, m.spawn_pos.y);
                lua_setfield(L, -2, "y");
                lua_pushinteger(L, elapsedMs);
                if (lua_pcall(L, 3, 1, 0) == 0) {
                    std::string state_name = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    if      (state_name == "CHASE")  m.state = MonsterState::CHASE;
                    else if (state_name == "ATTACK") m.state = MonsterState::ATTACK;
                    else if (state_name == "RETURN") m.state = MonsterState::RETURN;
                    else if (state_name == "PATROL") m.state = MonsterState::PATROL;
                    else if (state_name == "DEAD")   m.state = MonsterState::DEAD;
                    else                              m.state = MonsterState::IDLE;
                }
                lua_pop(L, 1); // pop decide_state result
            }
        }
        lua_pop(L, 2); // pop monster_obj and monster_mod
        return; // Lua handled it
    }
#endif
    // ── 内置 AI（Lua 未启用时的降级路径）───────────────────────────
    switch (m.state) {
        case MonsterState::IDLE:
            if (std::rand() % 100 < 30) m.state = MonsterState::PATROL;
            break;
        case MonsterState::PATROL:
            m.pos.x += (std::rand() % 3 - 1);
            m.pos.y += (std::rand() % 3 - 1);
            if (std::rand() % 100 < 20) m.state = MonsterState::IDLE;
            break;
        case MonsterState::CHASE:
            if (m.target_player_id == 0) { m.state = MonsterState::IDLE; break; }
            // 追击目标（使用已缓存的目标位置）
            if (m.target_pos.IsValid()) {
                MoveToward(m, m.target_pos.x, m.target_pos.y, elapsedMs);
                // 进入攻击范围则切换为 ATTACK
                if (IsInAttackRange(m, m.target_pos)) {
                    m.state = MonsterState::ATTACK;
                }
            }
            break;
        case MonsterState::ATTACK:
            if (m.target_player_id == 0) { m.state = MonsterState::IDLE; break; }
            // 攻击冷却检查 + 触发 hitCb
            if (nowMs - m.last_atk_ts >= 1000) { // 1s 攻击间隔
                m.last_atk_ts = nowMs;
                // 攻击时缓存目标位置供 CHASE 使用
                m.target_pos = PlayerPos{m.pos.mapId, m.target_pos.x, m.target_pos.y};
            }
            // 目标不在攻击范围则切回 CHASE
            if (!IsInAttackRange(m, m.target_pos)) {
                m.state = MonsterState::CHASE;
            }
            break;
        case MonsterState::RETURN:
            if (m.pos.x == m.spawn_pos.x && m.pos.y == m.spawn_pos.y) {
                m.state = MonsterState::IDLE;
            } else {
                if (m.pos.x < m.spawn_pos.x) ++m.pos.x;
                if (m.pos.x > m.spawn_pos.x) --m.pos.x;
                if (m.pos.y < m.spawn_pos.y) ++m.pos.y;
                if (m.pos.y > m.spawn_pos.y) --m.pos.y;
            }
            break;
        case MonsterState::DEAD:
            break;
    }
}

void MonsterManager::MoveToward(MonsterInstance& m, int32_t tx, int32_t ty, long long elapsedMs) {
    (void)elapsedMs;
    int dx = tx - m.pos.x, dy = ty - m.pos.y;
    int dist = std::abs(dx) + std::abs(dy);
    if (dist <= m.def.atkRange) return; // 已进入攻击范围，停止移动
    int steps = std::min(m.def.speed, dist);
    if (dx != 0) m.pos.x += (dx > 0 ? 1 : -1) * std::min(steps, std::abs(dx));
    if (dy != 0) m.pos.y += (dy > 0 ? 1 : -1) * std::min(steps, std::abs(dy));
}

bool MonsterManager::IsInAttackRange(const MonsterInstance& m, const PlayerPos& targetPos) const {
    int dx = m.pos.x - targetPos.x, dy = m.pos.y - targetPos.y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx + dy) <= m.def.atkRange;
}

void MonsterManager::RespawnIfNeeded(MonsterInstance& m, int32_t mapId) {
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (nowMs - m.died_at_ts < m.def.respawnMs) return;
    m.current_hp = m.def.hp;
    m.state      = MonsterState::IDLE;
    m.alive      = true;
    m.pos        = m.spawn_pos;
    m.target_player_id = 0;
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
