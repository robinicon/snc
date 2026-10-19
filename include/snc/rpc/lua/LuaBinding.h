// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

#ifdef SNC_LUA_AVAILABLE
#include <lua.hpp>
#endif

namespace snc {
namespace rpc {
namespace lua {

// Forward declarations
class LuaVmManager;
class EntityExport;
class MsgRouterLua;
class LuaRpcApi;
class HotUpdateMgr;

// ── LuaVmManager ─────────────────────────────────────────────
// Manages Lua VM lifecycle: creation, destruction, script loading.
// Sandboxed: limits memory usage via lua_setallocfunc.
class LuaVmManager {
public:
    using Ptr = std::shared_ptr<LuaVmManager>;

    LuaVmManager();
    ~LuaVmManager();

    // Non-copyable
    LuaVmManager(const LuaVmManager&) = delete;
    LuaVmManager& operator=(const LuaVmManager&) = delete;

#ifdef SNC_LUA_AVAILABLE
    lua_State* L() const { return L_; }
#else
    void* L() const { return nullptr; }
#endif

    // Load and execute a Lua script file. Returns true on success.
    bool LoadScript(const std::string& path);

    // Call a global function by name with given args. Returns number of results.
    int CallGlobal(const std::string& funcName, int nargs = 0, int nresults = 0);

    // Get a global value as string. Empty string if not found or not a string.
    std::string GetGlobalString(const std::string& name);

    // Push a C++ pointer as userdata with a metatable.
    // caller owns the pointer; Lua just holds a reference.
    template <typename T>
    void PushEntity(T* ptr) {
#ifdef SNC_LUA_AVAILABLE
        void** p = static_cast<void**>(lua_newuserdatauv(L_, sizeof(void*)));
        *p = reinterpret_cast<void*>(ptr);
        luaL_getmetatable(L_, "snc.entity");
        lua_setmetatable(L_, -2);
#else
        (void)ptr;
#endif
    }

    // Check if a stack index holds an snc entity userdata, return raw pointer.
    template <typename T>
    T* CheckEntity(int idx) {
#ifdef SNC_LUA_AVAILABLE
        void** p = static_cast<void**>(lua_touserdata(L_, idx));
        if (p && lua_getmetatable(L_, idx)) {
            luaL_getmetatable(L_, "snc.entity");
            if (lua_rawequal(L_, -1, -2)) {
                lua_pop(L_, 2);
                return reinterpret_cast<T*>(*p);
            }
            lua_pop(L_, 2);
        }
        return nullptr;
#else
        (void)idx;
        return nullptr;
#endif
    }

private:
#ifdef SNC_LUA_AVAILABLE
    lua_State* L_ = nullptr;
    bool registered_ = false;
#else
    int dummy_ = 0; // keep object sized
#endif
};

// ── EntityExport ─────────────────────────────────────────────
// Wraps a C++ Entity* as Lua userdata with safety checks.
class EntityExport {
public:
    using Ptr = std::shared_ptr<EntityExport>;

#ifdef SNC_LUA_AVAILABLE
    explicit EntityExport(lua_State* L);
#else
    explicit EntityExport(void* L);
#endif
    ~EntityExport();

    // Register the "snc.entity" metatable in the given Lua state.
#ifdef SNC_LUA_AVAILABLE
    static void RegisterMetatable(lua_State* L);
#else
    static void RegisterMetatable(void* L);
#endif

    // Push an Entity* as userdata onto the stack.
    void PushEntity(void* entityPtr);

private:
#ifdef SNC_LUA_AVAILABLE
    lua_State* L_;
#else
    void* L_;
#endif
};

// Handler typedef for Lua-side routing.
#ifdef SNC_LUA_AVAILABLE
using LuaHandler = std::function<void(lua_State* L, int msgId,
                                       const uint8_t* payload, size_t payloadLen)>;
#else
using LuaHandler = std::function<void(void* L, int msgId,
                                       const uint8_t* payload, size_t payloadLen)>;
#endif

// ── MsgRouterLua ─────────────────────────────────────────────
// Lua-side message router. Routes messages to Lua handlers on a dedicated
// single-threaded worker. Serial execution, timeout-protected.
class MsgRouterLua {
public:
    using Ptr = std::shared_ptr<MsgRouterLua>;
    using Handler = LuaHandler;

    MsgRouterLua(LuaVmManager::Ptr vm);
    ~MsgRouterLua();

    // Register a handler for a specific msgId. All handlers for that msgId
    // are called sequentially on the Lua worker thread.
    void RegisterHandler(int msgId, Handler handler);
    void UnregisterHandler(int msgId);

    // Process a message: find handler and invoke on Lua thread.
    // Returns true if a handler was found and executed.
    bool Route(int msgId, const uint8_t* payload, size_t payloadLen);

    // Check if any handler is registered for msgId.
    bool HasHandler(int msgId) const;

    // Execute a Lua chunk (for hot-update or init).
    // Returns 0 on success, non-zero error code.
    int ExecuteChunk(const std::string& chunk);

private:
    LuaVmManager::Ptr vm_;
    mutable std::mutex mtx_;
    std::unordered_map<int, Handler> handlers_;
};

// ── HotUpdateMgr ─────────────────────────────────────────────
// Double-buffer hot-update manager for Lua scripts.
// Two buffers (A and B) alternate: old script continues serving while
// new script is loaded and validated in the other buffer.
// On validation failure, switches back to the old buffer.
class HotUpdateMgr {
public:
    using Ptr = std::shared_ptr<HotUpdateMgr>;

    HotUpdateMgr();
    ~HotUpdateMgr();

    // Load and validate a new script version.
    // version: incremental version number (caller increments).
    // path: script file path.
    // Returns true on success.
    bool LoadVersion(uint32_t version, const std::string& path);

    // Switch active buffer to the newly loaded version.
    // Must be called after LoadVersion succeeds.
    void Commit();

    // Rollback to previous version.
    void Rollback();

    // Get current active version.
    uint32_t ActiveVersion() const { return activeVersion_; }

    // Reload the currently active script (for testing).
    bool Reload();

private:
    struct Buffer {
        LuaVmManager::Ptr vm;
        MsgRouterLua::Ptr router;
        std::shared_ptr<class LuaRpcApi> rpcApi;
        uint32_t version = 0;
        bool loaded = false;
    };

    Buffer buffers_[2]; // double buffer
    int activeIdx_ = 0;
    int pendingIdx_ = 1; // the other buffer for pre-loading
    uint32_t activeVersion_ = 0;
    std::mutex mtx_;
};

} // namespace lua
} // namespace rpc
} // namespace snc
