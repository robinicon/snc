// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

#include <memory>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <mutex>
#include <cstring>
#include "snc/base/Time.h"
#include "snc/rpc/Common.h"
#include "snc/rpc/Entity.h"
#include "snc/core/Msg.h"
#include "snc/core/IConnection.h"
#include "snc/core/RpcHeader.h"

namespace snc {
namespace rpc {

using core::IConnection;
using core::Msg;
using core::RpcRespHeader;
using core::RpcReqHeader;

// RPC control message ID used as the Msg::msgId wrapper around RpcReq/RpcResp.
static constexpr int32_t kRpcMsgId = 0xFEED;

class PendingCall {
public:
    PendingCall(uint64_t seq, int32_t msgId, long long timeoutMs)
        : seq(seq), msgId(msgId), timeoutMs(timeoutMs),
          createdUs(base::NowMicroSeconds()) {}
    uint64_t seq;
    int32_t  msgId;
    long long timeoutMs;
    long long createdUs;
    std::function<void(RpcError, const uint8_t*, size_t)> reply;
};

class RpcSession : public std::enable_shared_from_this<RpcSession> {
public:
    using Ptr = std::shared_ptr<RpcSession>;
    using ReplyCallback = std::function<void(RpcError, const uint8_t*, size_t)>;

    explicit RpcSession(Entity::Ptr entity)
        : entity_(std::move(entity)), nextSeq_(1) {}

    // Send an RPC request. Returns the sequence number assigned to this call.
    // Serialises RpcReqHeader(seq, timeoutMs, pad=0) + user payload into a
    // Msg (msgId=kRpcMsgId) and sends it over the underlying TCP connection.
    uint64_t Call(int32_t msgId, const char* payload, size_t len, ReplyCallback reply) {
        uint64_t seq = nextSeq_++;
        auto pending = std::make_shared<PendingCall>(seq, msgId, 5000);
        pending->reply = std::move(reply);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pendingCalls_[seq] = pending;
        }

        // Build the RPC request wire format: RpcReqHeader (13 bytes) + payload.
        // Msg::HEADER_SIZE(8) + RpcReqHeader::SIZE(13) + user payload.
        size_t totalLen = Msg::HEADER_SIZE + RpcReqHeader::SIZE + len;
        std::string raw(totalLen, '\0');
        // Write Msg header
        core::Msg::Header h{static_cast<int32_t>(kRpcMsgId), static_cast<int32_t>(RpcReqHeader::SIZE + len)};
        std::memcpy(raw.data(), &h, sizeof(h));
        // Write RpcReqHeader
        uint8_t* buf = reinterpret_cast<uint8_t*>(&raw[Msg::HEADER_SIZE]);
        RpcReqHeader reqHdr;
        reqHdr.seq      = seq;
        reqHdr.timeoutMs = static_cast<int32_t>(pending->timeoutMs);
        reqHdr.pad      = 0;
        reqHdr.WriteTo(buf);
        if (len > 0 && payload) {
            std::memcpy(buf + RpcReqHeader::SIZE, payload, len);
        }
        auto msg = Msg::FromBuffer(raw.data(), raw.size());
        // Send via the underlying connection owned by the Entity.
        auto conn = entity_ ? entity_->GetConn() : nullptr;
        if (conn) {
            conn->Send(msg);
        } else {
            // Connection not yet available — notify the caller instead of leaking.
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = pendingCalls_.find(seq);
            if (it != pendingCalls_.end()) {
                auto cb = std::move(it->second->reply);
                pendingCalls_.erase(it);
                if (cb) cb(RpcError::UNKNOWN, nullptr, 0);
            }
        }
        return seq;
    }

    // Called by the connection's message callback when RPC response data arrives.
    void OnData(const uint8_t* data, size_t len) {
        if (len < RpcRespHeader::SIZE) return;
        RpcRespHeader hdr;
        hdr.ReadFrom(data);
        ReplyCallback cb;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = pendingCalls_.find(hdr.seq);
            if (it == pendingCalls_.end()) return;
            cb = std::move(it->second->reply);
            pendingCalls_.erase(it);
        }
        if (cb) {
            const uint8_t* payload = data + RpcRespHeader::SIZE;
            size_t payloadLen = (len > RpcRespHeader::SIZE) ? (len - RpcRespHeader::SIZE) : 0;
            if (static_cast<int32_t>(hdr.errCode) == static_cast<int32_t>(RpcError::OK)) {
                cb(RpcError::OK, payload, payloadLen);
            } else {
                cb(static_cast<RpcError>(hdr.errCode), nullptr, 0);
            }
        }
    }

    void ClearAllPending() {
        std::lock_guard<std::mutex> lk(mtx_);
        pendingCalls_.clear();
    }

    void PruneExpired() {
        long long now = base::NowMicroSeconds();
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto it = pendingCalls_.begin(); it != pendingCalls_.end(); ) {
            auto& p = it->second;
            long long elapsed = (now - p->createdUs) / 1000;
            if (elapsed > p->timeoutMs) {
                auto cb = std::move(p->reply);
                it = pendingCalls_.erase(it);
                if (cb) cb(RpcError::TIMEOUT, nullptr, 0);
            } else {
                ++it;
            }
        }
    }

    void SetConn(IConnection::Ptr conn) {
        if (entity_) entity_->SetConn(std::move(conn));
    }

    Entity::Ptr GetEntity() const { return entity_; }

private:
    Entity::Ptr entity_;
    uint64_t nextSeq_;
    std::mutex mtx_;
    std::unordered_map<uint64_t, std::shared_ptr<PendingCall>> pendingCalls_;
};

} // namespace rpc
} // namespace snc
