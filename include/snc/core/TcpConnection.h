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
// 功能特性（对应 Phase C）：
//   C1 – 心跳保活：周期性发送 ping，等待 pong。连续 3 次未收到 pong → TIMEOUT 断线。
//   C3 – 反向压制的发送背压：当 sendBuffer_ 超过高水位线时，暂停解析新数据；
//        缓冲区回落到低水位线以下时恢复解析。
//   C5 – OnDisconnect 回调：断开时携带 DisconnectReason 原因码。

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

    // ── 发送 / 关闭 ───────────────────────────────────────────
    bool Send(Msg::Ptr msg) override {
        if (!msg || !sock_.Valid()) return false;
        const char* data = msg->Data();
        size_t total = msg->TotalLen();
        size_t prevBuffered = sendBuffer_.size() - sendOffset_;
        sendBuffer_.insert(sendBuffer_.end(), data, data + total);
        // 仅当缓冲区先前为空（或接近空）时自动 flush，避免在快速发送循环中每条消息都触发系统调用，
        // 同时确保每批首条消息及时发出。
        if (prevBuffered <= total) {
            FlushSendBuffer();
        }
        return true;
    }

    /// 关闭连接并恰好触发一次 closeCallback_。
    /// 返回 true 表示本次调用执行了关闭；false 表示其他并发路径已先关闭。
    bool Close() override {
        State expected = State::Connected;
        if (!state_.compare_exchange_strong(expected, State::Closed)) return false;
        sock_.Close();
        if (closeCallback_) closeCallback_(shared_from_this(), DisconnectReason::NORMAL);
        return true;
    }

    // ── Callbacks ───────────────────────────────────────────────────
    void SetMessageCallback(MessageCallback cb) override { messageCallback_ = std::move(cb); }
    void SetCloseCallback(CloseCallback cb) override { closeCallback_ = std::move(cb); }
    void SetHeartbeatCallback(HeartbeatCallback cb) override { heartbeatCallback_ = std::move(cb); }

    /// 获取包装本连接的 EntityHandle，用于跨线程安全传递
    EntityHandle<IConnection> GetConnHandle() {
        auto self = shared_from_this();
        return EntityHandle<IConnection>::from(std::shared_ptr<IConnection>(self));
    }

    // ── RPC 会话清理钩子（Phase C4 – PruneExpired）────────────────
    void SetPruneExpired(PruneExpiredFn fn) { pruneExpiredFn_ = std::move(fn); }
    void InvokePruneExpired() { if (pruneExpiredFn_) pruneExpiredFn_(); }

    // ── 消息队列分发钩子（Phase D）──────────────────────────────
    // 设置后，收到的消息通过全局 MessageQueue 路由（支持背压 + 高低水位线控制）。
    void SetEnqueueFn(EnqueueFn fn) override { enqueueFn_ = std::move(fn); }
    void DispatchToQueue(Msg::Ptr msg) override {
        if (enqueueFn_) enqueueFn_(std::move(msg));
    }

    // ── WorkerPool 直接分发钩子（Phase D）────────────────────────
    // 替代 SetEnqueueFn：将每条解析出的消息直接提交到 WorkerPool 线程，绕过全局消息队列。
    // 通常在应用层自行处理背压时使用。
    void SetWorkerDispatchFn(WorkerDispatchFn fn, WorkerPool* pool) {
        workerDispatchFn_ = std::move(fn);
        workerPool_ = pool;
    }
    WorkerPool* GetWorkerPool() const { return workerPool_; }

    // ── 心跳 ──────────────────────────────────────────────────
    /// 构造后（或在 SetNewConnectionCallback 中）调用。
    /// intervalMs      – 发送 ping 的间隔（默认 30000 ms）
    /// pongTimeoutMs   – 最后一次 ping 发出后等待 pong 的超时时间（默认 10000 ms）
    /// maxMissed       – 连续未收到 pong 的最大次数，超过则判为超时（默认 3）
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

    /// 由事件循环在每个 ProcessTimers() 周期调用，处理心跳状态机。
    void ProcessHeartbeatTick() {
        if (!heartbeatActive_) return;
        long long now = NowMs();

        // 1) 是否到了发送下一个 ping 的时机？
        if (now >= heartbeatNextPingAtMs_) {
            SendHeartbeatPing();
            ScheduleNextPing();
        }

        // 2) 最近一次 ping 是否已超时未收到 pong？
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

    // ── 接收 / 解析 ───────────────────────────────────────────
    void ProcessRecvData() {
        char buf[4096];
        ssize_t n = sock_.Recv(buf, sizeof(buf));
        if (n <= 0) {
            // 对端关闭或读取错误，触发断开回调
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

        // C3 – 背压门控：当发送缓冲区超过高水位线时，跳过消息解析以减轻压力；
        //      缓冲区回落到低水位线以下时，背压解除，继续解析。
        if (static_cast<long long>(sendBuffer_.size() - sendOffset_) > kSendBufferHighWatermark) return;

        ParseMessages();
    }

    /// 将 sendBuffer_ 中未发送的数据尽可能写入 socket，更新 sendOffset_。
    void FlushSendBuffer() {
        if (sendOffset_ >= sendBuffer_.size() || !sock_.Valid()) return;
        size_t available = sendBuffer_.size() - sendOffset_;
        if (available == 0) { sendOffset_ = 0; return; }
        ssize_t n = sock_.Send(sendBuffer_.data() + sendOffset_, available);
        if (n > 0) sendOffset_ += static_cast<size_t>(n);
        // 已发送超过一半时压缩缓冲区，最多回收 sendBuffer_.size()/2 的空间
        if (sendOffset_ >= sendBuffer_.size() / 2) CompactSendBuffer();
    }

    /// 供外部（如事件循环）查询当前是否处于背压状态
    bool IsBackpressuring() const {
        return static_cast<long long>(sendBuffer_.size() - sendOffset_) > kSendBufferHighWatermark;
    }

private:
    // ── 环形缓冲区压缩 ───────────────────────────────────────────
    /// 将 sendBuffer_ 中剩余未发送的数据前移，使 sendOffset_ 归零。
    /// 当"已消费但尚未移位"的尾部累积超过阈值时调用。
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

    /// 发送心跳 ping 并记录发送时刻
    void SendHeartbeatPing() {
        auto ping = Msg::Create(kMsgHeartbeatPing);
        sock_.Send(ping->Data(), ping->TotalLen());
        lastPingSentAtMs_ = NowMs();
        heartbeatMissed_  = 0;
    }

    /// 计算下次 ping 发送时间戳
    void ScheduleNextPing() {
        heartbeatNextPingAtMs_ = NowMs() + heartbeatIntervalMs_;
    }

    /// 逐条解析 recvBuffer_ 中的完整消息，并按当前配置的派发方式路由
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

            // 已消费超过一半时压缩 recv 缓冲区
            if (recvOffset_ >= recvBuffer_.size() / 2) CompactRecvBuffer();

            // C1 – 优先处理心跳消息，不进入用户回调
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

            // Phase D – 按配置的派发方式路由消息：WorkerPool 直发 / MessageQueue / 直接回调
            if (workerDispatchFn_ && workerPool_) {
                // 直接提交到 WorkerPool（绕过全局消息队列）
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
