// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#ifdef SNC_ENABLE_PROTOBUF
#include <google/protobuf/message.h>
#else
#pragma message("snc_serial_pb: Protobuf disabled — stub mode (all operations return SER_FAILED)")
namespace google { namespace protobuf { class Message; } }
#endif

#include "snc/serial/ISerializer.h"
#include "snc/core/Msg.h"

namespace snc {
namespace serial {
namespace pb {

// PbSerializer wraps a Protobuf Message for serialization.
// In stub mode (no protobuf), all methods are no-ops returning error codes.
class PbSerializer : public ISerializer {
public:
    SerialError Serialize(const void* src, size_t srcLen,
                          core::Msg& outMsg) override;
    SerialError Deserialize(const core::Msg& srcMsg, size_t offset,
                            void* dst, size_t dstLen) override;

    // Encode a Protobuf message into length-prefixed bytes.
    // Returns empty vector on failure. Caller owns the result.
    static std::vector<uint8_t> Encode(const char* proto_data, size_t proto_len,
                                       size_t& out_len);

    // Decode length-prefixed bytes back into a string.
    static bool Decode(const uint8_t* data, size_t len, std::string& out);
};

} // namespace pb
} // namespace serial
} // namespace snc
