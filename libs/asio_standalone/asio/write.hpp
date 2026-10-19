// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <functional>
#include "buffer.hpp"
#include "error.hpp"

namespace asio {

class io_context;

std::size_t read(io_context &ctx, const buffer &buf, asio::error_code &ec);
std::size_t write(io_context &ctx, const buffer &buf, asio::error_code &ec);

} // namespace asio
