// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <cstdint>
#include <cstring>
#include "error_code.hpp"

namespace asio {

class basic_socket {
public:
    explicit basic_socket(io_context &ctx);
    ~basic_socket();
    basic_socket(const basic_socket &) = delete;
    basic_socket &operator=(const basic_socket &) = delete;

    void close();
    void close(asio::error_code &ec);
    bool is_open() const;

protected:
    io_context &ctx_;
    int fd_ = -1;
};

} // namespace asio
