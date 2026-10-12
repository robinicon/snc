#include "snc/rpc/lua/LuaRouterApi.h"

#ifdef SNC_LUA_AVAILABLE

#include <cstdio>
#include <chrono>

using namespace snc::rpc::lua;

LuaRouterApi::LuaRouterApi(std::shared_ptr<LuaVmManager> vm) : vm_(std::move(vm)) {}
LuaRouterApi::~LuaRouterApi() {}

void LuaRouterApi::RegisterHandler(int msgId, Handler handler) {
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_[msgId] = std::move(handler);
}

void LuaRouterApi::UnregisterHandler(int msgId) {
    std::lock_guard<std::mutex> lk(mtx_);
    handlers_.erase(msgId);
}

bool LuaRouterApi::Route(int msgId, const uint8_t* payload, size_t payloadLen) {
    if (!vm_ || !vm_->L()) return false;

    std::lock_guard<std::mutex> lk(mtx_);
    auto it = handlers_.find(msgId);
    if (it == handlers_.end() || !it->second) return false;

    auto handler = it->second;

    // Execute handler with timeout protection using a separate thread
    std::atomic<bool> done{false};
    int result = 0;

    std::thread t([&, handler, &done, &result]() {
        handler(vm_->L(), msgId, payload, payloadLen);
        done = true;
    });

    // Wait up to timeoutMs_
    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs_) {
            fprintf(stderr, "[LuaRouterApi] Handler for msg %d timed out (%d ms)\n",
                    msgId, timeoutMs_);
            t.detach();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    t.join();
    return true;
}

bool LuaRouterApi::HasHandler(int msgId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = handlers_.find(msgId);
    return it != handlers_.end() && it->second;
}

int LuaRouterApi::ExecuteChunk(const std::string& chunk) {
    if (!vm_ || !vm_->L()) return -1;
    return luaL_loadstring(vm_->L(), chunk.c_str());
}

int LuaRouterApi::ExecuteFile(const std::string& path) {
    if (!vm_ || !vm_->L()) return -1;
    return luaL_dofile(vm_->L(), path.c_str());
}

#else // !SNC_LUA_AVAILABLE

using namespace snc::rpc::lua;

LuaRouterApi::LuaRouterApi(std::shared_ptr<LuaVmManager>) {}
LuaRouterApi::~LuaRouterApi() {}
void LuaRouterApi::RegisterHandler(int, Handler) {}
void LuaRouterApi::UnregisterHandler(int) {}
bool LuaRouterApi::Route(int, const uint8_t*, size_t) { return false; }
bool LuaRouterApi::HasHandler(int) const { return false; }
int LuaRouterApi::ExecuteChunk(const std::string&) { return -1; }
int LuaRouterApi::ExecuteFile(const std::string&) { return -1; }

#endif // SNC_LUA_AVAILABLE
