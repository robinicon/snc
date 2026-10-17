// AsioTcpConnection — Boost.Asio strand-safe async TCP connection implementation.
//
// Replaces the synchronous recv/send model of TcpConnection with:
//   - async_read in two phases (header 8B → payload N B) driven by a ReadState machine
//   - async_write using sendBuffer_/sendOffset_ state machine via asio::async_write
//   - asio::strand wrapping all I/O callbacks so reads and writes never interleave
//
// Heartbeat / ParseMessages / backpressure semantics are identical to TcpConnection.

#include "snc/core_asio/AsioTcpConnection.h"
#include "snc/base/Log.h"

#include <cstring>
#include <algorithm>

namespace snc {
namespace core {

// ── Construction ─────────────────────────────────────────────────

AsioTcpConnection::AsioTcpConnection(asio::io_context& ioCtx)
    : socket_(ioCtx)
    , strand_(ioCtx.get_executor())
{
    // Disable Nagle — small heartbeat / control messages should go out immediately.
    asio::error_code ec;
    socket_.set_option(asio::ip::tcp::no_delay(true), ec);
    (void)ec;
}

AsioTcpConnection::Ptr AsioTcpConnection::CreateFromAcceptedSocket(
    asio::io_context& ioCtx, asio::ip::tcp::socket sock)
{
    // Disable Nagle on the accepted socket.
    asio::error_code ec;
    sock.set_option(asio::ip::tcp::no_delay(true), ec);
    (void)ec;

    auto conn = std::make_shared<AsioTcpConnection>(ioCtx);
    // Transfer the socket into the connection.
    conn->socket_ = std::move(sock);
    // strand is non-assignable (holds a reference); re-create via placement new.
    ::new (&conn->strand_) asio::strand<asio::io_context::executor>(ioCtx.get_executor());

    // Start the async read loop immediately so that data arriving on the
    // accepted socket is read as soon as io_context runs.
    conn->Start();
    return conn;
}

AsioTcpConnection::~AsioTcpConnection() {
    // If the socket is still open (e.g. connection was never started or
    // CloseInternal was not called), close it here to avoid resource leak.
    asio::error_code ec;
    socket_.cancel(ec);
    socket_.close(ec);
}

// ── IConnection ──────────────────────────────────────────────────

int AsioTcpConnection::Fd() const {
#ifdef _WIN32
    // native_handle() returns SOCKET (uintptr_t) on Windows; cast to int
    // to match the Fd() contract used throughout the codebase.
    return static_cast<int>(socket_.native_handle());
#else
    return static_cast<int>(socket_.native_handle());
#endif
}

bool AsioTcpConnection::Send(Msg::Ptr msg) {
    if (!msg || state_ != State::Connected) return false;

    const char* data   = msg->Data();
    size_t      total  = msg->TotalLen();
    size_t      prevBuffered = sendBuffer_.size() - sendOffset_;

    // Post the append + possible send kick onto the strand so that concurrent
    // Send() calls from different threads cannot corrupt sendBuffer_.
    strand_.post([this, data, total, prevBuffered]() {
        if (state_ != State::Connected) return;
        sendBuffer_.insert(sendBuffer_.end(), data, data + total);

        // Flush only when the buffer was previously empty (or nearly empty),
        // avoiding a system call per message in a rapid send loop.
        if (prevBuffered <= total) {
            DoSend();
        }
    });

    return true;
}

bool AsioTcpConnection::Close() {
    State expected = State::Connected;
    if (!state_.compare_exchange_strong(expected, State::Closed)) return false;

    // Post close on the strand to avoid deadlock when Close() is invoked
    // from within a strand callback (e.g. an async_write error handler).
    strand_.post([this] { CloseInternal(); });
    return true;
}

EntityHandle<IConnection> AsioTcpConnection::GetConnHandle() {
    auto self = shared_from_this();
    return EntityHandle<IConnection>::from(
        std::shared_ptr<IConnection>(self));
}

// ── Heartbeat ────────────────────────────────────────────────────

void AsioTcpConnection::StartHeartbeat(long long intervalMs,
                                        long long pongTimeoutMs,
                                        int maxMissed) {
    heartbeatIntervalMs_  = intervalMs;
    heartbeatPongTimeout_ = pongTimeoutMs;
    heartbeatMaxMissed_   = maxMissed;
    heartbeatMissed_      = 0;
    lastPingSentAtMs_     = 0;
    heartbeatActive_      = true;
    ScheduleNextPing();
}

void AsioTcpConnection::StopHeartbeat() {
    heartbeatActive_ = false;
    lastPingSentAtMs_ = 0;
}

void AsioTcpConnection::ProcessHeartbeatTick() {
    if (!heartbeatActive_ || state_ != State::Connected) return;

    long long now = NowMs();

    // 1) Is it time to send the next ping?
    if (now >= heartbeatNextPingAtMs_) {
        SendHeartbeatPing();
        ScheduleNextPing();
    }

    // 2) Did the last ping exceed the pong timeout?
    if (lastPingSentAtMs_ > 0 &&
        now - lastPingSentAtMs_ > heartbeatPongTimeout_) {
        ++heartbeatMissed_;
        lastPingSentAtMs_ = 0;
        SNC_LOG_WARN("core_asio", "heartbeat missed %d/%d fd=%d",
                     heartbeatMissed_, heartbeatMaxMissed_, Fd());
        if (heartbeatMissed_ >= heartbeatMaxMissed_) {
            SNC_LOG_ERROR("core_asio",
                          "heartbeat timeout – closing conn fd=%d", Fd());
            // Transition to Closed and close on the strand.
            State expected = State::Connected;
            if (state_.compare_exchange_strong(expected, State::Closed)) {
                strand_.post([this]{ CloseInternal(); });
            }
        }
    }
}

// ── Read state machine ───────────────────────────────────────────

void AsioTcpConnection::Start() {
    StartReadHeader();
}

void AsioTcpConnection::StartReadHeader() {
    readState_ = ReadState::ReadingHeader;
    asio::async_read(socket_,
        asio::buffer(headerBuf_, Msg::HEADER_SIZE),
        strand_.wrap([this](asio::error_code ec, size_t) {
            OnReadHeaderDone(ec, 0);
        }));
}

void AsioTcpConnection::OnReadHeaderDone(asio::error_code ec, size_t) {
    if (ec) {
        if (ec.value() != static_cast<int>(asio::error::operation_aborted)) {
            SNC_LOG_WARN("core_asio", "read header error: %s fd=%d",
                         ec.message().c_str(), Fd());
        }
        CloseInternal();
        return;
    }

    // Append the freshly-read header to recvBuffer_.
    recvBuffer_.insert(recvBuffer_.end(), headerBuf_, headerBuf_ + Msg::HEADER_SIZE);

    // Parse the header to learn the payload length.
    Msg::Header h;
    std::memcpy(&h, headerBuf_, sizeof(h));

    if (h.payloadLen > 0) {
        // Need to read the payload in a second async_read phase.
        StartReadPayload(h.payloadLen);
    } else {
        // Zero-length payload — parse immediately and re-arm for the next header.
        readState_ = ReadState::Idle;
        ParseMessages();
        StartReadHeader();
    }
}

void AsioTcpConnection::StartReadPayload(int32_t payloadLen) {
    readState_ = ReadState::ReadingPayload;
    payloadBuf_.resize(static_cast<size_t>(payloadLen));
    asio::async_read(socket_,
        asio::buffer(payloadBuf_),
        strand_.wrap([this](asio::error_code ec, size_t) {
            OnReadPayloadDone(ec, 0);
        }));
}

void AsioTcpConnection::OnReadPayloadDone(asio::error_code ec, size_t) {
    if (ec) {
        if (ec.value() != static_cast<int>(asio::error::operation_aborted)) {
            SNC_LOG_WARN("core_asio", "read payload error: %s fd=%d",
                         ec.message().c_str(), Fd());
        }
        payloadBuf_.clear();
        CloseInternal();
        return;
    }

    // Append payload to recvBuffer_ and parse.
    recvBuffer_.insert(recvBuffer_.end(),
                       payloadBuf_.begin(), payloadBuf_.end());
    payloadBuf_.clear();

    readState_ = ReadState::Idle;
    ParseMessages();

    // Re-arm for the next message header.
    StartReadHeader();
}

// ── Write state machine ──────────────────────────────────────────

void AsioTcpConnection::DoSend() {
    if (writing_ || state_ != State::Connected) return;

    size_t available = sendBuffer_.size() - sendOffset_;
    if (available == 0) {
        writing_ = false;
        return;
    }

    writing_ = true;
    asio::async_write(socket_,
        asio::buffer(sendBuffer_.data() + sendOffset_, available),
        strand_.wrap([this](asio::error_code ec, size_t bytes_transferred) {
            OnWriteDone(ec, bytes_transferred);
        }));
}

void AsioTcpConnection::OnWriteDone(asio::error_code ec,
                                     size_t bytes_transferred) {
    if (ec) {
        SNC_LOG_WARN("core_asio", "async_write error: %s fd=%d",
                     ec.message().c_str(), Fd());
        writing_ = false;
        CloseInternal();
        return;
    }

    sendOffset_ += bytes_transferred;

    if (sendOffset_ >= sendBuffer_.size()) {
        // All data sent — reset for next batch.
        sendBuffer_.clear();
        sendOffset_ = 0;
    } else if (sendOffset_ >= sendBuffer_.size() / 2) {
        // Compacting saves memory during prolonged partial sends.
        CompactSendBuffer();
    }

    writing_ = false;

    // If there is still unsent data, continue pushing.
    if (sendOffset_ < sendBuffer_.size()) {
        DoSend();
    }
}

// ── Close ────────────────────────────────────────────────────────

void AsioTcpConnection::CloseInternal() {
    // Guard against double-close (e.g. error handler + explicit Close()).
    bool expected = false;
    if (!closing_.compare_exchange_strong(expected, true)) return;

    writing_ = false;
    readState_ = ReadState::Idle;
    payloadBuf_.clear();

    asio::error_code ec;
    socket_.cancel(ec);
    socket_.close(ec);

    if (heartbeatCallback_) {
        heartbeatCallback_(DisconnectReason::NORMAL);
    }
    if (closeCallback_) {
        closeCallback_(shared_from_this(), DisconnectReason::NORMAL);
    }

    SNC_LOG_INFO("core_asio", "connection closed fd=%d", Fd());
}

// ── ParseMessages ────────────────────────────────────────────────
// Logic identical to TcpConnection::ParseMessages; heartbeat
// ping/pong and worker dispatch paths are preserved.

void AsioTcpConnection::ParseMessages() {
    // C3 – Backpressure gate: skip parsing when the send buffer is overloaded.
    if (static_cast<long long>(sendBuffer_.size() - sendOffset_) >
        kSendBufferHighWatermark) {
        return;
    }

    while (true) {
        size_t avail = recvBuffer_.size() - recvOffset_;
        if (avail < Msg::HEADER_SIZE) return;

        const Msg::Header* h = reinterpret_cast<const Msg::Header*>(
            recvBuffer_.data() + recvOffset_);
        int32_t msgId   = h->msgId;
        int32_t plen    = h->payloadLen;
        int32_t total   = Msg::HEADER_SIZE + plen;

        if ((int32_t)avail < total) return;

        auto msg = Msg::FromBuffer(
            reinterpret_cast<const char*>(recvBuffer_.data() + recvOffset_),
            static_cast<size_t>(total));
        if (!msg) return;

        recvOffset_ += static_cast<size_t>(total);

        // Compact recv buffer when more than half has been consumed.
        if (recvOffset_ >= recvBuffer_.size() / 2) CompactRecvBuffer();

        // C1 – Heartbeat: handle ping/pong before any user callback.
        if (msgId == kMsgHeartbeatPong) {
            lastPingSentAtMs_ = 0;
            heartbeatMissed_  = 0;
            continue;
        }
        if (msgId == kMsgHeartbeatPing) {
            auto pong = Msg::Create(kMsgHeartbeatPong);
            Send(pong);  // goes through async_write path
            continue;
        }

        // Phase D – Route via WorkerPool / MessageQueue / direct callback.
        if (workerDispatchFn_ && workerPool_) {
            auto handle = GetConnHandle();
            workerPool_->Submit([fn = workerDispatchFn_, handle,
                                 msg = std::move(msg)]() mutable {
                fn(std::move(handle), std::move(msg));
            });
        } else if (enqueueFn_) {
            DispatchToQueue(std::move(msg));
        } else {
            if (messageCallback_) {
                messageCallback_(GetConnHandle(), msg);
            }
        }
    }
}

// ── Buffer compaction ────────────────────────────────────────────

void AsioTcpConnection::CompactSendBuffer() {
    if (sendOffset_ == 0) return;
    size_t avail = sendBuffer_.size() - sendOffset_;
    if (avail == 0) {
        sendBuffer_.clear();
        sendOffset_ = 0;
        return;
    }
    std::memmove(sendBuffer_.data(),
                 sendBuffer_.data() + sendOffset_, avail);
    sendBuffer_.resize(avail);
    sendOffset_ = 0;
}

void AsioTcpConnection::CompactRecvBuffer() {
    if (recvOffset_ == 0) return;
    size_t avail = recvBuffer_.size() - recvOffset_;
    if (avail == 0) {
        recvBuffer_.clear();
        recvOffset_ = 0;
        return;
    }
    std::memmove(recvBuffer_.data(),
                 recvBuffer_.data() + recvOffset_, avail);
    recvBuffer_.resize(avail);
    recvOffset_ = 0;
}

// ── Time helpers ─────────────────────────────────────────────────

long long AsioTcpConnection::NowMs() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return static_cast<long long>(
        counter.QuadPart * 1000.0 / freq.QuadPart);
#else
    using namespace std::chrono;
    return static_cast<long long>(duration_cast<milliseconds>(
        high_resolution_clock::now().time_since_epoch()).count());
#endif
}

void AsioTcpConnection::SendHeartbeatPing() {
    auto ping = Msg::Create(kMsgHeartbeatPing);
    Send(ping);
    lastPingSentAtMs_ = NowMs();
    heartbeatMissed_  = 0;
}

void AsioTcpConnection::ScheduleNextPing() {
    heartbeatNextPingAtMs_ = NowMs() + heartbeatIntervalMs_;
}

} // namespace core
} // namespace snc
