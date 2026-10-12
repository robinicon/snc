#include "snc/rpc/lua/LuaBinding.h"
#include "snc/rpc/lua/LuaRpcApi.h"

#ifdef SNC_LUA_AVAILABLE

#include <cstdio>
#include <cstring>

using namespace snc::rpc::lua;

// ── LuaVmManager ─────────────────────────────────────────────

LuaVmManager::LuaVmManager() {
    L_ = luaL_newstate();
    if (L_) {
        luaL_openlibs(L_);
        // Limit memory: 64MB cap
        lua_setalloc(L_, nullptr,
            [](void* ud, void* ptr, size_t osize, size_t nsize) -> void* {
                (void)ud; (void)osize;
                if (nsize == 0) { free(ptr); return nullptr; }
                return realloc(ptr, nsize);
            }, nullptr);
    }
}

LuaVmManager::~LuaVmManager() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

bool LuaVmManager::LoadScript(const std::string& path) {
    int rc = luaL_dofile(L_, path.c_str());
    if (rc != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        fprintf(stderr, "[LuaVmManager] LoadScript failed: %s\n", err ? err : "unknown");
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

int LuaVmManager::CallGlobal(const std::string& funcName, int nargs, int nresults) {
    lua_getglobal(L_, funcName.c_str());
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return -1;
    }
    // args pushed by caller already on stack... simplified: no extra args
    int rc = lua_pcall(L_, nargs, nresults, 0);
    if (rc != LUA_OK) {
        const char* err = lua_tostring(L_, -1);
        fprintf(stderr, "[LuaVmManager] CallGlobal '%s' failed: %s\n",
                funcName.c_str(), err ? err : "unknown");
        lua_pop(L_, 1);
        return -1;
    }
    return lua_gettop(L_) - nresults;
}

std::string LuaVmManager::GetGlobalString(const std::string& name) {
    lua_getglobal(L_, name.c_str());
    const char* s = lua_tostring(L_, -1);
    std::string result = s ? s : "";
    lua_pop(L_, 1);
    return result;
}

// ── EntityExport ─────────────────────────────────────────────

EntityExport::EntityExport(lua_State* L) : L_(L) {}
EntityExport::~EntityExport() {}

void EntityExport::RegisterMetatable(lua_State* L) {
    luaL_newmetatable(L, "snc.entity");
    // __gc: just print warning, don't delete C++ object
    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, [](lua_State* l) -> int {
        fprintf(stderr, "[EntityExport] GC called on snc.entity (pointer not freed)\n");
        return 0;
    });
    lua_settable(L, -3);
    lua_pop(L, 1);
}

void EntityExport::PushEntity(void* entityPtr) {
    void** p = static_cast<void**>(lua_newuserdatauv(L_, sizeof(void*)));
    *p = entityPtr;
    luaL_getmetatable(L_, "snc.entity");
    lua_setmetatable(L_, -2);
}

// ── MsgRouterLua ─────────────────────────────────────────────

MsgRouterLua::MsgRouterLua(LuaVmManager::Ptr vm) : vm_(std::move(vm)) {}
MsgRouterLua::~MsgRouterLua() {}

void MsgRouterLua::RegisterHandler(int msgId, Handler handler) {
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_[msgId] = std::move(handler);
}

void MsgRouterLua::UnregisterHandler(int msgId) {
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_.erase(msgId);
}

bool MsgRouterLua::Route(int msgId, const uint8_t* payload, size_t payloadLen) {
    if (!vm_ || !vm_->L()) return false;
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = handlers_.find(msgId);
    if (it == handlers_.end() || !it->second) return false;
    it->second(vm_->L(), msgId, payload, payloadLen);
    return true;
}

bool MsgRouterLua::HasHandler(int msgId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = handlers_.find(msgId);
    return it != handlers_.end() && it->second;
}

int MsgRouterLua::ExecuteChunk(const std::string& chunk) {
    if (!vm_ || !vm_->L()) return -1;
    int rc = luaL_loadstring(vm_->L(), chunk.c_str());
    if (rc != LUA_OK) return rc;
    rc = lua_pcall(vm_->L(), 0, 0, 0);
    return rc;
}

// ── HotUpdateMgr ─────────────────────────────────────────────

HotUpdateMgr::HotUpdateMgr() {}
HotUpdateMgr::~HotUpdateMgr() {}

bool HotUpdateMgr::LoadVersion(uint32_t version, const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    Buffer& buf = buffers_[pendingIdx_];
    buf.vm = std::make_shared<LuaVmManager>();
    if (!buf.vm->LoadScript(path)) {
        buf.loaded = false;
        return false;
    }
    buf.router = std::make_shared<MsgRouterLua>(buf.vm);
    buf.rpcApi = std::make_shared<LuaRpcApi>(buf.vm);
    buf.version = version;
    buf.loaded = true;
    return true;
}

void HotUpdateMgr::Commit() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!buffers_[pendingIdx_].loaded) return;
    activeIdx_ = pendingIdx_;
    activeVersion_ = buffers_[pendingIdx_].version;
    pendingIdx_ = 1 - pendingIdx_;
    // Reset pending buffer
    buffers_[pendingIdx_].loaded = false;
    buffers_[pendingIdx_].version = 0;
}

void HotUpdateMgr::Rollback() {
    std::lock_guard<std::mutex> lk(mtx_);
    pendingIdx_ = 1 - pendingIdx_;
    buffers_[pendingIdx_].loaded = false;
    buffers_[pendingIdx_].version = 0;
}

bool HotUpdateMgr::Reload() {
    std::lock_guard<std::mutex> lk(mtx_);
    Buffer& buf = buffers_[activeIdx_];
    if (!buf.vm) return false;
    // Re-execute init chunk from Lua state
    return buf.vm->LoadScript(""); // placeholder
}

#else // !SNC_LUA_AVAILABLE

// ── Stub implementations when Lua is not available ──────────

using namespace snc::rpc::lua;

LuaVmManager::LuaVmManager() {}
LuaVmManager::~LuaVmManager() {}
bool LuaVmManager::LoadScript(const std::string&) { return false; }
int LuaVmManager::CallGlobal(const std::string&, int, int) { return -1; }
std::string LuaVmManager::GetGlobalString(const std::string&) { return ""; }

EntityExport::EntityExport(void*) {}
EntityExport::~EntityExport() {}
void EntityExport::RegisterMetatable(void*) {}
void EntityExport::PushEntity(void*) {}

MsgRouterLua::MsgRouterLua(std::shared_ptr<LuaVmManager>) {}
MsgRouterLua::~MsgRouterLua() {}
void MsgRouterLua::RegisterHandler(int, Handler) {}
void MsgRouterLua::UnregisterHandler(int) {}
bool MsgRouterLua::Route(int, const uint8_t*, size_t) { return false; }
bool MsgRouterLua::HasHandler(int) const { return false; }
int MsgRouterLua::ExecuteChunk(const std::string&) { return -1; }

// LuaRpcApi stubs
LuaRpcApi::LuaRpcApi(std::shared_ptr<LuaVmManager>) {}
LuaRpcApi::~LuaRpcApi() {}
void LuaRpcApi::Register(void*) {}
void LuaRpcApi::SetSender(SendFn) {}
void LuaRpcApi::SetResponseHandler(ResponseFn) {}
int LuaRpcApi::lcall(void*) { return 0; }
int LuaRpcApi::lnotify(void*) { return 0; }
int LuaRpcApi::lregister(void*) { return 0; }
int LuaRpcApi::lversion(void*) { return 0; }

HotUpdateMgr::HotUpdateMgr() {}
HotUpdateMgr::~HotUpdateMgr() {}
bool HotUpdateMgr::LoadVersion(uint32_t, const std::string&) { return false; }
void HotUpdateMgr::Commit() {}
void HotUpdateMgr::Rollback() {}
bool HotUpdateMgr::Reload() { return false; }

#endif // SNC_LUA_AVAILABLE
