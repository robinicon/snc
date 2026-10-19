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

class LuaVmManager;

// Handler type alias
#ifdef SNC_LUA_AVAILABLE
using RouterHandler = std::function<void(lua_State* L, int msgId,
                                          const uint8_t* payload, size_t payloadLen)>;
#else
using RouterHandler = std::function<void(void* L, int msgId,
                                          const uint8_t* payload, size_t payloadLen)>;
#endif

// ── LuaRouterApi ─────────────────────────────────────────────
// C++ API for registering Lua handlers that are invoked by MsgRouterLua.
// This is the bridge between the C++ routing layer and Lua scripts.
//
// Usage in C++ service code:
//   LuaRouterApi api(vmManager);
//   api.RegisterHandler(0x1001, [](lua_State* L, int msgId,
//                                  const uint8_t* payload, size_t len) {
//       // ... call Lua function ...
//   });
//
// The handler is executed on the Lua worker thread (single-threaded, serial).
class LuaRouterApi {
public:
    using Handler = RouterHandler;

    explicit LuaRouterApi(std::shared_ptr<LuaVmManager> vm);
    ~LuaRouterApi();

    // Register a handler for a specific message ID.
    void RegisterHandler(int msgId, Handler handler);

    // Unregister a handler.
    void UnregisterHandler(int msgId);

    // Route a message to the registered Lua handler.
    // Returns true if a handler was found and executed within timeout.
    bool Route(int msgId, const uint8_t* payload, size_t payloadLen);

    // Check if a handler is registered for msgId.
    bool HasHandler(int msgId) const;

    // Execute an arbitrary Lua chunk (for hot-update).
    // Returns 0 on success, non-zero error string on stack.
    int ExecuteChunk(const std::string& chunk);

    // Execute a Lua script file.
    // Returns 0 on success.
    int ExecuteFile(const std::string& path);

    // Set execution timeout (ms). Default 100ms.
    void SetTimeoutMs(int ms) { timeoutMs_ = ms; }
    int TimeoutMs() const { return timeoutMs_; }

private:
    std::shared_ptr<LuaVmManager> vm_;
    mutable std::mutex mtx_;
    std::unordered_map<int, Handler> handlers_;
    int timeoutMs_ = 100;
};

} // namespace lua
} // namespace rpc
} // namespace snc
