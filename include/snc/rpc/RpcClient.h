// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

/// @file RpcClient.h
/// @brief RPC client with automatic reconnect and exponential backoff.
///
/// RpcClient wraps an RpcSession and handles connection lifecycle:
///   - On disconnect, schedules a retry via the EventLoop timer.
///   - Retry delay doubles each attempt (baseDelay * 2^attempt) up to maxDelayMs.
///   - Reconnection stops after maxRetries attempts or when explicitly stopped.
///
/// Usage:
///   auto client = std::make_shared<RpcClient>(session, &loop, factory, opts);
///   client->Call(msgId, payload, len, replyCallback);

#include <memory>
#include <functional>
#include "snc/base/Log.h"
#include "snc/rpc/RpcSession.h"
#include "snc/rpc/Common.h"
#include "snc/core/IConnection.h"
#include "snc/core/DisconnectReason.h"
#include "snc/core/IEventLoop.h"

namespace snc {
namespace rpc {

/// RPC client with automatic reconnect (exponential backoff).
///
/// Wraps an RpcSession. When the underlying connection drops (detected via
/// onDisconnect callback), the client schedules a retry via the EventLoop
/// timer manager. Retry delay doubles each attempt up to maxDelayMs.
/// Reconnection stops after maxRetries attempts or when the session is
/// explicitly destroyed.
class RpcClient : public std::enable_shared_from_this<RpcClient> {
public:
    using Ptr       = std::shared_ptr<RpcClient>;
    using Conn      = core::IConnection;
    /// Reply callback: invoked when a response arrives or an error occurs.
    using ReplyCallback = std::function<void(RpcError, const uint8_t*, size_t)>;

    /// Factory lambda: creates a fresh IConnection::Ptr for reconnection.
    using ConnFactory = std::function<Conn::Ptr()>;

    /// Options for automatic reconnect behavior.
    struct ReconnectOptions {
        int     maxRetries    = 5;   ///< -1 = unlimited retries; 0 = no retry
        long long baseDelayMs = 500; ///< initial delay in milliseconds
        long long maxDelayMs  = 30000; ///< cap on retry delay
    };

    /// Backward-compatible constructor: session-only, no auto-reconnect.
    explicit RpcClient(RpcSession::Ptr session)
        : session_(std::move(session))
    {
        UpdateConnCallback();
    }

    /// Full constructor with auto-reconnect and event loop binding.
    RpcClient(RpcSession::Ptr session, core::IEventLoop* el, ConnFactory factory,
              const ReconnectOptions& opts = ReconnectOptions{})
        : session_(std::move(session))
        , factory_(std::move(factory))
        , eventLoop_(el)
        , opts_(opts)
    {
        UpdateConnCallback();
    }

    ~RpcClient() {
        stopping_ = true;
        if (timerId_ > 0 && eventLoop_) {
            eventLoop_->TimerMgr().CancelTimer(timerId_);
        }
    }

    /// Send an RPC request through the current session.
    /// Returns the sequence number for tracking the call.
    uint64_t Call(int32_t msgId, const char* payload, size_t len, ReplyCallback reply) {
        return session_->Call(msgId, payload, len, std::move(reply));
    }

    /// Force close the underlying connection. Triggers reconnect on the next Call().
    void Disconnect() {
        Conn::Ptr c;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            c = conn_;
        }
        if (c) c->Close();
    }

    /// Called externally when a new connection is established (e.g. by Acceptor).
    void SetConnection(Conn::Ptr conn) {
        std::lock_guard<std::mutex> lk(mtx_);
        conn_ = std::move(conn);
        session_->SetConn(conn_);
        UpdateConnCallback();
        attempt_ = 0;
    }

    /// Called when the RpcSession is set separately from construction.
    void SetSession(RpcSession::Ptr session) {
        session_ = std::move(session);
    }

private:
    void UpdateConnCallback() {
        if (!conn_) return;
        auto self = shared_from_this();
        conn_->SetCloseCallback([self](Conn::Ptr, core::DisconnectReason reason) {
            self->OnDisconnect(reason);
        });
    }

    void OnDisconnect(core::DisconnectReason reason) {
        (void)reason;
        if (stopping_) return;

        std::lock_guard<std::mutex> lk(mtx_);

        if (opts_.maxRetries >= 0 && static_cast<int>(attempt_.load()) >= opts_.maxRetries) {
            SNC_LOG_ERROR("rpc", "RpcClient: max retries (%d) reached, giving up", opts_.maxRetries);
            return;
        }
        ++attempt_;

        long long delay = opts_.baseDelayMs;
        for (int i = 1; i < attempt_; ++i) {
            delay *= 2;
            if (delay > opts_.maxDelayMs) { delay = opts_.maxDelayMs; break; }
        }

        SNC_LOG_INFO("rpc", "RpcClient: reconnect scheduled in %lld ms (attempt %d)",
                     delay, static_cast<int>(attempt_.load()));

        if (eventLoop_ && factory_) {
            auto self = shared_from_this();
            auto el = eventLoop_;          // capture raw ptr (safe: el outlives this)
            timerId_ = el->TimerMgr().AddTimer(delay, [self, el] {
                self->TryReconnect(el);
            });
        }
    }

    void TryReconnect(core::IEventLoop* el) {
        std::lock_guard<std::mutex> lk(mtx_);
        timerId_ = 0;
        if (stopping_ || !factory_) return;

        auto newConn = factory_();
        if (!newConn) {
            SNC_LOG_WARN("rpc", "RpcClient: reconnect factory returned null");
            return;
        }
        conn_ = newConn;
        session_->SetConn(conn_);
        UpdateConnCallback();
        el->AddConnection(conn_);
        attempt_ = 0;
        SNC_LOG_INFO("rpc", "RpcClient: reconnected successfully");
    }

    ConnFactory         factory_;
    ReconnectOptions    opts_;
    Conn::Ptr           conn_;
    RpcSession::Ptr     session_;
    core::IEventLoop*   eventLoop_ = nullptr;
    int64_t             timerId_ = 0;
    std::atomic<int>    attempt_{0};
    std::atomic<bool>   stopping_{false};
    std::mutex          mtx_;
};

} // namespace rpc
} // namespace snc
