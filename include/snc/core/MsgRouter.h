// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include "snc/core/Msg.h"
#include "snc/core/IConnection.h"

namespace snc {
namespace core {

using Handler = std::function<void(IConnection::Ptr, Msg::Ptr)>;

class MsgRouter {
public:
    void RegisterHandler(int msgId, Handler handler) {
        std::lock_guard<std::mutex> lk(mtx_);
        handlers_[msgId] = std::move(handler);
    }

    void UnregisterHandler(int msgId) {
        std::lock_guard<std::mutex> lk(mtx_);
        handlers_.erase(msgId);
    }

    bool Route(IConnection::Ptr conn, Msg::Ptr msg) {
        if (!msg) return false;
        int id = msg->MsgId();
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = handlers_.find(id);
        if (it != handlers_.end() && it->second) {
            it->second(conn, msg);
            return true;
        }
        return false;
    }

    size_t HandlerCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return handlers_.size();
    }

private:
    mutable std::mutex mtx_;
    std::map<int, Handler> handlers_;
};

} // namespace core
} // namespace snc
