// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

#include "snc/base/Common.h"
#include "snc/core/IEventLoop.h"
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <thread>
#include <vector>
#include <memory>

namespace snc {
namespace core {

class MessageQueue;
class WorkerPool;
class IConnection;

// ── Windows IOCP / select ────────────────────────────────────────
#ifdef SNC_PLATFORM_WINDOWS

class EventLoopIOCP : public IEventLoop {
public:
    EventLoopIOCP();
    ~EventLoopIOCP() override;

    void Run() override;
    void Stop() override;
    void AddConnection(IConnection::Ptr conn) override;
    void RemoveConnection(IConnection::Ptr conn) override;
    void Wakeup() override;
    bool IsRunning() const override { return running_.load(std::memory_order_acquire); }
    TimerManager& TimerMgr() override { return timerMgr_; }
    void SetMessageQueue(MessageQueue* q) override { messageQueue_ = q; }
    void SetWorkerPool(WorkerPool* pool) override { workerPool_ = pool; }
    void SetAcceptSock(SocketWrap sock) override { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) override { acceptCallback_ = std::move(cb); }
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) override { messageDispatchFn_ = std::move(fn); }
    void ProcessHeartbeatTickAll() override;
    void ProcessRpcPruneAll() override;

private:
    std::unordered_map<int, IConnection::Ptr> connections_;
    std::mutex connMutex_;
    std::atomic<bool> running_{false};
    std::thread::id thread_id_;
    TimerManager timerMgr_;
    MessageQueue* messageQueue_ = nullptr;
    WorkerPool* workerPool_ = nullptr;
    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};

#endif // SNC_PLATFORM_WINDOWS

// ── Linux epoll ──────────────────────────────────────────────────
#ifdef SNC_PLATFORM_LINUX

class EventLoopEpoll : public IEventLoop {
public:
    EventLoopEpoll();
    ~EventLoopEpoll() override;

    void Run() override;
    void Stop() override;
    void AddConnection(IConnection::Ptr conn) override;
    void RemoveConnection(IConnection::Ptr conn) override;
    void Wakeup() override;
    bool IsRunning() const override { return running_.load(std::memory_order_acquire); }
    TimerManager& TimerMgr() override { return timerMgr_; }
    void SetMessageQueue(MessageQueue* q) override { messageQueue_ = q; }
    void SetWorkerPool(WorkerPool* pool) override { workerPool_ = pool; }
    void SetAcceptSock(SocketWrap sock) override { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) override { acceptCallback_ = std::move(cb); }
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) override { messageDispatchFn_ = std::move(fn); }
    void ProcessHeartbeatTickAll() override;
    void ProcessRpcPruneAll() override;

private:
    int epollFd_;
    int wakeupPipe_[2];
    std::unordered_map<int, IConnection::Ptr> connections_;
    std::mutex connMutex_;
    std::atomic<bool> running_{false};
    std::thread::id thread_id_;
    TimerManager timerMgr_;
    MessageQueue* messageQueue_ = nullptr;
    WorkerPool* workerPool_ = nullptr;
    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};

#endif // SNC_PLATFORM_LINUX

// ── macOS kqueue ─────────────────────────────────────────────────
#ifdef SNC_PLATFORM_MACOS

class EventLoopKqueue : public IEventLoop {
public:
    EventLoopKqueue();
    ~EventLoopKqueue() override;

    void Run() override;
    void Stop() override;
    void AddConnection(IConnection::Ptr conn) override;
    void RemoveConnection(IConnection::Ptr conn) override;
    void Wakeup() override;
    bool IsRunning() const override { return running_.load(std::memory_order_acquire); }
    TimerManager& TimerMgr() override { return timerMgr_; }
    void SetMessageQueue(MessageQueue* q) override { messageQueue_ = q; }
    void SetWorkerPool(WorkerPool* pool) override { workerPool_ = pool; }
    void SetAcceptSock(SocketWrap sock) override { acceptSock_ = std::move(sock); }
    void SetAcceptCallback(std::function<void()> cb) override { acceptCallback_ = std::move(cb); }
    void SetMessageDispatchFn(std::function<void(Msg::Ptr)> fn) override { messageDispatchFn_ = std::move(fn); }
    void ProcessHeartbeatTickAll() override;
    void ProcessRpcPruneAll() override;

private:
    int kqueueFd_;
    int wakeupPipe_[2];
    std::unordered_map<int, IConnection::Ptr> connections_;
    std::mutex connMutex_;
    std::atomic<bool> running_{false};
    std::thread::id thread_id_;
    TimerManager timerMgr_;
    MessageQueue* messageQueue_ = nullptr;
    WorkerPool* workerPool_ = nullptr;
    SocketWrap acceptSock_;
    std::function<void()> acceptCallback_;
    std::function<void(Msg::Ptr)> messageDispatchFn_;
};

#endif // SNC_PLATFORM_MACOS

// ── Factory ──────────────────────────────────────────────────────
std::unique_ptr<IEventLoop> CreateEventLoop();

} // namespace core
} // namespace snc
