// SPDX-License-Identifier: MIT
#pragma once

// snc_websocket — WebSocket gateway module.
//
// Architecture: The WebSocket gateway runs as a **separate process** that:
//   1. Accepts WebSocket connections from clients (via our lightweight WS engine).
//   2. Verifies JWT tokens and resolves player_id.
//   3. Forwards game messages to the GameServer cluster via snc_rpc over TCP.
//   4. Receives game-server pushes and relays them as WebSocket frames.
//
// The gateway maintains its own event loop (select/epoll/kqueue) independent
// of the game server's event loop. Communication between gateway and game
// servers uses the existing snc_rpc binary protocol.

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include "snc/base/Common.h"

namespace snc {
namespace ws {

// ── Constants ─────────────────────────────────────────────────────

static constexpr int32_t  kWsOpText    = 0x1;
static constexpr int32_t  kWsOpBinary  = 0x2;
static constexpr int32_t  kWsOpClose   = 0x8;
static constexpr int32_t  kWsOpPing    = 0x9;
static constexpr int32_t  kWsOpPong    = 0xA;
static constexpr size_t   kWsMaxFramePayload = 16 * 1024 * 1024; // 16 MB per frame
static constexpr size_t   kWsMaxTotalBuffer = 64 * 1024 * 1024; // 64 MB total recv+send buffer
static constexpr int      kWsHeartbeatIntervalMs = 30000;
static constexpr int      kWsHeartbeatMissMax    = 3;
static constexpr int      kWsHandshakeTimeoutMs  = 5000;

// ── Connection state machine ──────────────────────────────────────

enum class WsState : uint8_t {
    HANDSHAKE,        // HTTP upgrade in progress
    UNAUTHENTICATED,  // Waiting for login token
    AUTHENTICATED,    // Token verified, bound to player_id
    CLOSING,          // Close frame received, waiting for ACK
};

inline const char* WsStateStr(WsState s) {
    switch (s) {
        case WsState::HANDSHAKE:       return "HANDSHAKE";
        case WsState::UNAUTHENTICATED: return "UNAUTHENTICATED";
        case WsState::AUTHENTICATED:   return "AUTHENTICATED";
        case WsState::CLOSING:         return "CLOSING";
        default:                       return "UNKNOWN";
    }
}

// ── Forward declarations ──────────────────────────────────────────

class IGatewayBridge;  // defined in WsGateway.h

// ── WsConnection ──────────────────────────────────────────────────

class WsConnection : public std::enable_shared_from_this<WsConnection> {
public:
    using Ptr     = std::shared_ptr<WsConnection>;
    using CloseCb = std::function<void(int32_t playerId, int closeCode)>;

    WsConnection(int fd, IGatewayBridge& bridge);
    ~WsConnection();

    WsConnection(const WsConnection&) = delete;
    WsConnection& operator=(const WsConnection&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────
    int     Fd()         const { return fd_; }
    WsState State()      const { return state_; }
    int32_t PlayerId()   const { return player_id_; }
    bool    IsAuthenticated() const { return state_ == WsState::AUTHENTICATED; }

    void SetCloseCallback(CloseCb cb) { closeCb_ = std::move(cb); }

    // Called by the gateway loop when data is available.
    void OnData(const char* buf, size_t len);
    // Called when the connection is closed from the other side.
    void OnClosed();

    // Check total buffer size (recv + send). Returns true if within limit.
    bool CheckBufferLimit_() const;

    // Write raw bytes (used to forward RPC responses as WS frames).
    bool SendRaw(const char* data, size_t len);
    // Write a binary frame with the given payload.
    bool SendBinary(const char* data, size_t len);
    // Send a close frame.
    void SendClose(int code = 1000, const char* reason = "");
    // Send a ping (for heartbeat).
    void SendPing();

    // Gateway-access helpers (friend method via gateway friend declaration below)
    bool HasPendingSend() const { return !sendBuf_.empty(); }
    const char* SendBufData() const { return sendBuf_.data(); }
    size_t SendBufSize() const { return sendBuf_.size(); }
    void SendBufErase(size_t n) { sendBuf_.erase(sendBuf_.begin(), sendBuf_.begin() + n); }
    int64_t LastActivityMs() const { return lastActivityMs_; }
    void SetLastActivityMs(int64_t ms) { lastActivityMs_ = ms; }
    int IncrMissedPongs() { return missedPongs_.fetch_add(1, std::memory_order_relaxed); }

private:
    // ── WebSocket frame encoding ───────────────────────────────

    // Encode a WebSocket frame and append to send buffer.
    void EncodeFrame(uint8_t opcode, const uint8_t* payload, size_t len);
    // Decode a partial WebSocket frame from receive buffer.
    void ProcessReceivedData();

    // ── Parse HTTP upgrade request ─────────────────────────────
    bool ParseHandshake_(const char* data, size_t len);
    // Compute Sec-WebSocket-Accept value.
    static std::string ComputeAccept_(const std::string& key);
    // Decode masked client frame payload.
    static void DecodeMask_(uint8_t* payload, size_t len, const uint8_t* mask);

    int           fd_;
    IGatewayBridge& bridge_;
    WsState       state_ = WsState::HANDSHAKE;
    int32_t       player_id_ = 0;

    CloseCb    closeCb_;

    // Receive buffer accumulates incomplete frames.
    std::vector<char> recvBuf_;
    // Send buffer holds encoded frames waiting for write.
    std::vector<char> sendBuf_;

    // Handshake state
    std::string wsAcceptKey_;
    bool        handshakeComplete_ = false;

    // Heartbeat
    std::atomic<int>  missedPongs_{0};
    int64_t           lastActivityMs_ = 0;

    // ── Frame parsing state ────────────────────────────────────
    struct FrameState {
        bool   fin       = false;
        uint8_t opcode   = 0;
        bool   masked    = false;
        size_t payloadLen = 0;
        uint8_t  maskKey[4] = {0};
        std::vector<uint8_t> accumulated;
    };
    FrameState currentFrame_;
};

using WsConnectionPtr = WsConnection::Ptr;

// ── IGatewayBridge — callback interface for the gateway loop ────

class IGatewayBridge {
public:
    virtual ~IGatewayBridge() = default;
    // Called when a player logs in (connects + authenticates).
    virtual void OnPlayerConnected(int32_t playerId, WsConnectionPtr conn) = 0;
    // Called when a player disconnects.
    virtual void OnPlayerDisconnected(int32_t playerId, WsConnectionPtr conn) = 0;
    // Forward a game message to the game server via snc_rpc.
    // Returns true on success.
    virtual bool ForwardToGame(int32_t playerId, const char* payload, size_t len) = 0;
    // Relay a game-server push to the specified player.
    virtual void RelayFromGame(int32_t playerId, const char* payload, size_t len) = 0;
    // Verify a JWT token; returns player_id or -1 on failure.
    virtual int32_t VerifyToken(const std::string& token) = 0;
};

// ── WsGateway — accepts WS connections, manages connections ──────

class WsGateway {
public:
    WsGateway(IGatewayBridge& bridge);
    ~WsGateway();

    WsGateway(const WsGateway&) = delete;
    WsGateway& operator=(const WsGateway&) = delete;

    // Start listening on the given port. Returns true on success.
    bool Listen(int port);
    // Run the event loop (blocking). Call Stop() to exit.
    void Run();
    // Signal the event loop to stop.
    void Stop();

    // Get the number of active connections.
    size_t ActiveConnectionCount() const;

private:
    void AcceptLoop_();
    void EventLoop_();
    void ProcessIncoming_(int fd);
    void ProcessOutgoing_(WsConnectionPtr conn);
    void CleanupClosed_();

    IGatewayBridge&       bridge_;
    int                     listenFd_ = -1;
    std::atomic<bool>       running_{false};
    std::thread             eventThread_;

    std::mutex              connMutex_;
    std::unordered_map<int, WsConnectionPtr> connections_;
};

} // namespace ws
} // namespace snc
