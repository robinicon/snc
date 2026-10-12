#pragma once

#include <cstdint>
#include <cstring>
#include "snc/base/Common.h"

namespace snc {
namespace core {

struct RpcReqHeader {
    uint64_t seq;
    int32_t  timeoutMs;
    uint8_t  pad;
    static constexpr size_t SIZE = 13;

    void WriteTo(uint8_t* buf) const {
        buf[0]  = static_cast<uint8_t>(seq >> 56);
        buf[1]  = static_cast<uint8_t>(seq >> 48);
        buf[2]  = static_cast<uint8_t>(seq >> 40);
        buf[3]  = static_cast<uint8_t>(seq >> 32);
        buf[4]  = static_cast<uint8_t>(seq >> 24);
        buf[5]  = static_cast<uint8_t>(seq >> 16);
        buf[6]  = static_cast<uint8_t>(seq >> 8);
        buf[7]  = static_cast<uint8_t>(seq);
        buf[8]  = static_cast<uint8_t>(timeoutMs >> 24);
        buf[9]  = static_cast<uint8_t>(timeoutMs >> 16);
        buf[10] = static_cast<uint8_t>(timeoutMs >> 8);
        buf[11] = static_cast<uint8_t>(timeoutMs);
        buf[12] = pad;
    }

    void ReadFrom(const uint8_t* buf) {
        seq       = (static_cast<uint64_t>(buf[0]) << 56)
                  | (static_cast<uint64_t>(buf[1]) << 48)
                  | (static_cast<uint64_t>(buf[2]) << 40)
                  | (static_cast<uint64_t>(buf[3]) << 32)
                  | (static_cast<uint64_t>(buf[4]) << 24)
                  | (static_cast<uint64_t>(buf[5]) << 16)
                  | (static_cast<uint64_t>(buf[6]) << 8)
                  |  static_cast<uint64_t>(buf[7]);
        timeoutMs = (static_cast<int32_t>(buf[8]) << 24)
                  | (static_cast<int32_t>(buf[9]) << 16)
                  | (static_cast<int32_t>(buf[10]) << 8)
                  |  static_cast<int32_t>(buf[11]);
        pad = buf[12];
    }
};

struct RpcRespHeader {
    uint64_t seq;
    int32_t  errCode;
    int32_t  resultLen;
    static constexpr size_t SIZE = 17;

    void WriteTo(uint8_t* buf) const {
        buf[0]  = static_cast<uint8_t>(seq >> 56);
        buf[1]  = static_cast<uint8_t>(seq >> 48);
        buf[2]  = static_cast<uint8_t>(seq >> 40);
        buf[3]  = static_cast<uint8_t>(seq >> 32);
        buf[4]  = static_cast<uint8_t>(seq >> 24);
        buf[5]  = static_cast<uint8_t>(seq >> 16);
        buf[6]  = static_cast<uint8_t>(seq >> 8);
        buf[7]  = static_cast<uint8_t>(seq);
        buf[8]  = static_cast<uint8_t>(errCode >> 24);
        buf[9]  = static_cast<uint8_t>(errCode >> 16);
        buf[10] = static_cast<uint8_t>(errCode >> 8);
        buf[11] = static_cast<uint8_t>(errCode);
        buf[12] = static_cast<uint8_t>(resultLen >> 24);
        buf[13] = static_cast<uint8_t>(resultLen >> 16);
        buf[14] = static_cast<uint8_t>(resultLen >> 8);
        buf[15] = static_cast<uint8_t>(resultLen);
        buf[16] = 0;
    }

    void ReadFrom(const uint8_t* buf) {
        seq       = (static_cast<uint64_t>(buf[0]) << 56)
                  | (static_cast<uint64_t>(buf[1]) << 48)
                  | (static_cast<uint64_t>(buf[2]) << 40)
                  | (static_cast<uint64_t>(buf[3]) << 32)
                  | (static_cast<uint64_t>(buf[4]) << 24)
                  | (static_cast<uint64_t>(buf[5]) << 16)
                  | (static_cast<uint64_t>(buf[6]) << 8)
                  |  static_cast<uint64_t>(buf[7]);
        errCode   = (static_cast<int32_t>(buf[8]) << 24)
                  | (static_cast<int32_t>(buf[9]) << 16)
                  | (static_cast<int32_t>(buf[10]) << 8)
                  |  static_cast<int32_t>(buf[11]);
        resultLen = (static_cast<int32_t>(buf[12]) << 24)
                  | (static_cast<int32_t>(buf[13]) << 16)
                  | (static_cast<int32_t>(buf[14]) << 8)
                  |  static_cast<int32_t>(buf[15]);
    }
};

} // namespace core
} // namespace snc
