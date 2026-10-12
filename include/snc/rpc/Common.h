#pragma once

#include <string>
#include "snc/base/Common.h"

namespace snc {
namespace rpc {

enum class RpcError : int {
    OK = 0,
    TIMEOUT = -1,
    CANCELLED = -2,
    UNKNOWN = -3,
    SERVICE_UNAVAILABLE = -4,
};

inline const char* RpcErrorStr(RpcError err) {
    switch (err) {
        case RpcError::OK: return "OK";
        case RpcError::TIMEOUT: return "TIMEOUT";
        case RpcError::CANCELLED: return "CANCELLED";
        case RpcError::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

} // namespace rpc
} // namespace snc
