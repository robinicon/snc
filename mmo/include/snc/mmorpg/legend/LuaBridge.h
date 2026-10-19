// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file LuaBridge.h
/// @brief �?C++ 传奇对象推入 Lua VM，供 Lua 脚本访问
///
/// 使用方式（C++ 侧）�?
///   LuaBridge bridge(L);
///   bridge.PushPlayer(player_ptr.get());
///
/// �?Lua 未安装时，所有函数为 no-op stub，测试可正常编译运行�?

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <vector>

#ifdef SNC_LUA_AVAILABLE
#include <lua.hpp>
#endif

#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace mmorpg {
namespace legend {

// 前向声明（避免循环依赖）
class MonsterManager;
struct MonsterInstance;
class CombatSystem;
class DropManager;
class GuildManager;
class WorldChannel;
class NPCManager;

// ════════════════════════════════════════════════════════════════
// LuaBridge 是 C++ 传奇对象推入 Lua VM 的桥接�?Lua �?C++ 传奇对象的桥�?
// ════════════════════════════════════════════════════════════════
class LuaBridge {
public:
#ifdef SNC_LUA_AVAILABLE
    using LuaState = lua_State*;
#else
    using LuaState = void*;
#endif

    explicit LuaBridge(LuaState L) : L_(L) {}

    // ── 获取 lua_State ───────────────────────────────────────
    LuaState L() const { return L_; }
    bool IsValid() const { return L_ != nullptr; }

    // ── 推�?C++ 对象�?Lua �?────────────────────────────────
    void PushPlayer(PlayerActor* p);
    void PushMonster(const MonsterInstance* m);
    void PushCombat(CombatSystem* c);
    void PushDropMgr(DropManager* d);
    void PushGuildMgr(GuildManager* g);
    void PushWorldChan(WorldChannel* wc);
    void PushNPCMgr(NPCManager* n);

    // ── �?Lua 栈检�?C++ 对象 ───────────────────────────────
    PlayerActor*      CheckPlayer(int idx);
    MonsterInstance*  CheckMonster(int idx);
    CombatSystem*     CheckCombat(int idx);
    DropManager*      CheckDropMgr(int idx);
    GuildManager*     CheckGuildMgr(int idx);
    WorldChannel*     CheckWorldChan(int idx);
    NPCManager*       CheckNPCMgr(int idx);

    // ── 注册所�?metatable ───────────────────────────────────
    static void RegisterAll(LuaState L);
    static void RegisterAllStatics(LuaState L);

    // ── 便捷静态函�?─────────────────────────────────────────
    static void PushPlayer(LuaState L, PlayerActor* p);
    static void PushMonster(LuaState L, const MonsterInstance* m);

#ifdef SNC_LUA_AVAILABLE
    // ── Lua 脚本管理 ─────────────────────────────────────────
    /// 加载并执�?Lua 脚本文件
    bool LoadScript(const std::string& path);
    /// 调用全局函数
    int CallGlobal(const std::string& funcName, int nargs = 0, int nresults = 0);
    /// 获取全局字符串�?
    std::string GetGlobalString(const std::string& name);
#endif

private:
    LuaState L_ = nullptr;
};

// ── Lua 脚本路径常量 ────────────────────────────────────────────────
namespace LuaScriptPath {
    constexpr const char* COMBAT    = "mmo/lua/combat.lua";
    constexpr const char* DROP      = "mmo/lua/drop.lua";
    constexpr const char* MONSTER   = "mmo/lua/monster.lua";
    constexpr const char* GUILD     = "mmo/lua/guild.lua";
    constexpr const char* WORLD_CHAN= "mmo/lua/world_channel.lua";
    constexpr const char* NPC       = "mmo/lua/npc.lua";
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
