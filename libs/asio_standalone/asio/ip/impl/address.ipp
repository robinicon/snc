// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.
// asio/ip/impl/address.ipp — minimal address implementation
#pragma once
#include "address.hpp"
#include <sstream>
#include <iomanip>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif

namespace asio {
namespace ip {

inline address address::v4(uint32_t net_byte_order) {
    address a;
    a.family_ = AF_INET;
    auto* sa = reinterpret_cast<sockaddr_in*>(&a.addr_);
    sa->sin_family      = AF_INET;
    sa->sin_addr.s_addr = net_byte_order;
    sa->sin_port        = 0;
    return a;
}

inline address address::v6(const uint8_t (&bytes)[16]) {
    address a;
    a.family_ = AF_INET6;
    auto* sa = reinterpret_cast<sockaddr_in6*>(&a.addr_);
    sa->sin6_family = AF_INET6;
    std::memcpy(sa->sin6_addr.s6_addr, bytes, 16);
    sa->sin6_port = 0;
    return a;
}

inline std::string address::to_string() const {
    char buf[INET6_ADDRSTRLEN] = {};
    if (family_ == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&addr_)->sin_addr, buf, sizeof(buf));
    } else if (family_ == AF_INET6) {
        inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&addr_)->sin6_addr, buf, sizeof(buf));
    }
    return std::string(buf);
}

} // namespace ip
} // namespace asio
