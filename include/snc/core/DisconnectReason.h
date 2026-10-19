// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace snc {
namespace core {

enum class DisconnectReason : int {
    NORMAL         = 0,   // Close() called by application or TCP FIN received
    SOCKET_ERR     = 1,   // recv() returned error / WSAECONNRESET etc.
    TIMEOUT        = 2,   // heartbeat timeout (no pong in interval_ms)
    PROTOCOL_ERROR = 3,
    BACKPRESSURE   = 4,
};

} // namespace core
} // namespace snc
