#include "snc/ws/WsConnection.h"
#include "snc/base/Common.h"
#include <chrono>

namespace snc {
namespace ws {

// WsGateway implementation.
//
// This is a lightweight, self-contained WebSocket server. It handles:
//   - HTTP upgrade handshake (RFC 6455)
//   - WebSocket frame encoding/decoding (binary/text)
//   - Client-to-server masking (XOR with 4-byte key)
//   - Connection lifecycle (open, authenticated, closing, closed)
//   - Heartbeat (ping/pong) every kWsHeartbeatIntervalMs

namespace {

#ifdef _WIN32
using SocketType = SOCKET;
#define CLOSE_SOCKET(s) closesocket(s)
#else
using SocketType = int;
#define CLOSE_SOCKET(s) ::close(s)
#endif

// ── Socket helpers ──────────────────────────────────────────────

static bool SetNonBlocking(SocketType fd, bool enable) {
#ifdef _WIN32
    unsigned long mode = enable ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) != -1;
#endif
}

static bool SetNoDelay(SocketType fd) {
    int val = 1;
#ifdef _WIN32
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
#else
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
#endif
}

// ── SHA-1 (for WebSocket Accept key computation) ────────────────
// Minimal SHA-1 implementation for RFC 6455 handshake.
// Only used once per connection; performance is not critical.

static uint32_t LeftRotate(uint32_t val, int shift) {
    return (val << shift) | (val >> (32 - shift));
}

static void Sha1Transform(uint32_t* state, const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
             | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
             |  static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = LeftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;           k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;           k = 0xCA62C1D6; }
        uint32_t temp = LeftRotate(a, 5) + f + e + k + w[i];
        e = d; d = c; c = LeftRotate(b, 30); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c;
    state[3] += d; state[4] += e;
}

static std::string Sha1Base64(const std::string& input) {
    static const char kBase64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    size_t msgLen = input.size();
    uint64_t bitLen = msgLen * 8;

    // Append bit '1' (0x80 byte)
    std::vector<uint8_t> padded(msgLen + 1);
    std::copy(input.begin(), input.end(), padded.begin());
    padded[msgLen] = 0x80;

    // Pad with zeros until length ≡ 56 (mod 64)
    while (padded.size() % 64 != 56) {
        padded.push_back(0);
    }

    // Append original length in bits as big-endian 64-bit integer
    padded.resize(padded.size() + 8);
    for (int i = 0; i < 8; ++i) {
        padded[padded.size() - 8 + i] = static_cast<uint8_t>((bitLen >> (56 - i * 8)) & 0xFF);
    }

    // Process 64-byte blocks
    for (size_t i = 0; i < padded.size(); i += 64) {
        Sha1Transform(h, &padded[i]);
    }

    // Produce raw digest (20 bytes)
    uint8_t digest[20];
    for (int i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = (h[i] >> 24) & 0xFF;
        digest[i * 4 + 1] = (h[i] >> 16) & 0xFF;
        digest[i * 4 + 2] = (h[i] >> 8)  & 0xFF;
        digest[i * 4 + 3] =  h[i]         & 0xFF;
    }

    // Base64 encode
    std::string result;
    result.reserve(((20 + 2) / 3) * 4);
    for (size_t i = 0; i < 20; i += 3) {
        uint32_t triple = (static_cast<uint32_t>(digest[i]) << 16)
                        | ((i + 1 < 20) ? static_cast<uint32_t>(digest[i + 1]) << 8 : 0)
                        | ((i + 2 < 20) ? static_cast<uint32_t>(digest[i + 2]) : 0);
        result.push_back(kBase64[(triple >> 18) & 0x3F]);
        result.push_back(kBase64[(triple >> 12) & 0x3F]);
        result.push_back(kBase64[(triple >>  6) & 0x3F]);
        result.push_back(kBase64[ triple        & 0x3F]);
    }
    // Pad to multiple of 4
    while (result.size() % 4 != 0) result.push_back('=');
    return result;
}

} // anonymous namespace

// ── WsConnection ────────────────────────────────────────────────

WsConnection::WsConnection(int fd, IGatewayBridge& bridge)
    : fd_(fd), bridge_(bridge), lastActivityMs_(snc::TimeMs()) {
    SetNonBlocking(static_cast<SocketType>(fd), true);
    SetNoDelay(static_cast<SocketType>(fd));
}

WsConnection::~WsConnection() {
    if (fd_ >= 0) {
        CLOSE_SOCKET(static_cast<SocketType>(fd_));
        fd_ = -1;
    }
}

bool WsConnection::SendRaw(const char* data, size_t len) {
    if (len == 0) return true;
    const char* p = data;
    size_t remaining = len;
    while (remaining > 0) {
#ifdef _WIN32
        int n = ::send(fd_, p, static_cast<int>(remaining), 0);
#else
        ssize_t n = ::send(fd_, p, remaining, 0);
#endif
        if (n < 0) {
            // Would-block or error
            sendBuf_.insert(sendBuf_.end(), p, p + remaining);
            return false;
        }
        p += n;
        remaining -= n;
    }
    lastActivityMs_ = snc::TimeMs();
    return true;
}

bool WsConnection::SendBinary(const char* data, size_t len) {
    EncodeFrame(kWsOpBinary, reinterpret_cast<const uint8_t*>(data), len);
    return true;
}

void WsConnection::SendClose(int code, const char* reason) {
    // Close frame: 2-byte code + optional reason
    std::vector<uint8_t> payload(2 + strlen(reason), 0);
    payload[0] = static_cast<uint8_t>((code >> 8) & 0xFF);
    payload[1] = static_cast<uint8_t>(code & 0xFF);
    if (reason && reason[0]) {
        memcpy(payload.data() + 2, reason, strlen(reason));
    }
    EncodeFrame(kWsOpClose, payload.data(), payload.size());
}

void WsConnection::SendPing() {
    EncodeFrame(kWsOpPing, nullptr, 0);
}

void WsConnection::EncodeFrame(uint8_t opcode, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> frame;
    size_t headerLen = 2;

    // First byte: FIN + opcode
    uint8_t firstByte = 0x80 | opcode; // FIN=1, RSV1-3=0
    frame.push_back(firstByte);

    // Second byte: MASK=0 (server→client), payload length
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
        headerLen = 2;
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
        headerLen = 4;
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
        headerLen = 10;
    }

    // Append payload
    if (payload && len > 0) {
        frame.insert(frame.end(), payload, payload + len);
    }

    // Write to send buffer
    sendBuf_.insert(sendBuf_.end(), frame.begin(), frame.end());

    // Try to flush immediately
    if (!sendBuf_.empty()) {
        const char* p = sendBuf_.data();
        size_t rem = sendBuf_.size();
#ifdef _WIN32
        int n = ::send(fd_, p, static_cast<int>(rem), 0);
#else
        ssize_t n = ::send(fd_, p, rem, 0);
#endif
        if (n > 0) {
            sendBuf_.erase(sendBuf_.begin(), sendBuf_.begin() + n);
        }
    }
    lastActivityMs_ = snc::TimeMs();
}

void WsConnection::DecodeMask_(uint8_t* payload, size_t len, const uint8_t* mask) {
    for (size_t i = 0; i < len; ++i) {
        payload[i] ^= mask[i & 3];
    }
}

void WsConnection::ProcessReceivedData() {
    // Enforce total buffer limit (includes current recvBuf_ content)
    if (!CheckBufferLimit_()) {
        recvBuf_.clear();
        SendClose(1009, "Buffer limit exceeded");
        return;
    }

    // Attempt to read more data
    char readBuf[65536];
#ifdef _WIN32
    int n = ::recv(fd_, readBuf, sizeof(readBuf), 0);
#else
    ssize_t n = ::recv(fd_, readBuf, sizeof(readBuf), 0);
#endif
    if (n > 0) {
        recvBuf_.insert(recvBuf_.end(), readBuf, readBuf + n);
    }

    // Try to parse complete frames from recvBuf_
    while (recvBuf_.size() >= 2) {
        uint8_t first = static_cast<uint8_t>(recvBuf_[0]);
        uint8_t second = static_cast<uint8_t>(recvBuf_[1]);
        bool fin = (first & 0x80) != 0;
        uint8_t opcode = first & 0x0F;
        bool masked = (second & 0x80) != 0;
        size_t payloadLen = second & 0x7F;

        // Server MUST NOT mask frames (RFC 6455 §5.1)
        if (masked) {
            // Force close on malformed frame
            recvBuf_.clear();
            SendClose(1002, "Masked frame from server");
            return;
        }

        size_t headerSize = 2;
        if (payloadLen == 126) {
            if (recvBuf_.size() < 4) break;
            payloadLen = (static_cast<size_t>(recvBuf_[2]) << 8) | recvBuf_[3];
            headerSize = 4;
        } else if (payloadLen == 127) {
            if (recvBuf_.size() < 10) break;
            payloadLen = 0;
            for (int i = 2; i < 10; ++i) {
                payloadLen = (payloadLen << 8) | static_cast<uint8_t>(recvBuf_[i]);
            }
            headerSize = 10;
        }

        if (payloadLen > kWsMaxFramePayload) {
            recvBuf_.clear();
            SendClose(1009, "Frame too large");
            return;
        }

        if (recvBuf_.size() < headerSize + payloadLen) break;

        // Extract frame payload
        const uint8_t* frameData = reinterpret_cast<const uint8_t*>(recvBuf_.data() + headerSize);
        size_t framePayloadLen = payloadLen;

        // Handle control frames (close/ping/pong) — always fin
        if (opcode == kWsOpClose) {
            recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + headerSize + payloadLen);
            // Extract close code
            int closeCode = 1005;
            if (framePayloadLen >= 2) {
                closeCode = (frameData[0] << 8) | frameData[1];
            }
            state_ = WsState::CLOSING;
            SendClose(closeCode);
            if (closeCb_) {
                closeCb_(player_id_, closeCode);
            }
            return;
        }

        if (opcode == kWsOpPing) {
            recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + headerSize + payloadLen);
            // Respond with pong, echoing the payload
            EncodeFrame(kWsOpPong, frameData, framePayloadLen);
            continue;
        }

        if (opcode == kWsOpPong) {
            recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + headerSize + payloadLen);
            missedPongs_.fetch_sub(1, std::memory_order_relaxed);
            continue;
        }

        // Data frames (text/binary)
        if (!fin) {
            // Fragmented frame — accumulate (simplified: don't support fragmentation)
            recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + headerSize);
            continue;
        }

        // Copy payload out of recvBuf_
        std::vector<uint8_t> framePayload(frameData, frameData + framePayloadLen);
        recvBuf_.erase(recvBuf_.begin(), recvBuf_.begin() + headerSize + framePayloadLen);

        lastActivityMs_ = TimeMs();

        // Dispatch based on state
        if (state_ == WsState::HANDSHAKE) {
            // Should not happen — handshake should be processed separately
            continue;
        }

        if (state_ == WsState::UNAUTHENTICATED && opcode == kWsOpBinary) {
            // First binary message is the login token
            std::string token(reinterpret_cast<const char*>(framePayload.data()),
                              framePayload.size());
            int32_t pid = bridge_.VerifyToken(token);
            if (pid < 0) {
                // Auth failed — send close and disconnect
                SendClose(4001, "Unauthorized");
                if (closeCb_) closeCb_(0, 4001);
                return;
            }
            player_id_ = pid;
            state_ = WsState::AUTHENTICATED;
            bridge_.OnPlayerConnected(pid, shared_from_this());
            continue;
        }

        if (state_ == WsState::AUTHENTICATED && opcode == kWsOpBinary) {
            // Forward game message to game server
            bridge_.ForwardToGame(player_id_,
                                  reinterpret_cast<const char*>(framePayload.data()),
                                  framePayload.size());
            continue;
        }

        // Text frames are ignored (we use binary only)
    }
}

void WsConnection::OnData(const char* buf, size_t len) {
    if (state_ == WsState::HANDSHAKE) {
        // Process HTTP upgrade request
        if (ParseHandshake_(buf, len)) {
            state_ = WsState::UNAUTHENTICATED;
        } else {
            // Invalid handshake
            state_ = WsState::CLOSING;
            SendClose(4002, "Invalid handshake");
        }
        return;
    }
    // Enforce total buffer limit before appending
    if (!CheckBufferLimit_()) {
        recvBuf_.clear();
        SendClose(1009, "Buffer limit exceeded");
        return;
    }
    // Append to receive buffer and try to parse
    recvBuf_.insert(recvBuf_.end(), buf, buf + len);
    ProcessReceivedData();
}

bool WsConnection::CheckBufferLimit_() const {
    size_t total = recvBuf_.size() + sendBuf_.size();
    return total <= kWsMaxTotalBuffer;
}

void WsConnection::OnClosed() {
    if (state_ == WsState::CLOSING || state_ == WsState::HANDSHAKE) {
        // Already closing or never completed — nothing to do
    } else if (state_ == WsState::AUTHENTICATED) {
        bridge_.OnPlayerDisconnected(player_id_, shared_from_this());
    }
    if (closeCb_) {
        closeCb_(player_id_, 1000);
    }
    fd_ = -1;
}

bool WsConnection::ParseHandshake_(const char* data, size_t len) {
    // Look for "GET " or "POST " at start
    std::string req(data, len);
    if (req.find("GET ") != 0 && req.find("POST ") != 0) return false;

    // Find the Upgrade: websocket header
    bool hasUpgrade = (req.find("Upgrade: websocket") != std::string::npos ||
                       req.find("upgrade: websocket") != std::string::npos);
    bool hasConnection = (req.find("Connection: Upgrade") != std::string::npos ||
                          req.find("connection: upgrade") != std::string::npos);

    // Extract Sec-WebSocket-Key
    size_t keyPos = req.find("Sec-WebSocket-Key:");
    if (keyPos == std::string::npos) {
        keyPos = req.find("sec-websocket-key:");
    }
    if (keyPos == std::string::npos) return false;

    keyPos += 18; // length of "Sec-WebSocket-Key:"
    size_t lineEnd = req.find('\n', keyPos);
    if (lineEnd == std::string::npos) return false;

    std::string key = req.substr(keyPos, lineEnd - keyPos);
    // Trim whitespace
    size_t start = key.find_first_not_of(" \t\r\n");
    size_t end   = key.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) key = "";
    else key = key.substr(start, end - start + 1);

    if (!hasUpgrade || !hasConnection || key.empty()) return false;

    // Compute accept key and send response
    std::string accept = ComputeAccept_(key);

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";

    wsAcceptKey_ = std::move(accept);
    handshakeComplete_ = true;

    // Send the response
    SendRaw(response.c_str(), response.size());
    return true;
}

std::string WsConnection::ComputeAccept_(const std::string& key) {
    // The accept key is SHA1(key + "258EAFA5-E914-47DA-95CA-5AB5DC107A1E") base64
    static const char kMagic[] = "258EAFA5-E914-47DA-95CA-5AB5DC107A1E";
    std::string combined = key + kMagic;
    return Sha1Base64(combined);
}

// ── WsGateway ────────────────────────────────────────────────────

WsGateway::WsGateway(IGatewayBridge& bridge) : bridge_(bridge) {}

WsGateway::~WsGateway() {
    Stop();
    if (listenFd_ >= 0) {
        CLOSE_SOCKET(static_cast<SocketType>(listenFd_));
        listenFd_ = -1;
    }
    // Close all connections
    std::lock_guard<std::mutex> lk(connMutex_);
    for (auto& [fd, conn] : connections_) {
        conn->OnClosed();
    }
    connections_.clear();
}

bool WsGateway::Listen(int port) {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(0x0202, &wsaData) != 0) return false;
#endif

    listenFd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (listenFd_ < 0) return false;

    int val = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&val), sizeof(val));
#ifdef _WIN32
    setsockopt(listenFd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&val), sizeof(val));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = static_cast<uint16_t>(htons(static_cast<uint16_t>(port)));

    if (::bind(static_cast<SocketType>(listenFd_),
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCKET(static_cast<SocketType>(listenFd_));
        listenFd_ = -1;
        return false;
    }

    if (::listen(static_cast<SocketType>(listenFd_), 128) != 0) {
        CLOSE_SOCKET(static_cast<SocketType>(listenFd_));
        listenFd_ = -1;
        return false;
    }

    SetNonBlocking(static_cast<SocketType>(listenFd_), true);
    running_.store(true);
    eventThread_ = std::thread(&WsGateway::EventLoop_, this);
    return true;
}

void WsGateway::Run() {
    eventThread_.join();
}

void WsGateway::Stop() {
    running_.store(false);
    if (listenFd_ >= 0) {
        // Wake up the event loop by connecting to itself
#ifdef _WIN32
        SOCKET fake = socket(AF_INET, SOCK_STREAM, 0);
        if (fake != INVALID_SOCKET) {
            connect(fake, nullptr, 0);
            closesocket(fake);
        }
#else
        int fake = socket(AF_INET, SOCK_STREAM, 0);
        if (fake >= 0) { close(fake); }
#endif
    }
    eventThread_.join();
}

void WsGateway::EventLoop_() {
    // Simple select-based event loop
    while (running_.load()) {
        fd_set readfds, writefds, errfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&errfds);

        int maxFd = listenFd_;
        if (maxFd >= 0) FD_SET(static_cast<UINT>(maxFd), &readfds);

        {
            std::lock_guard<std::mutex> lk(connMutex_);
            for (auto& [fd, conn] : connections_) {
                if (fd >= 0) {
                    FD_SET(static_cast<UINT>(fd), &readfds);
                    if (conn->HasPendingSend()) {
                        FD_SET(static_cast<UINT>(fd), &writefds);
                    }
                    FD_SET(static_cast<UINT>(fd), &errfds);
                    if (fd > maxFd) maxFd = fd;
                }
            }
        }

        if (maxFd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 50000; // 50ms timeout

        int ret = select(maxFd + 1, &readfds, &writefds, &errfds, &tv);
        if (ret < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#endif
            break;
        }

        // Accept new connections
        if (listenFd_ >= 0 && FD_ISSET(static_cast<UINT>(listenFd_), &readfds)) {
            sockaddr_in clientAddr{};
            int addrLen = sizeof(clientAddr);
#ifdef _WIN32
            SOCKET clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
#else
            int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), static_cast<socklen_t*>(&addrLen));
#endif
            if (clientFd >= 0) {
                SetNonBlocking(static_cast<SocketType>(clientFd), true);
                SetNoDelay(static_cast<SocketType>(clientFd));

                auto conn = std::make_shared<WsConnection>(clientFd, bridge_);
                std::lock_guard<std::mutex> lk(connMutex_);
                connections_[clientFd] = conn;
            }
        }

        // Process each connection
        std::vector<int> toRemove;
        {
            std::lock_guard<std::mutex> lk(connMutex_);
            for (auto it = connections_.begin(); it != connections_.end(); ) {
                int fd = it->first;
                auto& conn = it->second;

                bool readable = FD_ISSET(static_cast<UINT>(fd), &readfds);
                bool writable = FD_ISSET(static_cast<UINT>(fd), &writefds);
                bool errored  = FD_ISSET(static_cast<UINT>(fd), &errfds);

                if (errored) {
                    conn->OnClosed();
                    toRemove.push_back(fd);
                    ++it;
                    continue;
                }

                if (readable) {
                    // Read available data
                    char readBuf[65536];
#ifdef _WIN32
                    int n = recv(fd, readBuf, sizeof(readBuf), 0);
#else
                    ssize_t n = recv(fd, readBuf, sizeof(readBuf), 0);
#endif
                    if (n > 0) {
                        conn->OnData(readBuf, static_cast<size_t>(n));
                    } else if (n == 0 || (n < 0 &&
#ifdef _WIN32
                                           WSAGetLastError() != WSAEWOULDBLOCK
#else
                                           errno != EAGAIN && errno != EWOULDBLOCK
#endif
                                          )) {
                        conn->OnClosed();
                        toRemove.push_back(fd);
                        ++it;
                        continue;
                    }
                }

                // Flush send buffer
                if (writable && conn->HasPendingSend()) {
                    const char* p = conn->SendBufData();
                    size_t rem = conn->SendBufSize();
#ifdef _WIN32
                    int sent = send(fd, p, static_cast<int>(rem), 0);
#else
                    ssize_t sent = send(fd, p, rem, 0);
#endif
                    if (sent > 0) {
                        conn->SendBufErase(static_cast<size_t>(sent));
                    }
                }

                // Heartbeat check
                int64_t now = snc::TimeMs();
                if (now - conn->LastActivityMs() > kWsHeartbeatIntervalMs) {
                    conn->SendPing();
                    int missed = conn->IncrMissedPongs();
                    if (missed >= kWsHeartbeatMissMax) {
                        conn->OnClosed();
                        toRemove.push_back(fd);
                        ++it;
                        continue;
                    }
                }

                ++it;
            }
        }

        // Remove closed connections
        for (int fd : toRemove) {
            std::lock_guard<std::mutex> lk(connMutex_);
            auto it = connections_.find(fd);
            if (it != connections_.end()) {
                CLOSE_SOCKET(static_cast<SocketType>(it->first));
                it->second.reset();
                connections_.erase(it);
            }
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace ws
} // namespace snc
