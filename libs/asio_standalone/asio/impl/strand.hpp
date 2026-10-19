// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.
// asio/impl/strand.hpp — inline implementation
#pragma once
#include "../strand.hpp"
#include "../io_context.hpp"

namespace asio {

inline strand::strand(io_context& ctx) : impl_(ctx) {}

inline io_context& strand::context() noexcept { return impl_; }
inline const io_context& strand::context() const noexcept { return impl_; }

} // namespace asio
