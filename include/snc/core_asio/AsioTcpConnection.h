#pragma once

#include <memory>
#include <atomic>
#include <vector>
#include <functional>
#include <asio.hpp>
#include "snc/core/IConnection.h"
#include "snc/core/DisconnectReason.h"
#include "snc/core/Msg.h"
#include "snc/core/EntityHandle.h"
#include "snc/core/WorkerPool.h"

namespace snc {
namespace core {

// Heartbeat message IDs — mirrors TcpConnection for protocol consistency
static constexpr int32_t kMsgHeartbeatPing = 0x0001;
static constexpr int32_t kMsgHeartbeatPong = 0x0002;

// ── AsioTcpConnection ───────────────────────────────────────────
//
// 基于 Boost.Asio strand 的异步 TCP 连接实现，替代 TcpConnection 的同步 recv/send 模型。
//
// 功能特性：
//   C1 – 心跳保活：周期性发送 ping，等待 pong。连续 3 次未收到 pong → TIMEOUT 断线。
//   C3 – 反向压制的发送背压：当 sendBuffer_ 超过高水位线时，暂停解析新数据；
//        缓冲区回落到低水位线以下时恢复解析。
//   C5 – OnDisconnect 回调：断开时携带 DisconnectReason 原因码。
//
// 设计要点：
//   - 使用 asio::ip::tcp::socket 作为底层传输（而非 SocketWrap）
//   - 使用 asio::strand 保证同一连接的读写操作不并发
//   - async_read 分两阶段：先读 MsgHeader（8B），完成后读 payload
//   - async_write 使用 sendBuffer_/sendOffset_ 状态机（与 TcpConnection 语义一致）
//   - ParseMessages 完全复用 TcpConnection 的心跳/路由逻辑

class AsioTcpConnection : public IConnection {
public:
    using Ptr = std::shared_ptr<AsioTcpConnection>;

    AsioTcpConnection(const AsioTcpConnection&) = delete;
    AsioTcpConnection& operator=(const AsioTcpConnection&) = delete;

    // ── 构造 / 析构 ───────────────────────────────────────────
    explicit AsioTcpConnection(asio::io_context& ioCtx);
    ~AsioTcpConnection() override;

    // ── 工厂：从已接受的 socket 创建连接（AsioAcceptor 使用）─────────
    static Ptr CreateFromAcceptedSocket(asio::io_context& ioCtx,
                                        asio::ip::tcp::socket sock);

    // ── IConnection 接口 ──────────────────────────────────────
    int Fd() const override;
    bool Send(Msg::Ptr msg) override;
    bool Close() override;

    void SetMessageCallback(MessageCallback cb) override {
        messageCallback_ = std::move(cb);
    }
    void SetCloseCallback(CloseCallback cb) override {
        closeCallback_ = std::move(cb);
    }
    void SetHeartbeatCallback(HeartbeatCallback cb) override {
        heartbeatCallback_ = std::move(cb);
    }

    EntityHandle<IConnection> GetConnHandle() override;

    // ── Phase C4 – RPC 会话清理钩子 ───────────────────────────
    void SetPruneExpired(PruneExpiredFn fn) override {
        pruneExpiredFn_ = std::move(fn);
    }
    void InvokePruneExpired() override {
        if (pruneExpiredFn_) pruneExpiredFn_();
    }

    // ── Phase D – 消息队列 / WorkerPool 分发 ──────────────────
    void SetEnqueueFn(EnqueueFn fn) override {
        enqueueFn_ = std::move(fn);
    }
    void DispatchToQueue(Msg::Ptr msg) override {
        if (enqueueFn_) enqueueFn_(std::move(msg));
    }
    void SetWorkerDispatchFn(WorkerDispatchFn fn, WorkerPool* pool) override {
        workerDispatchFn_ = std::move(fn);
        workerPool_ = pool;
    }
    WorkerPool* GetWorkerPool() const override {
        return workerPool_;
    }

    // ── Phase C1 – 心跳 ─────────────────────────────────────
    void StartHeartbeat(long long intervalMs = 30000,
                        long long pongTimeoutMs = 10000,
                        int maxMissed = 3);
    void StopHeartbeat();
    void ProcessHeartbeatTick();

    // ── Phase C3 – 背压查询 ───────────────────────────────────
    bool IsBackpressuring() const override {
        return static_cast<long long>(sendBuffer_.size() - sendOffset_) > kSendBufferHighWatermark;
    }

    // ── 启动异步读取循环 ──────────────────────────────────────
    void Start();

private:
    // ── 读取状态机 ────────────────────────────────────────────
    enum class ReadState { ReadingHeader, ReadingPayload, Idle };

    void StartReadHeader();
    void OnReadHeaderDone(asio::error_code ec, size_t bytes_transferred);
    void StartReadPayload(int32_t payloadLen);
    void OnReadPayloadDone(asio::error_code ec, size_t bytes_transferred);

    // ── 写入状态机 ────────────────────────────────────────────
    void DoSend();
    void OnWriteDone(asio::error_code ec, size_t bytes_transferred);

    // ── 关闭（必须在 strand 上调用）───────────────────────────
    void CloseInternal();

    // ── 消息解析（与 TcpConnection::ParseMessages 语义一致）──
    void ParseMessages();

    // ── 缓冲区压缩 ────────────────────────────────────────────
    void CompactSendBuffer();
    void CompactRecvBuffer();

    // ── 时间工具 ──────────────────────────────────────────────
    static long long NowMs();

    // ── 心跳辅助 ──────────────────────────────────────────────
    void SendHeartbeatPing();
    void ScheduleNextPing();

    // ── 成员变量 ──────────────────────────────────────────────
    asio::ip::tcp::socket               socket_;
    asio::strand<asio::io_context::executor> strand_;

    MessageCallback                     messageCallback_;
    CloseCallback                       closeCallback_;
    HeartbeatCallback                   heartbeatCallback_;
    EnqueueFn                           enqueueFn_;
    WorkerDispatchFn                    workerDispatchFn_;
    WorkerPool*                         workerPool_ = nullptr;
    PruneExpiredFn                      pruneExpiredFn_;

    // 接收缓冲区（供 ParseMessages 使用）
    std::vector<char>                   recvBuffer_;
    size_t                              recvOffset_ = 0;

    // 发送缓冲区 + 状态机
    std::vector<char>                   sendBuffer_;
    size_t                              sendOffset_ = 0;
    bool                                writing_ = false;

    // 读取状态机
    ReadState                           readState_ = ReadState::Idle;
    char                                headerBuf_[Msg::HEADER_SIZE];
    std::vector<char>                   payloadBuf_;

    // 连接状态
    enum class State { Connected, Closed };
    std::atomic<State>                  state_{State::Connected};
    std::atomic<bool>                   closing_{false};

    // C3 – 背压阈值（字节）
    static constexpr long long kSendBufferHighWatermark = 1024 * 1024;   // 1 MB
    static constexpr long long kSendBufferLowWatermark  = 512  * 1024;   // 512 KB

    // C1 – 心跳状态
    long long heartbeatIntervalMs_  = 30000;
    long long heartbeatPongTimeout_ = 10000;
    int       heartbeatMaxMissed_   = 3;
    int       heartbeatMissed_      = 0;
    long long lastPingSentAtMs_     = 0;
    long long heartbeatNextPingAtMs_= 0;
    bool      heartbeatActive_      = false;
};

} // namespace core
} // namespace snc
