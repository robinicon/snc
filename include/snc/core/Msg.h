// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <memory>
#include <cstring>
#include <string>
#include "snc/base/Common.h"

namespace snc {
namespace core {

/// Msg — 消息封装类，采用 [msgId(int32)][payloadLen(int32)][payload] 定长头部协议。
/// 所有字段采用主机字节序，发送前由上层统一转换。
class Msg {
public:
    using Ptr = std::shared_ptr<Msg>;

    static const size_t HEADER_SIZE = 8;  // 消息头固定 8 字节（msgId 4B + payloadLen 4B）

    struct Header {
        int32_t msgId;   ///< 消息类型 ID
        int32_t payloadLen; ///< payload 有效字节数（不含头部）
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

    /// 工厂方法：创建带 payload 的消息
    static Ptr Create(int32_t msgId, const char* payload, size_t len) {
        return std::make_shared<Msg>(msgId, payload, len);
    }

    /// 工厂方法：创建空 payload 的消息（如心跳 ping）
    static Ptr Create(int32_t msgId) {
        return std::make_shared<Msg>(msgId, nullptr, 0);
    }

    /// 从原始字节流解析消息，校验长度后返回；失败返回 nullptr
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

    int32_t MsgId()  const { return msgId_; }
    int32_t PayloadLen() const { return payloadLen_; }
    size_t TotalLen() const { return HEADER_SIZE + (size_t)payloadLen_; }

    /// 返回序列化后的完整字节（含头部）
    const char* Data() const { return buf_.data(); }
    char* Data() { return buf_.data(); }

    /// 返回 payload 起始指针（不含头部）
    const char* Payload() const { return payload_.data(); }
    const uint8_t* PayloadBytes() const { return reinterpret_cast<const uint8_t*>(payload_.data()); }

private:
    /// 将 msgId + payloadLen + payload 序列化到 buf_
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
    std::string payload_;          ///< 原始 payload
    mutable std::string buf_;      ///< 序列化后的完整字节流（含头部）
};

} // namespace core
} // namespace snc
