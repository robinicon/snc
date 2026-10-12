#pragma once

#include <cstdint>

namespace snc {
namespace base {

// ── Protocol constants (v1.0) ─────────────────────────────────────
// These values are defined per the SNC protocol spec and must be kept in
// sync across all services and the C client.

// Maximum payload size per message (excluding the 8-byte Msg header).
// Chosen to fit comfortably within a single TCP segment and the
// MessageQueue high-watermark used by Gate.
static constexpr int32_t kMaxPayloadSize = 16 * 1024 * 1024;  // 16 MB

// Message header size (msgId + payloadLen, both int32).
static constexpr int32_t kMsgHeaderSize = 8;

// ── RPC wire format constants ─────────────────────────────────────
// RpcReqHeader: seq(8) + timeoutMs(4) + pad(1) = 13 bytes
static constexpr int32_t kRpcReqHeaderSize = 13;

// RpcRespHeader: seq(8) + errCode(4) + resultLen(4) + pad(1) = 17 bytes
static constexpr int32_t kRpcRespHeaderSize = 17;

// Control message ID wrapping an RpcReqHeader or RpcRespHeader on the wire.
static constexpr int32_t kRpcMsgId = 0xFEED;

// ── Message ID ranges ─────────────────────────────────────────────
// Client  → Gate   : 0x1000 – 0x1FFF
// Gate    → Server : 0x2000 – 0x2FFF
// Server  → Server : 0x3000 – 0x3FFF
// Broadcast      : 0x4000 – 0x4FFF
enum class MsgIdRange : uint16_t {
    ClientToGate    = 0x1000,
    GateToServer    = 0x2000,
    ServerToServer  = 0x3000,
    Broadcast       = 0x4000,
};

// ── Heartbeat constants ───────────────────────────────────────────
static constexpr int32_t kMsgHeartbeatPing  = 0x0001;
static constexpr int32_t kMsgHeartbeatPong  = 0x0002;

} // namespace base
} // namespace snc
