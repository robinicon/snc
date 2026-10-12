#pragma once

#include <cstdint>
#include <memory>
#include <cstring>
#include <string>
#include "snc/base/Common.h"

namespace snc {
namespace core {

class Msg {
public:
    using Ptr = std::shared_ptr<Msg>;

    static const size_t HEADER_SIZE = 8;

    struct Header {
        int32_t msgId;
        int32_t payloadLen;
    };

    Msg() : msgId_(0), payloadLen_(0) {}
    Msg(int32_t msgId, const char* payload, size_t len)
        : msgId_(msgId), payloadLen_((int32_t)len) {
        if (payload && len > 0) {
            payload_.assign(payload, len);
        } else {
            payload_.clear();
        }
        BuildBuffer();
    }

    static Ptr Create(int32_t msgId, const char* payload, size_t len) {
        return std::make_shared<Msg>(msgId, payload, len);
    }

    static Ptr Create(int32_t msgId) {
        return std::make_shared<Msg>(msgId, nullptr, 0);
    }

    static Ptr FromBuffer(const char* data, size_t len) {
        if (len < HEADER_SIZE) return nullptr;
        Header h;
        std::memcpy(&h, data, sizeof(Header));
        int32_t pid = h.msgId;
        int32_t plen = h.payloadLen;
        if (len < (size_t)(HEADER_SIZE + plen)) return nullptr;
        auto msg = std::make_shared<Msg>(pid, data + HEADER_SIZE, plen);
        return msg;
    }

    int32_t MsgId() const { return msgId_; }
    int32_t PayloadLen() const { return payloadLen_; }
    size_t TotalLen() const { return HEADER_SIZE + (size_t)payloadLen_; }

    const char* Data() const { return buf_.data(); }
    char* Data() { return buf_.data(); }

    const char* Payload() const { return payload_.data(); }
    const uint8_t* PayloadBytes() const { return reinterpret_cast<const uint8_t*>(payload_.data()); }

private:
    void BuildBuffer() {
        buf_.resize(HEADER_SIZE + payload_.size());
        Header h{msgId_, payloadLen_};
        std::memcpy(buf_.data(), &h, sizeof(Header));
        if (!payload_.empty()) {
            std::memcpy(buf_.data() + HEADER_SIZE, payload_.data(), payload_.size());
        }
    }

    int32_t msgId_;
    int32_t payloadLen_;
    std::string payload_;
    mutable std::string buf_;
};

} // namespace core
} // namespace snc
