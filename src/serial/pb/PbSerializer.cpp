// PbSerializer implementation — stub mode compatible
#include "snc/serial/pb/PbSerializer.h"

#include <cstring>

#ifdef SNC_ENABLE_PROTOBUF
#include <google/protobuf/message.h>
#endif

namespace snc {
namespace serial {
namespace pb {

uint8_t* PbSerializer::Encode(const char* proto_data, size_t proto_len,
                              size_t& out_len) {
#ifdef SNC_ENABLE_PROTOBUF
    out_len = 4 + proto_len;
    uint8_t* buf = new uint8_t[out_len];
    buf[0] = static_cast<uint8_t>((proto_len >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((proto_len >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((proto_len >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(proto_len & 0xFF);
    std::memcpy(buf + 4, proto_data, proto_len);
    return buf;
#else
    (void)proto_data;
    (void)proto_len;
    out_len = 0;
    return nullptr;
#endif
}

bool PbSerializer::Decode(const uint8_t* data, size_t len, std::string& out) {
#ifdef SNC_ENABLE_PROTOBUF
    if (len < 4) return false;
    size_t proto_len =
        (static_cast<size_t>(data[0]) << 24) |
        (static_cast<size_t>(data[1]) << 16) |
        (static_cast<size_t>(data[2]) << 8)  |
        (static_cast<size_t>(data[3]));
    if (len < 4 + proto_len) return false;
    out.assign(reinterpret_cast<const char*>(data + 4), proto_len);
    return true;
#else
    (void)data;
    (void)len;
    (void)out;
    return false;
#endif
}

SerialError PbSerializer::Serialize(const void* src, size_t srcLen,
                                     core::Msg& outMsg) {
#ifdef SNC_ENABLE_PROTOBUF
    if (!src || srcLen == 0) return SerialError::INVALID_MSG;
    const google::protobuf::Message* msg =
        reinterpret_cast<const google::protobuf::Message*>(src);
    std::string encoded;
    if (!msg->SerializeToString(&encoded)) return SerialError::SER_FAILED;
    // Wire format: 4-byte big-endian length prefix + protobuf bytes
    size_t total = 4 + encoded.size();
    std::string buf(total, '\0');
    uint8_t* p = reinterpret_cast<uint8_t*>(buf.data());
    p[0] = static_cast<uint8_t>((encoded.size() >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((encoded.size() >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((encoded.size() >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(encoded.size() & 0xFF);
    std::memcpy(buf.data() + 4, encoded.data(), encoded.size());
    outMsg = core::Msg(0, buf.data(), buf.size());
    return SerialError::OK;
#else
    (void)src;
    (void)srcLen;
    (void)outMsg;
    return SerialError::SER_FAILED;
#endif
}

SerialError PbSerializer::Deserialize(const core::Msg& srcMsg, size_t offset,
                                       void* dst, size_t dstLen) {
#ifdef SNC_ENABLE_PROTOBUF
    if (!dst || dstLen == 0) return SerialError::INVALID_MSG;
    const auto* data = srcMsg.PayloadBytes() + offset;
    size_t remaining = static_cast<size_t>(srcMsg.PayloadLen()) - offset;
    if (remaining < 4) return SerialError::INVALID_MSG;

    size_t proto_len =
        (static_cast<size_t>(data[0]) << 24) |
        (static_cast<size_t>(data[1]) << 16) |
        (static_cast<size_t>(data[2]) << 8)  |
        (static_cast<size_t>(data[3]));
    if (remaining < 4 + proto_len) return SerialError::INVALID_MSG;

    std::string proto_str;
    proto_str.assign(reinterpret_cast<const char*>(data + 4), proto_len);

    google::protobuf::Message* msg =
        reinterpret_cast<google::protobuf::Message*>(dst);
    if (!msg->ParseFromString(proto_str)) return SerialError::SER_FAILED;
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

} // namespace pb
} // namespace serial
} // namespace snc
