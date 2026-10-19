// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "snc/base/Common.h"

namespace snc {
namespace base {

struct NetBuffer {
    static const size_t DEFAULT_CAP = 4096;

    NetBuffer() : buf_(new char[DEFAULT_CAP]), cap_(DEFAULT_CAP), pos_(0), len_(0) {}
    explicit NetBuffer(size_t cap) : buf_(new char[cap]), cap_(cap), pos_(0), len_(0) {}
    ~NetBuffer() { delete[] buf_; }

    NetBuffer(const NetBuffer&) = delete;
    NetBuffer& operator=(const NetBuffer&) = delete;
    NetBuffer(NetBuffer&& other) noexcept
        : buf_(other.buf_), cap_(other.cap_), pos_(other.pos_), len_(other.len_) {
        other.buf_ = nullptr;
        other.cap_ = other.pos_ = other.len_ = 0;
    }
    NetBuffer& operator=(NetBuffer&& other) noexcept {
        if (this != &other) {
            delete[] buf_;
            buf_ = other.buf_;
            cap_ = other.cap_;
            pos_ = other.pos_;
            len_ = other.len_;
            other.buf_ = nullptr;
            other.cap_ = other.pos_ = other.len_ = 0;
        }
        return *this;
    }

    // Write a value in network byte order
    template <typename T>
    void Write(T val) {
        Expand(sizeof(T));
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&val);
        std::memcpy(buf_ + pos_, src, sizeof(T));
        pos_ += sizeof(T);
        if (pos_ > len_) len_ = pos_;
    }

    // Read a value in network byte order
    template <typename T>
    bool Read(T& val) {
        if (pos_ + sizeof(T) > len_) return false;
        uint8_t* dst = reinterpret_cast<uint8_t*>(&val);
        std::memcpy(dst, buf_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    void ResetPos() { pos_ = 0; }
    size_t Pos() const { return pos_; }
    size_t Len() const { return len_; }
    const char* Data() const { return buf_; }
    char* Data() { return buf_; }

    void Expand(size_t needed) {
        size_t new_cap = cap_;
        while (new_cap < pos_ + needed) new_cap *= 2;
        if (new_cap == cap_) return;
        char* new_buf = new char[new_cap];
        std::memcpy(new_buf, buf_, len_);
        delete[] buf_;
        buf_ = new_buf;
        cap_ = new_cap;
    }

private:
    char* buf_;
    size_t cap_;
    size_t pos_;
    size_t len_;
};

// Host to network byte order for integers
template <typename T>
SNC_FORCE_INLINE T HostToNet(T val) {
    if constexpr (sizeof(T) == 1) return val;
    T result = 0;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(&val);
    for (size_t i = 0; i < sizeof(T); ++i) {
        reinterpret_cast<uint8_t*>(&result)[sizeof(T) - 1 - i] = src[i];
    }
    return result;
}

template <typename T>
SNC_FORCE_INLINE T NetToHost(T val) {
    return HostToNet<T>(val);
}

template <typename T>
SNC_FORCE_INLINE void ReadNet(NetBuffer& buf, T& val) {
    val = NetToHost(val);
    buf.Read(val);
    val = NetToHost(val);
}

template <typename T>
SNC_FORCE_INLINE void WriteNet(NetBuffer& buf, T val) {
    val = HostToNet(val);
    buf.Write(val);
}

enum class ErrorCode : int {
    OK = 0,
    FAILURE = -1,
    INVALID_ARG = -2,
    TIMEOUT = -3,
    CONNECTION_CLOSED = -4,
    BUFFER_OVERFLOW = -5,
    PROTOCOL_ERROR = -6,
    IO_ERROR = -7,
    QUEUE_FULL = -8,
    INTERRUPTED = -9,
};

inline const char* ErrorCodeToStr(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK: return "OK";
        case ErrorCode::FAILURE: return "FAILURE";
        case ErrorCode::INVALID_ARG: return "INVALID_ARG";
        case ErrorCode::TIMEOUT: return "TIMEOUT";
        case ErrorCode::CONNECTION_CLOSED: return "CONNECTION_CLOSED";
        case ErrorCode::BUFFER_OVERFLOW: return "BUFFER_OVERFLOW";
        case ErrorCode::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case ErrorCode::IO_ERROR: return "IO_ERROR";
        case ErrorCode::QUEUE_FULL: return "QUEUE_FULL";
        case ErrorCode::INTERRUPTED: return "INTERRUPTED";
        default: return "UNKNOWN";
    }
}

// Message type classification helpers
inline bool IsClientMsg(int msgId) { return msgId >= 1000 && msgId < 2000; }
inline bool IsGateToServerMsg(int msgId) { return msgId >= 2000 && msgId < 3000; }
inline bool IsServerToServerMsg(int msgId) { return msgId >= 3000 && msgId < 4000; }
inline bool IsBroadcastMsg(int msgId) { return msgId >= 4000 && msgId < 5000; }

} // namespace base
} // namespace snc
