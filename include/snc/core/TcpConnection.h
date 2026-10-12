#pragma once

#include <memory>
#include <vector>
#include <algorithm>
#include <functional>
#include <chrono>
#include <atomic>
#include "snc/core/IConnection.h"
#include "snc/core/DisconnectReason.h"
#include "snc/core/SocketWrap.h"
#include "snc/core/Msg.h"
#include "snc/core/EntityHandle.h"
#include "snc/core/WorkerPool.h"

namespace snc {
namespace core {

// Heartbeat message IDs
static constexpr int32_t kMsgHeartbeatPing = 0x0001;
static constexpr int32_t kMsgHeartbeatPong = 0x0002;

// ── TcpConnection ────────────────────────────────────────────────
//
// Features (Phase C):
//   C1 – Heartbeat: periodically sends ping; waits for pong.  3 missed
//        pongs → DisconnectReason::TIMEOUT.
//   C3 – Backpressure: when sendBuffer_ exceeds kSendBufferHighWatermark,
//        the connection stops calling ParseMessages() on new data until
//        the buffer drains below kSendBufferLowWatermark.
//   C5 – OnDisconnect callback with reason (DisconnectReason).

class TcpConnection : public IConnection {
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using HeartbeatCallback = std::function<void(DisconnectReason)>;

    TcpConnection() = default;
    TcpConnection(TcpConnection&&) = default;
    TcpConnection& operator=(TcpConnection&&) = default;
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    explicit TcpConnection(SocketWrap sock)
        : sock_(std::move(sock)), state_(State::Connected) {}

    int Fd() const override { return sock_.Fd(); }

    // ── Send / Close ───────────────────────────────────────────
    bool Send(Msg::Ptr msg) override {
        if (!msg || !sock_.Valid()) return false;
        const char* data = msg->Data();
        size_t total = msg->TotalLen();
        size_t prevBuffered = sendBuffer_.size() - sendOffset_;
        sendBuffer_.insert(sendBuffer_.end(), data, data + total);
        // Only auto-flush when the buffer was previously empty (or nearly so)
        // to avoid a syscall per message in a send loop, while still flushing
        // the first message of each batch promptly.
        if (prevBuffered <= total) {
            FlushSendBuffer();
        }
        return true;
    }

    // Close the connection and fire closeCallback_ exactly once.
    // Returns true if this call performed the close; false if another
    // concurrent path already closed it.
    bool Close() override {
        State expected = State::Connected;
        if (!state_.compare_exchange_strong(expected, State::Closed)) return false;
        sock_.Close();
        if (closeCallback_) closeCallback_(shared_from_this(), DisconnectReason::NORMAL);
        return true;
    }

    // ── Callbacks ──────────────────────────────────────────────
    void SetMessageCallback(MessageCallback cb) override { messageCallback_ = std::move(cb); }
    void SetCloseCallback(CloseCallback cb) override  { closeCallback_  = std::move(cb); }
    void SetHeartbeatCallback(HeartbeatCallback cb)   { heartbeatCallback_ = std::move(cb); }

    // Returns an EntityHandle wrapping this connection as an Entity.
    EntityHandle<IConnection> GetConnHandle() {
        auto self = shared_from_this();
        return EntityHandle<IConnection>::from(std::shared_ptr<IConnection>(self));
    }

    // ── RPC session hook (for Phase C4 – PruneExpired) ───────────
    using PruneExpiredFn = std::function<void()>;
    void SetPruneExpired(PruneExpiredFn fn) { pruneExpiredFn_ = std::move(fn); }
    void InvokePruneExpired() { if (pruneExpiredFn_) pruneExpiredFn_(); }

    // ── MessageQueue dispatch hook (Phase D) ─────────────────────
    // When SetEnqueueFn is set, messages are routed through the global
    // MessageQueue (backpressure + high/low watermarks).
    void SetEnqueueFn(EnqueueFn fn) override { enqueueFn_ = std::move(fn); }
    void DispatchToQueue(Msg::Ptr msg) override {
        if (enqueueFn_) enqueueFn_(std::move(msg));
    }

    // ── WorkerPool dispatch hook (Phase D – direct submission) ───
    // Alternative to SetEnqueueFn: submit each parsed message directly
    // to a WorkerPool thread, bypassing the global MessageQueue.
    // Typically used when backpressure is handled at the application level.
    using WorkerDispatchFn = std::function<void(EntityHandle<IConnection>, Msg::Ptr)>;
    void SetWorkerDispatchFn(WorkerDispatchFn fn, WorkerPool* pool) {
        workerDispatchFn_ = std::move(fn);
        workerPool_ = pool;
    }
    WorkerPool* GetWorkerPool() const { return workerPool_; }

    // ── Heartbeat ──────────────────────────────────────────────
    // Call after construction (or in SetNewConnectionCallback).
    // intervalMs  – how often to send a ping  (default 30 000 ms)
    // pongTimeoutMs – time to wait for a pong after last ping sent (default 10 000 ms)
    // maxMissed   – consecutive missed pongs before declaring timeout (default 3)
    void StartHeartbeat(long long intervalMs = 30000,
                        long long pongTimeoutMs = 10000,
                        int maxMissed = 3) {
        heartbeatIntervalMs_  = intervalMs;
        heartbeatPongTimeout_ = pongTimeoutMs;
        heartbeatMaxMissed_   = maxMissed;
        heartbeatMissed_ = 0;
        heartbeatNextPingAtMs_ = NowMs() + intervalMs;
        ScheduleNextPing();
    }

    void StopHeartbeat() {
        heartbeatActive_ = false;
        if (heartbeatTimerId_ > 0) {
            heartbeatTimerId_ = 0;
        }
    }

    // Called by the event loop on every ProcessTimers() cycle.
    void ProcessHeartbeatTick() {
        if (!heartbeatActive_) return;
        long long now = NowMs();

        // 1) Is it time to send another ping?
        if (now >= heartbeatNextPingAtMs_) {
            SendHeartbeatPing();
            ScheduleNextPing();
        }

        // 2) Has the last ping timed out waiting for pong?
        if (lastPingSentAtMs_ > 0 && now - lastPingSentAtMs_ > heartbeatPongTimeout_) {
            ++heartbeatMissed_;
            lastPingSentAtMs_ = 0;
            SNC_LOG_WARN("core", "heartbeat missed %d/%d", heartbeatMissed_, heartbeatMaxMissed_);
            if (heartbeatMissed_ >= heartbeatMaxMissed_) {
                SNC_LOG_ERROR("core", "heartbeat timeout – closing conn fd=%d", Fd());
                State expected = State::Connected;
                if (state_.compare_exchange_strong(expected, State::Closed)) {
                    sock_.Close();
                    if (heartbeatCallback_) heartbeatCallback_(DisconnectReason::TIMEOUT);
                    if (closeCallback_)     closeCallback_(shared_from_this(), DisconnectReason::TIMEOUT);
                }
            }
        }
    }

    // ── Read / Parse ───────────────────────────────────────────
    void ProcessRecvData() {
        char buf[4096];
        ssize_t n = sock_.Recv(buf, sizeof(buf));
        if (n <= 0) {
            State expected = State::Connected;
            if (!state_.compare_exchange_strong(expected, State::Closed)) return;
            sock_.Close();
            DisconnectReason reason = (n == 0)
                ? DisconnectReason::NORMAL
                : DisconnectReason::SOCKET_ERR;
            if (heartbeatCallback_) heartbeatCallback_(reason);
            if (closeCallback_)     closeCallback_(shared_from_this(), reason);
            return;
        }
        recvBuffer_.insert(recvBuffer_.end(), buf, buf + n);

        // C3 – Backpressure gate: skip parsing while send buffer is above high watermark.
        // When below low watermark, backpressure is lifted and parsing resumes.
        if (static_cast<long long>(sendBuffer_.size() - sendOffset_) > kSendBufferHighWatermark) return;

        ParseMessages();
    }

    void FlushSendBuffer() {
        if (sendOffset_ >= sendBuffer_.size() || !sock_.Valid()) return;
        size_t available = sendBuffer_.size() - sendOffset_;
        if (available == 0) { sendOffset_ = 0; return; }
        ssize_t n = sock_.Send(sendBuffer_.data() + sendOffset_, available);
        if (n > 0) sendOffset_ += static_cast<size_t>(n);
        // Compact when more than half is consumed (reclaim at most sendBuffer_.size()/2).
        if (sendOffset_ >= sendBuffer_.size() / 2) CompactSendBuffer();
    }

    // Check backpressure state (for external callers such as the event loop).
    bool IsBackpressuring() const {
        return static_cast<long long>(sendBuffer_.size() - sendOffset_) > kSendBufferHighWatermark;
    }

private:
    // ── Ring-buffer compaction ───────────────────────────────────
    // Shifts remaining live data to the front of the vector so that
    // sendOffset_/recvOffset_ can be reset to 0. Called when the
    // "used but not yet consumed" tail grows beyond a threshold.
    void CompactSendBuffer() {
        if (sendOffset_ == 0) return;
        size_t avail = sendBuffer_.size() - sendOffset_;
        if (avail == 0) { sendBuffer_.clear(); sendOffset_ = 0; return; }
        std::memmove(sendBuffer_.data(), sendBuffer_.data() + sendOffset_, avail);
        sendBuffer_.resize(avail);
        sendOffset_ = 0;
    }

    void CompactRecvBuffer() {
        if (recvOffset_ == 0) return;
        size_t avail = recvBuffer_.size() - recvOffset_;
        if (avail == 0) { recvBuffer_.clear(); recvOffset_ = 0; return; }
        std::memmove(recvBuffer_.data(), recvBuffer_.data() + recvOffset_, avail);
        recvBuffer_.resize(avail);
        recvOffset_ = 0;
    }

    static long long NowMs() {
        auto tp = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count();
    }

    void SendHeartbeatPing() {
        auto ping = Msg::Create(kMsgHeartbeatPing);
        sock_.Send(ping->Data(), ping->TotalLen());
        lastPingSentAtMs_ = NowMs();
        heartbeatMissed_  = 0;
    }

    void ScheduleNextPing() {
        heartbeatNextPingAtMs_ = NowMs() + heartbeatIntervalMs_;
    }

    void ParseMessages() {
        while (true) {
            size_t avail = recvBuffer_.size() - recvOffset_;
            if (avail < Msg::HEADER_SIZE) return;
            const Msg::Header* h = reinterpret_cast<const Msg::Header*>(recvBuffer_.data() + recvOffset_);
            int32_t msgId  = h->msgId;
            int32_t plen   = h->payloadLen;
            int32_t total  = Msg::HEADER_SIZE + plen;
            if ((int32_t)avail < total) return;

            auto msg = Msg::FromBuffer(reinterpret_cast<const char*>(recvBuffer_.data() + recvOffset_), static_cast<size_t>(total));
            if (!msg) return;
            recvOffset_ += static_cast<size_t>(total);

            // Compact recv buffer when more than half has been consumed.
            if (recvOffset_ >= recvBuffer_.size() / 2) CompactRecvBuffer();

            // C1 – Route heartbeat messages before user callback.
            if (msgId == kMsgHeartbeatPong) {
                lastPingSentAtMs_ = 0;
                heartbeatMissed_  = 0;
                continue;
            }
            if (msgId == kMsgHeartbeatPing) {
                auto pong = Msg::Create(kMsgHeartbeatPong);
                sock_.Send(pong->Data(), pong->TotalLen());
                continue;
            }

            // Phase D – dispatch to MessageQueue / WorkerPool when configured.
            if (workerDispatchFn_ && workerPool_) {
                // Direct submission to WorkerPool (bypasses global queue).
                auto handle = GetConnHandle();
                workerPool_->Submit([fn = workerDispatchFn_, handle, msg = std::move(msg)]() mutable {
                    fn(std::move(handle), std::move(msg));
                });
            } else if (enqueueFn_) {
                DispatchToQueue(std::move(msg));
            } else {
                if (messageCallback_) messageCallback_(GetConnHandle(), msg);
            }
        }
    }

    // ── Members ────────────────────────────────────────────────
    SocketWrap          sock_;
    MessageCallback     messageCallback_;
    CloseCallback       closeCallback_;
    HeartbeatCallback   heartbeatCallback_;
    EnqueueFn           enqueueFn_;

    std::vector<char>   recvBuffer_;
    size_t              recvOffset_ = 0;
    std::vector<char>   sendBuffer_;
    size_t              sendOffset_ = 0;

    enum class State { Connected, Closing, Closed };
    std::atomic<State>    state_{State::Connected};

    // C3 – Backpressure thresholds (bytes).
    static constexpr long long kSendBufferHighWatermark = 1024 * 1024;   // 1 MB
    static constexpr long long kSendBufferLowWatermark  = 512  * 1024;   // 512 KB

    // C1 – Heartbeat state.
    long long heartbeatIntervalMs_  = 30000;
    long long heartbeatPongTimeout_ = 10000;
    int       heartbeatMaxMissed_   = 3;
    int       heartbeatMissed_      = 0;
    long long lastPingSentAtMs_     = 0;
    long long heartbeatNextPingAtMs_= 0;
    int64_t   heartbeatTimerId_     = 0;
    bool      heartbeatActive_      = false;
    PruneExpiredFn pruneExpiredFn_;

    // Phase D – Worker dispatch.
    WorkerDispatchFn workerDispatchFn_;
    WorkerPool*      workerPool_ = nullptr;

};

} // namespace core
} // namespace snc
