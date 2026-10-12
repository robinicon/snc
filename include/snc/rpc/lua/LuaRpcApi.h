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

// Lua callback type for async RPC responses.
using RpcCallback = std::function<void(int err, const uint8_t* payload, size_t len)>;

// ── LuaRpcApi ────────────────────────────────────────────────
// Exposes `snc.rpc.*` functions to Lua scripts.
// All operations are asynchronous — no blocking calls allowed.
//
// Lua API surface:
//   snc.rpc.call(serviceId, msgId, payloadBytes, callback)
//     callback(err, responsePayload, responseLen)
//
//   snc.rpc.notify(serviceId, msgId, payloadBytes)
//     fire-and-forget, no callback
//
//   snc.rpc.register(msgId, handler)
//     handler(payloadBytes, replyFn)
//     replyFn(err, responsePayload, responseLen)
//
//   snc.rpc.version() -> integer
//
// The C++ side must call SetSender() and SetResponseHandler() to wire
// the actual network I/O.
class LuaRpcApi {
public:
    using Ptr = std::shared_ptr<LuaRpcApi>;

    explicit LuaRpcApi(std::shared_ptr<LuaVmManager> vm);
    ~LuaRpcApi();

    // Register this module under the "snc" global table in the given Lua state.
#ifdef SNC_LUA_AVAILABLE
    void Register(lua_State* L);
#else
    void Register(void* L);
#endif

    // Wire the outgoing sender. Called by the service initialization code.
    // fn(msgId, payload, len) -> true if sent, false otherwise
    using SendFn = std::function<bool(int msgId, const uint8_t* payload, size_t len)>;
    void SetSender(SendFn fn);

    // Wire the incoming response handler. Called by the service initialization code.
    // Called when an RPC response arrives; forwards to pending Lua callbacks.
    using ResponseFn = std::function<void(int seq, int err, const uint8_t* payload, size_t len)>;
    void SetResponseHandler(ResponseFn fn);

    // Get the underlying Lua state.
#ifdef SNC_LUA_AVAILABLE
    lua_State* L() const { return vm_ ? vm_->L() : nullptr; }
#else
    void* L() const { return nullptr; }
#endif

private:
    // Lua C-function stubs (static methods bound to Lua registry)
#ifdef SNC_LUA_AVAILABLE
    static int lcall(lua_State* L);
    static int lnotify(lua_State* L);
    static int lregister(lua_State* L);
    static int lversion(lua_State* L);
#else
    static int lcall(void*);
    static int lnotify(void*);
    static int lregister(void*);
    static int lversion(void*);
#endif

    std::shared_ptr<LuaVmManager> vm_;
    SendFn sendFn_;
    ResponseFn responseFn_;
    mutable std::mutex mtx_;
    int nextSeq_ = 1;
    std::unordered_map<int, RpcCallback> pendingCallbacks_;
};

} // namespace lua
} // namespace rpc
} // namespace snc
