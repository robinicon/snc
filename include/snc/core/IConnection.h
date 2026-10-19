// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>
#include "snc/core/Msg.h"
#include "snc/core/EntityHandle.h"
#include "snc/core/DisconnectReason.h"

namespace snc {
namespace core {

class WorkerPool;

class IConnection : public std::enable_shared_from_this<IConnection> {
public:
    using Ptr = std::shared_ptr<IConnection>;
    using MessageCallback = std::function<void(EntityHandle<IConnection> conn, Msg::Ptr msg)>;
    using CloseCallback = std::function<void(Ptr conn, DisconnectReason reason)>;
    using EnqueueFn = std::function<bool(Msg::Ptr)>;
    // HeartbeatCallback notifies the owner when the connection state changes (e.g. timeout).
    using HeartbeatCallback = std::function<void(DisconnectReason reason)>;
    using PruneExpiredFn = std::function<void()>;

    virtual ~IConnection() {}
    virtual int Fd() const = 0;
    virtual bool Send(Msg::Ptr msg) = 0;
    virtual bool Close() = 0;
    virtual void SetMessageCallback(MessageCallback cb) = 0;
    virtual void SetCloseCallback(CloseCallback cb) = 0;
    virtual void SetEnqueueFn(EnqueueFn fn) = 0;
    virtual void DispatchToQueue(Msg::Ptr msg) = 0;
    virtual void SetHeartbeatCallback(HeartbeatCallback cb) {}
    virtual void SetPruneExpired(PruneExpiredFn fn) {}
    virtual EntityHandle<IConnection> GetConnHandle() { return EntityHandle<IConnection>{}; }

    // Phase D – WorkerPool 直接分发（可选，子类按需覆盖）
    // 注意：WorkerDispatchFn 在类体外定义，此处用完整类型。
    virtual void SetWorkerDispatchFn(
        std::function<void(EntityHandle<IConnection>, Msg::Ptr)> /*fn*/,
        WorkerPool* /*pool*/) {}
    virtual WorkerPool* GetWorkerPool() const { return nullptr; }

    // 心跳管理钩子（由事件循环周期性调用；各后端可自由覆盖）
    virtual void StartHeartbeat(long long /*intervalMs*/, long long /*pongTimeoutMs*/, int /*maxMissed*/) {}
    virtual void ProcessHeartbeatTick() {}
    virtual void InvokePruneExpired() {}

    // Phase C3 – 背压查询
    virtual bool IsBackpressuring() const { return false; }
};

// WorkerDispatchFn must be defined AFTER the IConnection class body so that
// EntityHandle<IConnection> can reference the complete type (MSVC requirement).
using WorkerDispatchFn = std::function<void(EntityHandle<IConnection>, Msg::Ptr)>;

} // namespace core
} // namespace snc
