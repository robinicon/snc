// SPDX-License-Identifier: MIT
// LuaRpcApi.cpp — Stub implementation (Lua not available)

#include "snc/rpc/lua/LuaRpcApi.h"

namespace snc {
namespace rpc {
namespace lua {

LuaRpcApi::LuaRpcApi(std::shared_ptr<LuaVmManager> /*vm*/) {}
LuaRpcApi::~LuaRpcApi() = default;

void LuaRpcApi::Register(void* /*L*/) {}
void LuaRpcApi::SetSender(SendFn /*fn*/) {}
void LuaRpcApi::SetResponseHandler(ResponseFn /*fn*/) {}

int LuaRpcApi::lcall(void*) { return 0; }
int LuaRpcApi::lnotify(void*) { return 0; }
int LuaRpcApi::lregister(void*) { return 0; }
int LuaRpcApi::lversion(void*) { return 0; }

} // namespace lua
} // namespace rpc
} // namespace snc
