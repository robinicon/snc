// SPDX-License-Identifier: MIT
/// @file LuaBridge.cpp
/// @brief LuaBridge 实现：注册 metatable，push/check C++ 对象
///
/// 当 Lua 可用时（SNC_LUA_AVAILABLE），实现完整的 Lua-C++ 桥接。
/// 当 Lua 不可用时，所有函数为 no-op stub，测试可正常编译运行。

#include "snc/mmorpg/legend/LuaBridge.h"
#include <cassert>

namespace snc {
namespace mmorpg {
namespace legend {

// ════════════════════════════════════════════════════════════════
// Lua 可用时的完整实现
// ════════════════════════════════════════════════════════════════
#ifdef SNC_LUA_AVAILABLE

// ── 元表 key ────────────────────────────────────────────────────
static const char* kMetaPlayer    = "snc.legend.PlayerActor";
static const char* kMetaMonster   = "snc.legend.MonsterInstance";
static const char* kMetaCombat    = "snc.legend.CombatSystem";
static const char* kMetaDropMgr   = "snc.legend.DropManager";
static const char* kMetaGuildMgr  = "snc.legend.GuildManager";
static const char* kMetaWorldChan = "snc.legend.WorldChannel";
static const char* kMetaNPCMgr    = "snc.legend.NPCManager";

// ── 辅助：从 Lua 栈取指针 ───────────────────────────────────────
template<typename T>
static T* GetPtr(lua_State* L, const char* meta, int idx) {
    void** p = static_cast<void**>(lua_touserdata(L, idx));
    if (p && lua_getmetatable(L, idx)) {
        luaL_getmetatable(L, meta);
        if (lua_rawequal(L, -1, -2)) {
            lua_pop(L, 2);
            return reinterpret_cast<T*>(*p);
        }
        lua_pop(L, 2);
    }
    return nullptr;
}

template<typename T>
static void SetPtr(lua_State* L, const char* meta, T* ptr) {
    void** p = static_cast<void**>(lua_newuserdatauv(L, sizeof(void*)));
    *p = reinterpret_cast<void*>(ptr);
    luaL_getmetatable(L, meta);
    lua_setmetatable(L, -2);
}

// ════════════════════════════════════════════════════════════════
// PlayerActor 方法表
// ════════════════════════════════════════════════════════════════
static int lb_player_id     (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0; lua_pushinteger(L, p->PlayerId());    return 1; }
static int lb_nickname      (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushnil(L); return 1; } lua_pushstring(L, p->Nickname().c_str()); return 1; }
static int lb_class_type    (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, -1); return 1; } lua_pushinteger(L, (int)p->GetClassType()); return 1; }
static int lb_level         (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->Level()); return 1; }
static int lb_exp           (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, (lua_Integer)p->Exp()); return 1; }
static int lb_gold          (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, (lua_Integer)p->Gold()); return 1; }
static int lb_cur_hp        (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->CurrentHp()); return 1; }
static int lb_max_hp        (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->MaxHp()); return 1; }
static int lb_cur_mp        (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->CurrentMp()); return 1; }
static int lb_max_mp        (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->MaxMp()); return 1; }
static int lb_map_id        (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->Pos().mapId); return 1; }
static int lb_x             (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->Pos().x); return 1; }
static int lb_y             (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->Pos().y); return 1; }
static int lb_str           (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->TotalStats().str); return 1; }
static int lb_mag           (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->TotalStats().mag); return 1; }
static int lb_def           (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->TotalStats().def); return 1; }

static int lb_add_item      (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, -1); return 1; }
    int32_t id  = (int32_t)luaL_checkinteger(L, 2);
    int32_t cnt = (int32_t)luaL_checkinteger(L, 3);
    lua_pushinteger(L, p->AddItem(id, cnt)); return 1;
}
static int lb_has_item      (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushboolean(L, 0); return 1; }
    int32_t id = (int32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, p->HasItem(id) ? 1 : 0); return 1;
}
static int lb_add_gold      (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    lua_Integer amt = luaL_checkinteger(L, 2);
    p->AddGold(amt); return 0;
}
static int lb_spend_gold    (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    lua_Integer amt = luaL_checkinteger(L, 2);
    p->SpendGold(amt); return 0;
}
static int lb_level_up      (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    p->LevelUp(); return 0;
}
static int lb_set_hp        (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    int32_t hp = (int32_t)luaL_checkinteger(L, 2);
    p->SetHp(hp); return 0;
}
static int lb_set_mp        (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    int32_t mp = (int32_t)luaL_checkinteger(L, 2);
    p->SetMp(mp); return 0;
}
static int lb_take_damage   (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    int32_t dmg = (int32_t)luaL_checkinteger(L, 2);
    p->TakeDamage(dmg); return 0;
}
static int lb_move_to       (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    int32_t mapId = (int32_t)luaL_checkinteger(L, 2);
    int32_t x = (int32_t)luaL_checkinteger(L, 3);
    int32_t y = (int32_t)luaL_checkinteger(L, 4);
    lua_pushboolean(L, p->MoveTo(mapId, x, y) ? 1 : 0); return 1;
}
static int lb_join_guild    (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    int32_t gid = (int32_t)luaL_checkinteger(L, 2);
    const char* name = lua_tostring(L, 3) ? lua_tostring(L, 3) : "";
    int32_t rank = (int32_t)luaL_optinteger(L, 4, 3);
    p->JoinGuild(gid, name, rank); return 0;
}
static int lb_leave_guild   (lua_State* L) {
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) return 0;
    p->LeaveGuild(); return 0;
}
static int lb_guild_id      (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->Guild().guild_id); return 1; }
static int lb_guild_name    (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushnil(L); return 1; } lua_pushstring(L, p->Guild().guild_name.c_str()); return 1; }
static int lb_guild_rank    (lua_State* L) { auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 1); if (!p) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, p->Guild().guild_rank); return 1; }

static const struct luaL_Reg kPlayerMethods[] = {
    {"player_id",     lb_player_id},   {"nickname",      lb_nickname},
    {"class_type",    lb_class_type},  {"level",         lb_level},
    {"exp",           lb_exp},         {"gold",          lb_gold},
    {"cur_hp",        lb_cur_hp},      {"max_hp",        lb_max_hp},
    {"cur_mp",        lb_cur_mp},      {"max_mp",        lb_max_mp},
    {"map_id",        lb_map_id},      {"x",             lb_x},
    {"y",             lb_y},           {"str",           lb_str},
    {"mag",           lb_mag},         {"def",           lb_def},
    {"add_item",      lb_add_item},    {"has_item",      lb_has_item},
    {"add_gold",      lb_add_gold},    {"spend_gold",    lb_spend_gold},
    {"level_up",      lb_level_up},    {"set_hp",        lb_set_hp},
    {"set_mp",        lb_set_mp},      {"take_damage",   lb_take_damage},
    {"move_to",       lb_move_to},     {"join_guild",    lb_join_guild},
    {"leave_guild",   lb_leave_guild}, {"guild_id",      lb_guild_id},
    {"guild_name",    lb_guild_name},  {"guild_rank",    lb_guild_rank},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// MonsterInstance 方法
// ════════════════════════════════════════════════════════════════
static int lb_monster_cur_hp  (lua_State* L) { auto* m = GetPtr<MonsterInstance>(L, kMetaMonster, 1); if (!m) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, m->current_hp); return 1; }
static int lb_monster_is_alive(lua_State* L) { auto* m = GetPtr<MonsterInstance>(L, kMetaMonster, 1); if (!m) { lua_pushboolean(L, 0); return 1; } lua_pushboolean(L, m->alive ? 1 : 0); return 1; }
static int lb_monster_x       (lua_State* L) { auto* m = GetPtr<MonsterInstance>(L, kMetaMonster, 1); if (!m) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, m->pos.x); return 1; }
static int lb_monster_y       (lua_State* L) { auto* m = GetPtr<MonsterInstance>(L, kMetaMonster, 1); if (!m) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, m->pos.y); return 1; }
static int lb_monster_map_id  (lua_State* L) { auto* m = GetPtr<MonsterInstance>(L, kMetaMonster, 1); if (!m) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, m->pos.mapId); return 1; }
static int lb_monster_state   (lua_State* L) { auto* m = GetPtr<MonsterInstance>(L, kMetaMonster, 1); if (!m) { lua_pushinteger(L, 0); return 1; } lua_pushinteger(L, (int)m->state); return 1; }

static const struct luaL_Reg kMonsterMethods[] = {
    {"cur_hp",     lb_monster_cur_hp},  {"is_alive",   lb_monster_is_alive},
    {"x",          lb_monster_x},       {"y",          lb_monster_y},
    {"map_id",     lb_monster_map_id},  {"state",      lb_monster_state},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// CombatSystem 方法
// ════════════════════════════════════════════════════════════════
static int lb_combat_phys_dmg(lua_State* L) {
    auto* c = GetPtr<CombatSystem>(L, kMetaCombat, 1); if (!c) { lua_pushinteger(L, 0); return 1; }
    auto* atk = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!atk) { lua_pushinteger(L, 0); return 1; }
    auto* def = GetPtr<PlayerActor>(L, kMetaPlayer, 3); if (!def) { lua_pushinteger(L, 0); return 1; }
    float ratio = (float)luaL_optnumber(L, 4, 1.0);
    auto r = c->CalcPhysicalDamage(*atk, *def, ratio);
    lua_pushinteger(L, r.damage); return 1;
}
static int lb_combat_magic_dmg(lua_State* L) {
    auto* c = GetPtr<CombatSystem>(L, kMetaCombat, 1); if (!c) { lua_pushinteger(L, 0); return 1; }
    auto* atk = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!atk) { lua_pushinteger(L, 0); return 1; }
    auto* def = GetPtr<PlayerActor>(L, kMetaPlayer, 3); if (!def) { lua_pushinteger(L, 0); return 1; }
    float ratio = (float)luaL_optnumber(L, 4, 1.0);
    auto r = c->CalcMagicDamage(*atk, *def, ratio);
    lua_pushinteger(L, r.damage); return 1;
}
static int lb_combat_heal(lua_State* L) {
    auto* c = GetPtr<CombatSystem>(L, kMetaCombat, 1); if (!c) { lua_pushinteger(L, 0); return 1; }
    auto* t = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!t) { lua_pushinteger(L, 0); return 1; }
    int32_t amt = (int32_t)luaL_checkinteger(L, 3);
    lua_pushinteger(L, c->Heal(std::const_pointer_cast<PlayerActor>(
        t ? std::make_shared<PlayerActor>(*t) : nullptr), amt)); return 1;
}
static int lb_combat_register_skill(lua_State* L) {
    auto* c = GetPtr<CombatSystem>(L, kMetaCombat, 1); if (!c) return 0;
    SkillDef s{};
    s.skill_id = (int32_t)luaL_checkinteger(L, 2);
    s.name     = lua_tostring(L, 3)  ? lua_tostring(L, 3)  : "";
    s.cls      = (ClassType)(int32_t)luaL_optinteger(L, 4, (int)ClassType::WARRIOR);
    s.min_level= (int32_t)luaL_optinteger(L, 5, 1);
    s.mp_cost  = (int32_t)luaL_optinteger(L, 6, 0);
    c->RegisterSkill(s); return 0;
}
static int lb_combat_can_use_skill(lua_State* L) {
    auto* c = GetPtr<CombatSystem>(L, kMetaCombat, 1); if (!c) { lua_pushboolean(L, 0); return 1; }
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!p) { lua_pushboolean(L, 0); return 1; }
    int32_t sid = (int32_t)luaL_checkinteger(L, 3);
    lua_pushboolean(L, c->CanUseSkill(*p, sid) ? 1 : 0); return 1;
}

static const struct luaL_Reg kCombatMethods[] = {
    {"calc_physical_damage", lb_combat_phys_dmg},
    {"calc_magic_damage",    lb_combat_magic_dmg},
    {"heal",                 lb_combat_heal},
    {"register_skill",       lb_combat_register_skill},
    {"can_use_skill",        lb_combat_can_use_skill},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// DropManager 方法
// ════════════════════════════════════════════════════════════════
static int lb_drop_register_table(lua_State* L) {
    auto* d = GetPtr<DropManager>(L, kMetaDropMgr, 1); if (!d) return 0;
    int32_t tid = (int32_t)luaL_checkinteger(L, 2);
    DropTable tbl{tid, {}};
    if (lua_istable(L, 3)) {
        lua_pushnil(L);
        while (lua_next(L, 3)) {
            DropEntry e{};
            e.item_id   = (int32_t)luaL_checkinteger(L, -4);
            e.rate      = (int32_t)luaL_checkinteger(L, -3);
            e.min_count = (int32_t)luaL_optinteger(L, -2, 1);
            e.max_count = (int32_t)luaL_optinteger(L, -1, 1);
            tbl.entries.push_back(e);
            lua_pop(L, 1);
        }
    }
    d->RegisterTable(tbl); return 0;
}
static int lb_drop_roll(lua_State* L) {
    auto* d = GetPtr<DropManager>(L, kMetaDropMgr, 1); if (!d) { lua_pushinteger(L, 0); return 1; }
    int32_t tid = (int32_t)luaL_checkinteger(L, 2);
    int32_t cnt = (int32_t)luaL_optinteger(L, 3, 1);
    auto items = d->RollDrops(tid, cnt);
    lua_createtable(L, (int)items.size(), 0);
    for (size_t i = 0; i < items.size(); ++i) {
        lua_createtable(L, 0, 2);
        lua_pushstring(L, "item_id");   lua_pushinteger(L, items[i].first);   lua_setfield(L, -2, "item_id");
        lua_pushstring(L, "count");     lua_pushinteger(L, items[i].second);  lua_setfield(L, -2, "count");
        lua_seti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}
static int lb_drop_create(lua_State* L) {
    auto* d = GetPtr<DropManager>(L, kMetaDropMgr, 1); if (!d) { lua_pushinteger(L, 0); return 1; }
    int32_t mapId = (int32_t)luaL_checkinteger(L, 2);
    int32_t x = (int32_t)luaL_checkinteger(L, 3);
    int32_t y = (int32_t)luaL_checkinteger(L, 4);
    int32_t itemId = (int32_t)luaL_checkinteger(L, 5);
    int32_t count = (int32_t)luaL_optinteger(L, 6, 1);
    auto drop = d->CreateDrop(mapId, x, y, itemId, count);
    lua_pushinteger(L, drop ? (lua_Integer)drop.get() : 0); return 1;
}
static int lb_drop_pickup(lua_State* L) {
    auto* d = GetPtr<DropManager>(L, kMetaDropMgr, 1); if (!d) { lua_pushinteger(L, 0); return 1; }
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!p) { lua_pushinteger(L, 0); return 1; }
    int32_t range = (int32_t)luaL_optinteger(L, 3, 2);
    lua_pushinteger(L, d->PickUp(std::const_pointer_cast<PlayerActor>(
        p ? std::make_shared<PlayerActor>(*p) : nullptr), range)); return 1;
}
static int lb_drop_cleanup(lua_State* L) {
    auto* d = GetPtr<DropManager>(L, kMetaDropMgr, 1); if (!d) { lua_pushinteger(L, 0); return 1; }
    int64_t now = (int64_t)luaL_optinteger(L, 2, 0);
    lua_pushinteger(L, d->CleanupExpired(now)); return 1;
}
static int lb_drop_count(lua_State* L) {
    auto* d = GetPtr<DropManager>(L, kMetaDropMgr, 1); if (!d) { lua_pushinteger(L, 0); return 1; }
    int32_t mapId = (int32_t)luaL_checkinteger(L, 2);
    lua_pushinteger(L, (lua_Integer)d->DropCount(mapId)); return 1;
}

static const struct luaL_Reg kDropMethods[] = {
    {"register_table",  lb_drop_register_table},
    {"roll_drops",      lb_drop_roll},
    {"create_drop",     lb_drop_create},
    {"pick_up",         lb_drop_pickup},
    {"cleanup_expired", lb_drop_cleanup},
    {"drop_count",      lb_drop_count},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// GuildManager 方法
// ════════════════════════════════════════════════════════════════
static int lb_guild_create(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushinteger(L, 0); return 1; }
    auto* leader = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!leader) { lua_pushinteger(L, 0); return 1; }
    const char* name = lua_tostring(L, 3) ? lua_tostring(L, 3) : "";
    int32_t level_req = (int32_t)luaL_optinteger(L, 4, 5);
    int32_t gold_cost = (int32_t)luaL_optinteger(L, 5, 5000);
    lua_pushinteger(L, g->CreateGuild(std::const_pointer_cast<PlayerActor>(
        leader ? std::make_shared<PlayerActor>(*leader) : nullptr),
        name, level_req, gold_cost));
    return 1;
}
static int lb_guild_join(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushboolean(L, 0); return 1; }
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!p) { lua_pushboolean(L, 0); return 1; }
    int32_t gid = (int32_t)luaL_checkinteger(L, 3);
    lua_pushboolean(L, g->JoinGuild(std::const_pointer_cast<PlayerActor>(
        p ? std::make_shared<PlayerActor>(*p) : nullptr), gid) ? 1 : 0);
    return 1;
}
static int lb_guild_kick(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushboolean(L, 0); return 1; }
    auto* op = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!op) { lua_pushboolean(L, 0); return 1; }
    int32_t target = (int32_t)luaL_checkinteger(L, 3);
    lua_pushboolean(L, g->KickMember(std::const_pointer_cast<PlayerActor>(
        op ? std::make_shared<PlayerActor>(*op) : nullptr), target) ? 1 : 0);
    return 1;
}
static int lb_guild_leave(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) return 0;
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!p) return 0;
    g->LeaveGuild(std::const_pointer_cast<PlayerActor>(
        p ? std::make_shared<PlayerActor>(*p) : nullptr));
    return 0;
}
static int lb_guild_dissolve(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) return 0;
    auto* leader = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!leader) return 0;
    g->DissolveGuild(std::const_pointer_cast<PlayerActor>(
        leader ? std::make_shared<PlayerActor>(*leader) : nullptr));
    return 0;
}
static int lb_guild_put_gold(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushboolean(L, 0); return 1; }
    int32_t gid  = (int32_t)luaL_checkinteger(L, 2);
    int32_t pid  = (int32_t)luaL_checkinteger(L, 3);
    lua_Integer amt = luaL_checkinteger(L, 4);
    lua_pushboolean(L, g->PutGold(gid, pid, amt) ? 1 : 0); return 1;
}
static int lb_guild_take_gold(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushboolean(L, 0); return 1; }
    int32_t gid  = (int32_t)luaL_checkinteger(L, 2);
    int32_t pid  = (int32_t)luaL_checkinteger(L, 3);
    lua_Integer amt = luaL_checkinteger(L, 4);
    lua_pushboolean(L, g->TakeGold(gid, pid, amt) ? 1 : 0); return 1;
}
static int lb_guild_is_at_war(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushboolean(L, 0); return 1; }
    int32_t gid = (int32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, g->IsAtWar(gid) ? 1 : 0); return 1;
}
static int lb_guild_start_siege(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) return 0;
    int32_t gid = (int32_t)luaL_checkinteger(L, 2);
    int32_t mid = (int32_t)luaL_optinteger(L, 3, 0);
    g->StartSiege(gid, mid); return 0;
}
static int lb_guild_end_siege(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) return 0;
    int32_t gid = (int32_t)luaL_checkinteger(L, 2);
    g->EndSiege(gid); return 0;
}
static int lb_guild_has_permission(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushboolean(L, 0); return 1; }
    int32_t rank = (int32_t)luaL_checkinteger(L, 2);
    int32_t action = (int32_t)luaL_checkinteger(L, 3);
    lua_pushboolean(L, 1); return 1;  // placeholder
}
static int lb_guild_get_member_count(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushinteger(L, 0); return 1; }
    int32_t gid = (int32_t)luaL_checkinteger(L, 2);
    auto guild = g->GetGuild(gid);
    lua_pushinteger(L, guild ? guild->member_count : 0); return 1;
}
static int lb_guild_get_treasury(lua_State* L) {
    auto* g = GetPtr<GuildManager>(L, kMetaGuildMgr, 1); if (!g) { lua_pushinteger(L, 0); return 1; }
    int32_t gid = (int32_t)luaL_checkinteger(L, 2);
    auto guild = g->GetGuild(gid);
    lua_pushinteger(L, (lua_Integer)(guild ? guild->treasury : 0)); return 1;
}

static const struct luaL_Reg kGuildMethods[] = {
    {"create",       lb_guild_create},
    {"join",         lb_guild_join},
    {"kick",         lb_guild_kick},
    {"leave",        lb_guild_leave},
    {"dissolve",     lb_guild_dissolve},
    {"put_gold",     lb_guild_put_gold},
    {"take_gold",    lb_guild_take_gold},
    {"is_at_war",    lb_guild_is_at_war},
    {"start_siege",  lb_guild_start_siege},
    {"end_siege",    lb_guild_end_siege},
    {"has_permission",lb_guild_has_permission},
    {"member_count", lb_guild_get_member_count},
    {"treasury",     lb_guild_get_treasury},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// WorldChannel 方法
// ════════════════════════════════════════════════════════════════
static int lb_wc_send(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) { lua_pushboolean(L, 0); return 1; }
    int32_t channel = (int32_t)luaL_checkinteger(L, 2);
    int32_t senderId = (int32_t)luaL_checkinteger(L, 3);
    const char* content = lua_tostring(L, 4) ? lua_tostring(L, 4) : "";
    int32_t targetId = (int32_t)luaL_optinteger(L, 5, 0);
    int32_t guildId  = (int32_t)luaL_optinteger(L, 6, 0);
    lua_pushboolean(L, wc->Send((ChatChannel)channel, senderId, content, targetId, guildId) ? 1 : 0);
    return 1;
}
static int lb_wc_get_world_history(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) { lua_pushnil(L); return 1; }
    int32_t limit = (int32_t)luaL_optinteger(L, 2, 10);
    auto hist = wc->GetWorldHistory(limit);
    lua_createtable(L, (int)hist.size(), 0);
    for (size_t i = 0; i < hist.size(); ++i) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, "sender_id");    lua_pushinteger(L, hist[i].sender_id);    lua_setfield(L, -2, "sender_id");
        lua_pushstring(L, "sender_name");  lua_pushstring(L, hist[i].sender_name.c_str()); lua_setfield(L, -2, "sender_name");
        lua_pushstring(L, "content");      lua_pushstring(L, hist[i].content.c_str());   lua_setfield(L, -2, "content");
        lua_pushstring(L, "channel");      lua_pushinteger(L, (int)hist[i].channel);   lua_setfield(L, -2, "channel");
        lua_seti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}
static int lb_wc_add_forbidden(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) return 0;
    const char* word = lua_tostring(L, 2); if (!word) return 0;
    wc->AddForbiddenWord(word); return 0;
}
static int lb_wc_set_cooldown(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) return 0;
    wc->SetCooldownMs((int64_t)luaL_checkinteger(L, 2)); return 0;
}
static int lb_wc_set_max_len(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) return 0;
    wc->SetMaxMessageLen((size_t)luaL_checkinteger(L, 2)); return 0;
}
static int lb_wc_register_player(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) return 0;
    int32_t pid = (int32_t)luaL_checkinteger(L, 2);
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 3);
    if (p) wc->RegisterPlayer(pid, std::const_pointer_cast<PlayerActor>(
        p ? std::make_shared<PlayerActor>(*p) : nullptr));
    return 0;
}
static int lb_wc_unregister_player(lua_State* L) {
    auto* wc = GetPtr<WorldChannel>(L, kMetaWorldChan, 1); if (!wc) return 0;
    int32_t pid = (int32_t)luaL_checkinteger(L, 2);
    wc->UnregisterPlayer(pid); return 0;
}

static const struct luaL_Reg kWorldChanMethods[] = {
    {"send",               lb_wc_send},
    {"get_world_history",  lb_wc_get_world_history},
    {"add_forbidden_word", lb_wc_add_forbidden},
    {"set_cooldown_ms",    lb_wc_set_cooldown},
    {"set_max_message_len",lb_wc_set_max_len},
    {"register_player",    lb_wc_register_player},
    {"unregister_player",  lb_wc_unregister_player},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// MonsterManager 方法
// ════════════════════════════════════════════════════════════════
static int lb_mm_register(lua_State* L) {
    auto* m = GetPtr<MonsterManager>(L, "snc.legend.MonsterManager", 1); if (!m) return 0;
    MonsterDef def{};
    def.monster_id = (int32_t)luaL_checkinteger(L, 2);
    def.name       = lua_tostring(L, 3)  ? lua_tostring(L, 3)  : "";
    def.hp         = (int32_t)luaL_optinteger(L, 4, 50);
    def.attack     = (int32_t)luaL_optinteger(L, 5, 5);
    def.defense    = (int32_t)luaL_optinteger(L, 6, 2);
    def.exp        = (int32_t)luaL_optinteger(L, 7, 10);
    def.speed      = (int32_t)luaL_optinteger(L, 8, 2);
    def.drop_table_id = (int32_t)luaL_optinteger(L, 9, 0);
    def.respawnMs  = (int64_t)luaL_optinteger(L, 10, 30000);
    m->RegisterMonster(def); return 0;
}
static int lb_mm_spawn(lua_State* L) {
    auto* mgr = GetPtr<MonsterManager>(L, "snc.legend.MonsterManager", 1); if (!mgr) { lua_pushnil(L); return 1; }
    int32_t mid   = (int32_t)luaL_checkinteger(L, 2);
    int32_t moid  = (int32_t)luaL_checkinteger(L, 3);
    int32_t x     = (int32_t)luaL_checkinteger(L, 4);
    int32_t y     = (int32_t)luaL_checkinteger(L, 5);
    auto monster = mgr->SpawnMonster(mid, moid, x, y);
    if (!monster) { lua_pushnil(L); return 1; }
    // Push monster as raw pointer (for read-only access)
    void** p = static_cast<void**>(lua_newuserdatauv(L, sizeof(void*)));
    *p = const_cast<MonsterInstance*>(&monster->def); // push def ptr as marker
    luaL_getmetatable(L, kMetaMonster);
    lua_setmetatable(L, -2);
    return 1;
}
static int lb_mm_count(lua_State* L) {
    auto* mgr = GetPtr<MonsterManager>(L, "snc.legend.MonsterManager", 1); if (!mgr) { lua_pushinteger(L, 0); return 1; }
    int32_t mid = (int32_t)luaL_checkinteger(L, 2);
    lua_pushinteger(L, (lua_Integer)mgr->MonsterCount(mid)); return 1;
}
static int lb_mm_destroy(lua_State* L) {
    auto* mgr = GetPtr<MonsterManager>(L, "snc.legend.MonsterManager", 1); if (!mgr) return 0;
    int32_t mid   = (int32_t)luaL_checkinteger(L, 2);
    int32_t moid  = (int32_t)luaL_checkinteger(L, 3);
    mgr->DestroyMonster(mid, moid); return 0;
}

static const struct luaL_Reg kMonsterMgrMethods[] = {
    {"register", lb_mm_register},
    {"spawn",    lb_mm_spawn},
    {"count",    lb_mm_count},
    {"destroy",  lb_mm_destroy},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// NPCManager 方法
// ════════════════════════════════════════════════════════════════
static int lb_npc_register(lua_State* L) {
    auto* n = GetPtr<NPCManager>(L, "snc.legend.NPCManager", 1); if (!n) return 0;
    NPCDef def{};
    def.npc_id   = (int32_t)luaL_checkinteger(L, 2);
    def.name     = lua_tostring(L, 3)  ? lua_tostring(L, 3)  : "";
    def.type     = (NPCType)(int32_t)luaL_optinteger(L, 4, (int)NPCType::DIALOG);
    def.map_id   = (int32_t)luaL_optinteger(L, 5, 0);
    def.x        = (int32_t)luaL_optinteger(L, 6, 0);
    def.y        = (int32_t)luaL_optinteger(L, 7, 0);
    n->RegisterNPC(def); return 0;
}
static int lb_npc_place(lua_State* L) {
    auto* n = GetPtr<NPCManager>(L, "snc.legend.NPCManager", 1); if (!n) { lua_pushboolean(L, 0); return 1; }
    int32_t mid  = (int32_t)luaL_checkinteger(L, 2);
    int32_t nid  = (int32_t)luaL_checkinteger(L, 3);
    int32_t x    = (int32_t)luaL_checkinteger(L, 4);
    int32_t y    = (int32_t)luaL_checkinteger(L, 5);
    lua_pushboolean(L, n->PlaceNPC(mid, nid, x, y) ? 1 : 0); return 1;
}
static int lb_npc_talk(lua_State* L) {
    auto* n = GetPtr<NPCManager>(L, "snc.legend.NPCManager", 1); if (!n) { lua_pushstring(L, ""); return 1; }
    auto* p = GetPtr<PlayerActor>(L, kMetaPlayer, 2); if (!p) { lua_pushstring(L, ""); return 1; }
    int32_t nid = (int32_t)luaL_checkinteger(L, 3);
    std::string result = n->Talk(std::const_pointer_cast<PlayerActor>(
        p ? std::make_shared<PlayerActor>(*p) : nullptr), nid);
    lua_pushstring(L, result.c_str()); return 1;
}

static const struct luaL_Reg kNPCMgrMethods[] = {
    {"register", lb_npc_register},
    {"place",    lb_npc_place},
    {"talk",     lb_npc_talk},
    {nullptr, nullptr}
};

// ════════════════════════════════════════════════════════════════
// 注册所有 metatable
// ════════════════════════════════════════════════════════════════
void LuaBridge::RegisterAllStatics(lua_State* L) {
    // PlayerActor
    {
        luaL_newmetatable(L, kMetaPlayer);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kPlayerMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // MonsterInstance
    {
        luaL_newmetatable(L, kMetaMonster);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kMonsterMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // CombatSystem
    {
        luaL_newmetatable(L, kMetaCombat);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kCombatMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // DropManager
    {
        luaL_newmetatable(L, kMetaDropMgr);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kDropMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // GuildManager
    {
        luaL_newmetatable(L, kMetaGuildMgr);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kGuildMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // WorldChannel
    {
        luaL_newmetatable(L, kMetaWorldChan);
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kWorldChanMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // MonsterManager
    {
        luaL_newmetatable(L, "snc.legend.MonsterManager");
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kMonsterMgrMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
    // NPCManager
    {
        luaL_newmetatable(L, "snc.legend.NPCManager");
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        for (const auto* fn = kNPCMgrMethods; fn->name; ++fn)
            lua_register(L, fn->name, fn->func);
        lua_pop(L, 1);
    }
}

// ── 实例方法 ──────────────────────────────────────────────────────
void LuaBridge::PushPlayer(PlayerActor* p) {
    if (!L_) return;
    SetPtr<PlayerActor>(L_, kMetaPlayer, p);
}
void LuaBridge::PushMonster(const MonsterInstance* m) {
    if (!L_) return;
    SetPtr<MonsterInstance>(L_, kMetaMonster, const_cast<MonsterInstance*>(m));
}
void LuaBridge::PushCombat(CombatSystem* c) {
    if (!L_) return;
    SetPtr<CombatSystem>(L_, kMetaCombat, c);
}
void LuaBridge::PushDropMgr(DropManager* d) {
    if (!L_) return;
    SetPtr<DropManager>(L_, kMetaDropMgr, d);
}
void LuaBridge::PushGuildMgr(GuildManager* g) {
    if (!L_) return;
    SetPtr<GuildManager>(L_, kMetaGuildMgr, g);
}
void LuaBridge::PushWorldChan(WorldChannel* wc) {
    if (!L_) return;
    SetPtr<WorldChannel>(L_, kMetaWorldChan, wc);
}
void LuaBridge::PushNPCMgr(NPCManager* n) {
    if (!L_) return;
    SetPtr<NPCManager>(L_, "snc.legend.NPCManager", n);
}

PlayerActor*      LuaBridge::CheckPlayer(int idx)      { return L_ ? GetPtr<PlayerActor>(L_, kMetaPlayer, idx) : nullptr; }
MonsterInstance*  LuaBridge::CheckMonster(int idx)     { return L_ ? GetPtr<MonsterInstance>(L_, kMetaMonster, idx) : nullptr; }
CombatSystem*     LuaBridge::CheckCombat(int idx)      { return L_ ? GetPtr<CombatSystem>(L_, kMetaCombat, idx) : nullptr; }
DropManager*      LuaBridge::CheckDropMgr(int idx)     { return L_ ? GetPtr<DropManager>(L_, kMetaDropMgr, idx) : nullptr; }
GuildManager*     LuaBridge::CheckGuildMgr(int idx)    { return L_ ? GetPtr<GuildManager>(L_, kMetaGuildMgr, idx) : nullptr; }
WorldChannel*     LuaBridge::CheckWorldChan(int idx)   { return L_ ? GetPtr<WorldChannel>(L_, kMetaWorldChan, idx) : nullptr; }
NPCManager*       LuaBridge::CheckNPCMgr(int idx)      { return L_ ? GetPtr<NPCManager>(L_, "snc.legend.NPCManager", idx) : nullptr; }

void LuaBridge::RegisterAll(LuaState L) { RegisterAllStatics(L); }

void LuaBridge::PushPlayer(LuaState L, PlayerActor* p) {
    if (!L) return;
    void** pp = static_cast<void**>(lua_newuserdatauv(L, sizeof(void*)));
    *pp = reinterpret_cast<void*>(p);
    luaL_getmetatable(L, kMetaPlayer);
    lua_setmetatable(L, -2);
}
void LuaBridge::PushMonster(LuaState L, const MonsterInstance* m) {
    if (!L) return;
    void** pp = static_cast<void**>(lua_newuserdatauv(L, sizeof(void*)));
    *pp = reinterpret_cast<void*>(const_cast<MonsterInstance*>(m));
    luaL_getmetatable(L, kMetaMonster);
    lua_setmetatable(L, -2);
}

bool LuaBridge::LoadScript(const std::string& path) {
    if (!L_) return false;
    return luaL_dofile(L_, path.c_str()) == 0;
}
int LuaBridge::CallGlobal(const std::string& funcName, int nargs, int nresults) {
    if (!L_) return -1;
    lua_getglobal(L_, funcName.c_str());
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return -1; }
    // args already on stack before call
    int err = lua_pcall(L_, nargs, nresults, 0);
    if (err != 0) {
        const char* msg = lua_tostring(L_, -1);
        (void)msg;
        lua_pop(L_, 1);
        return err;
    }
    return nresults;
}
std::string LuaBridge::GetGlobalString(const std::string& name) {
    if (!L_) return {};
    lua_getglobal(L_, name.c_str());
    if (!lua_isstring(L_, -1)) { lua_pop(L_, 1); return {}; }
    std::string s(lua_tostring(L_, -1));
    lua_pop(L_, 1);
    return s;
}

#else // ── SNC_LUA_AVAILABLE not defined: stub implementation ──

// ── 所有函数为 no-op stub ────────────────────────────────────────
void LuaBridge::PushPlayer(PlayerActor*) {}
void LuaBridge::PushMonster(const MonsterInstance*) {}
void LuaBridge::PushCombat(CombatSystem*) {}
void LuaBridge::PushDropMgr(DropManager*) {}
void LuaBridge::PushGuildMgr(GuildManager*) {}
void LuaBridge::PushWorldChan(WorldChannel*) {}
void LuaBridge::PushNPCMgr(NPCManager*) {}

PlayerActor*      LuaBridge::CheckPlayer(int)      { return nullptr; }
MonsterInstance*  LuaBridge::CheckMonster(int)     { return nullptr; }
CombatSystem*     LuaBridge::CheckCombat(int)      { return nullptr; }
DropManager*      LuaBridge::CheckDropMgr(int)     { return nullptr; }
GuildManager*     LuaBridge::CheckGuildMgr(int)    { return nullptr; }
WorldChannel*     LuaBridge::CheckWorldChan(int)   { return nullptr; }
NPCManager*       LuaBridge::CheckNPCMgr(int)      { return nullptr; }

void LuaBridge::RegisterAll(void*) {}
void LuaBridge::RegisterAllStatics(void*) {}

void LuaBridge::PushPlayer(void*, PlayerActor*) {}
void LuaBridge::PushMonster(void*, const MonsterInstance*) {}

#endif // SNC_LUA_AVAILABLE — end stub section

} // namespace legend
} // namespace mmorpg
} // namespace snc
