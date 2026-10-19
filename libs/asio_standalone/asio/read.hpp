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

// Minimal async_result: wraps a promise/future pair.
template <typename Signature>
struct async_result_impl;

// read and write
std::size_t read(io_context &ctx, const buffer &buf, asio::error_code &ec);
std::size_t write(io_context &ctx, const buffer &buf, asio::error_code &ec);

} // namespace asio
