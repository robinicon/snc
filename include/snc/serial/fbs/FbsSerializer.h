#pragma once

#include <vector>

#ifdef SNC_ENABLE_FLATBUFFERS
#include <flatbuffers/flatbuffers.h>
#else
#pragma message("snc_serial_fbs: FlatBuffers disabled — stub mode (all operations return SER_FAILED)")
// Stub: no-op types for compile-only compatibility
namespace flatbuffers {
class FlatBufferBuilder {};
template<typename T> struct Offset {};
}
#endif

#include "snc/serial/ISerializer.h"
#include "snc/core/Msg.h"

namespace snc {
namespace serial {
namespace fbs {

// FbsSerializer wraps a FlatBuffers-built buffer for serialization.
// In stub mode (no flatbuffers), all methods are no-ops returning error codes.
class FbsSerializer : public ISerializer {
public:
    SerialError Serialize(const void* src, size_t srcLen,
                          core::Msg& outMsg) override;
    SerialError Deserialize(const core::Msg& srcMsg, size_t offset,
                            void* dst, size_t dstLen) override;

    // Encode a FlatBuffers bytes into length-prefixed wire format.
    // Returns empty vector on failure. Caller owns the result.
    static std::vector<uint8_t> Encode(const char* fbs_data, size_t fbs_len,
                                       size_t& out_len);

    // Decode length-prefixed bytes back into raw FlatBuffers data.
    static bool Decode(const uint8_t* data, size_t len, std::string& out);
};

} // namespace fbs
} // namespace serial
} // namespace snc
