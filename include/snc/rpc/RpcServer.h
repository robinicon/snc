#pragma once

/// @file RpcServer.h
/// @brief Lightweight RPC server with dynamic handler registration.
///
/// RpcServer provides a simple request-routing layer:
///   1. Register handlers by message ID via RegisterHandler().
///   2. Call Route() to dispatch incoming messages to the matching handler.
///   3. The mutex is released before invoking the handler to avoid re-entrant deadlocks.
///
/// Usage:
///   auto server = std::make_shared<RpcServer>();
///   server->RegisterHandler(MSG_ID_LOGIN, [](conn, session, msg, respond) { ... });
///   bool handled = server->Route(conn, session, msg);

#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include "snc/core/IConnection.h"
#include "snc/core/Msg.h"
#include "snc/core/RpcHeader.h"
#include "snc/rpc/RpcSession.h"
#include "snc/rpc/Common.h"

namespace snc {
namespace rpc {

using core::IConnection;
using core::Msg;
using core::RpcRespHeader;

/// Handler signature for RPC requests.
/// @param conn        The connection that received the request.
/// @param session     The RpcSession associated with the connection.
/// @param msg         The parsed message (consumed by the handler).
/// @param respond     Callback to send the response back to the client.
using RpcHandler = std::function<void(IConnection::Ptr, RpcSession::Ptr, Msg::Ptr,
                                       std::function<void(RpcError, const uint8_t*, size_t)>)>;

/// Lightweight RPC server with dynamic handler registration.
///
/// Thread-safe: concurrent RegisterHandler and Route calls are protected by a mutex.
/// The mutex is released before calling the handler to avoid deadlock if the
/// handler re-enters RegisterHandler or HandlerCount.
class RpcServer {
public:
    using Ptr = std::shared_ptr<RpcServer>;

    /// Register a handler for the given message ID.
    /// Thread-safe; can be called concurrently with Route().
    void RegisterHandler(int32_t msgId, RpcHandler handler) {
        std::lock_guard<std::mutex> lk(mtx_);
        handlers_[msgId] = std::move(handler);
    }

    /// Route msg to the registered handler. Returns true if a handler was found.
    /// Thread-safe: the mutex is released before calling the handler to avoid
    /// deadlock if the handler re-enters RegisterHandler/HandlerCount.
    bool Route(IConnection::Ptr conn, RpcSession::Ptr session, Msg::Ptr msg) {
        if (!msg) return false;
        int32_t id = msg->MsgId();
        RpcHandler handler;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = handlers_.find(id);
            if (it == handlers_.end() || !it->second) return false;
            handler = it->second;
        }
        handler(conn, session, std::move(msg),
            [conn](RpcError err, const uint8_t* payload, size_t payloadLen) {
                if (!conn) return;
                // Extract the request seq from the incoming RpcReqHeader embedded in payload.
                // The request message format is: RpcReqHeader (13 bytes) + body.
                int32_t reqSeq = 0;
                if (payload && payloadLen >= RpcReqHeader::SIZE) {
                    RpcReqHeader reqHdr{};
                    reqHdr.ReadFrom(payload);
                    reqSeq = reqHdr.seq;
                }
                std::vector<uint8_t> respBuf(RpcRespHeader::SIZE + payloadLen);
                RpcRespHeader hdr{};
                hdr.seq      = reqSeq;
                hdr.errCode  = static_cast<int32_t>(err);
                hdr.resultLen = static_cast<int32_t>(payloadLen);
                hdr.WriteTo(respBuf.data());
                if (payloadLen > 0 && payload) {
                    std::memcpy(respBuf.data() + RpcRespHeader::SIZE, payload, payloadLen);
                }
                conn->Send(Msg::Create(0, reinterpret_cast<const char*>(respBuf.data()),
                                       respBuf.size()));
            });
        return true;
    }

    /// Get the number of registered handlers. Thread-safe.
    size_t HandlerCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return handlers_.size();
    }

private:
    mutable std::mutex mtx_;
    std::map<int32_t, RpcHandler> handlers_;
};

} // namespace rpc
} // namespace snc
