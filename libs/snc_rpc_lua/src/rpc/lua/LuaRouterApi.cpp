// LuaRouterApi.cpp — Stub implementation (Lua not available)

#include "snc/rpc/lua/LuaRouterApi.h"

namespace snc {
namespace rpc {
namespace lua {

LuaRouterApi::LuaRouterApi(std::shared_ptr<LuaVmManager> /*vm*/) : timeoutMs_(100) {}
LuaRouterApi::~LuaRouterApi() = default;

void LuaRouterApi::RegisterHandler(int /*msgId*/, Handler /*handler*/) {}
void LuaRouterApi::UnregisterHandler(int /*msgId*/) {}
bool LuaRouterApi::Route(int /*msgId*/, const uint8_t* /*payload*/, size_t /*payloadLen*/) {
    return false;
}
bool LuaRouterApi::HasHandler(int /*msgId*/) const { return false; }
int LuaRouterApi::ExecuteChunk(const std::string& /*chunk*/) { return -1; }
int LuaRouterApi::ExecuteFile(const std::string& /*path*/) { return -1; }

} // namespace lua
} // namespace rpc
} // namespace snc
