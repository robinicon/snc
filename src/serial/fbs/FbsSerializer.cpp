// SPDX-License-Identifier: MIT
// FbsSerializer implementation — stub mode compatible
#include "snc/serial/fbs/FbsSerializer.h"

#include <cstring>

#ifdef SNC_ENABLE_FLATBUFFERS
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#endif

namespace snc {
namespace serial {
namespace fbs {

std::vector<uint8_t> FbsSerializer::Encode(const char* fbs_data, size_t fbs_len,
                                           size_t& out_len) {
#ifdef SNC_ENABLE_FLATBUFFERS
    out_len = 4 + fbs_len;
    std::vector<uint8_t> buf(out_len);
    buf[0] = static_cast<uint8_t>((fbs_len >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((fbs_len >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((fbs_len >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(fbs_len & 0xFF);
    std::memcpy(buf.data() + 4, fbs_data, fbs_len);
    return buf;
#else
    (void)fbs_data;
    (void)fbs_len;
    out_len = 0;
    return {};
#endif
}

bool FbsSerializer::Decode(const uint8_t* data, size_t len, std::string& out) {
#ifdef SNC_ENABLE_FLATBUFFERS
    if (len < 4) return false;
    size_t fbs_len =
        (static_cast<size_t>(data[0]) << 24) |
        (static_cast<size_t>(data[1]) << 16) |
        (static_cast<size_t>(data[2]) << 8)  |
        (static_cast<size_t>(data[3]));
    if (len < 4 + fbs_len) return false;
    out.assign(reinterpret_cast<const char*>(data + 4), fbs_len);
    return true;
#else
    (void)data;
    (void)len;
    (void)out;
    return false;
#endif
}

SerialError FbsSerializer::Serialize(const void* src, size_t srcLen,
                                      core::Msg& outMsg) {
#ifdef SNC_ENABLE_FLATBUFFERS
    if (!src || srcLen == 0) return SerialError::INVALID_MSG;
    const char* buf_ptr = static_cast<const char*>(src);

    // Verify it's a valid FlatBuffers buffer (starts with 4-byte root sequence)
    // FlatBuffers root sequence is 4 bytes at the end of the buffer
    if (srcLen < 4) return SerialError::INVALID_MSG;

    size_t total = 4 + srcLen; // 4-byte prefix + fb data
    std::string wire(total, '\0');
    uint8_t* p = reinterpret_cast<uint8_t*>(wire.data());
    p[0] = static_cast<uint8_t>((srcLen >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((srcLen >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((srcLen >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(srcLen & 0xFF);
    std::memcpy(wire.data() + 4, buf_ptr, srcLen);
    outMsg = core::Msg(0, wire.data(), wire.size());
    return SerialError::OK;
#else
    (void)src;
    (void)srcLen;
    (void)outMsg;
    return SerialError::SER_FAILED;
#endif
}

SerialError FbsSerializer::Deserialize(const core::Msg& srcMsg, size_t offset,
                                        void* dst, size_t dstLen) {
#ifdef SNC_ENABLE_FLATBUFFERS
    if (!dst || dstLen == 0) return SerialError::INVALID_MSG;
    const auto* data = srcMsg.PayloadBytes() + offset;
    size_t remaining = static_cast<size_t>(srcMsg.PayloadLen()) - offset;
    if (remaining < 4) return SerialError::INVALID_MSG;

    size_t fbs_len =
        (static_cast<size_t>(data[0]) << 24) |
        (static_cast<size_t>(data[1]) << 16) |
        (static_cast<size_t>(data[2]) << 8)  |
        (static_cast<size_t>(data[3]));
    if (remaining < 4 + fbs_len) return SerialError::INVALID_MSG;

    // For FlatBuffers, we typically don't need to copy — the data is already
    // in a valid buffer format. Write raw bytes to dst.
    if (fbs_len > dstLen) return SerialError::TYPE_MISMATCH;
    std::memcpy(dst, data + 4, fbs_len);
    (void)dstLen;
    return SerialError::OK;
#else
    (void)srcMsg;
    (void)offset;
    (void)dst;
    (void)dstLen;
    return SerialError::SER_FAILED;
#endif
}

} // namespace fbs
} // namespace serial
} // namespace snc
