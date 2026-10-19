// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace snc {
namespace core { class Msg; }
namespace serial {

enum class SerialError : int32_t {
    OK            =  0,
    INVALID_MSG   = -1,
    SER_FAILED    = -2,
    TYPE_MISMATCH = -3,
};

// ISerializer: abstract interface for protocol serialization.
// Both PbSerializer and FbsSerializer implement this.
class ISerializer {
public:
    virtual ~ISerializer() = default;

    // Serialize src buffer into outMsg.
    // src: raw bytes to serialize (e.g. proto-encoded bytes)
    // srcLen: length of src
    // outMsg: output message with msgId set, payload containing serialized data
    virtual SerialError Serialize(const void* src, size_t srcLen,
                                  core::Msg& outMsg) = 0;

    // Deserialize outMsg.payload into dst buffer.
    // srcMsg: the incoming message
    // offset: byte offset within payload to start reading
    // dst: output buffer
    // dstLen: size of dst buffer
    virtual SerialError Deserialize(const core::Msg& srcMsg, size_t offset,
                                    void* dst, size_t dstLen) = 0;
};

} // namespace serial
} // namespace snc
