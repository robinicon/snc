// LuaBinding.cpp — Stub implementation (Lua not available)
// When Lua is found, this file is replaced by the real implementation.

#include "snc/rpc/lua/LuaBinding.h"

namespace snc {
namespace rpc {
namespace lua {

// LuaVmManager stub
LuaVmManager::LuaVmManager() = default;
LuaVmManager::~LuaVmManager() = default;

bool LuaVmManager::LoadScript(const std::string& /*path*/) {
    return false;
}

int LuaVmManager::CallGlobal(const std::string& /*funcName*/, int /*nargs*/, int /*nresults*/) {
    return -1;
}

std::string LuaVmManager::GetGlobalString(const std::string& /*name*/) {
    return {};
}

// EntityExport stub
EntityExport::EntityExport(void* /*L*/) : L_(nullptr) {}
EntityExport::~EntityExport() = default;
void EntityExport::RegisterMetatable(void* /*L*/) {}
void EntityExport::PushEntity(void* /*entityPtr*/) {}

// MsgRouterLua stub
MsgRouterLua::MsgRouterLua(LuaVmManager::Ptr /*vm*/) {}
MsgRouterLua::~MsgRouterLua() = default;
void MsgRouterLua::RegisterHandler(int /*msgId*/, Handler /*handler*/) {}
void MsgRouterLua::UnregisterHandler(int /*msgId*/) {}
bool MsgRouterLua::Route(int /*msgId*/, const uint8_t* /*payload*/, size_t /*payloadLen*/) {
    return false;
}
bool MsgRouterLua::HasHandler(int /*msgId*/) const { return false; }
int MsgRouterLua::ExecuteChunk(const std::string& /*chunk*/) { return -1; }

// HotUpdateMgr stub
HotUpdateMgr::HotUpdateMgr() : activeVersion_(0), activeIdx_(0), pendingIdx_(1) {}
HotUpdateMgr::~HotUpdateMgr() = default;
bool HotUpdateMgr::LoadVersion(uint32_t /*version*/, const std::string& /*path*/) { return false; }
void HotUpdateMgr::Commit() {}
void HotUpdateMgr::Rollback() {}
bool HotUpdateMgr::Reload() { return false; }

} // namespace lua
} // namespace rpc
} // namespace snc
