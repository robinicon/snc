// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.
// asio/ip/address.hpp — Minimal IPv4 address
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
#endif

namespace asio {
namespace ip {

class address {
public:
    address() : addr_(0) {}
    explicit address(uint32_t a) : addr_(a) {}

    static address make_from_raw(const char* bytes) {
        address addr;
        std::memcpy(&addr.addr_, bytes, 4);
        return addr;
    }

    std::array<uint8_t, 4> to_bytes() const {
        std::array<uint8_t, 4> result = {{
            static_cast<uint8_t>((addr_ >> 24) & 0xff),
            static_cast<uint8_t>((addr_ >> 16) & 0xff),
            static_cast<uint8_t>((addr_ >> 8)  & 0xff),
            static_cast<uint8_t>( addr_ & 0xff)
        }};
        return result;
    }

    uint32_t to_v4() const { return addr_; }

private:
    uint32_t addr_ = 0;
};

inline address make_address(const char* str) {
    uint32_t val = 0;
    inet_pton(AF_INET, str, &val);
    return address(val);
}

inline address make_address(const std::string& str) {
    return make_address(str.c_str());
}

} // namespace ip
} // namespace asio
