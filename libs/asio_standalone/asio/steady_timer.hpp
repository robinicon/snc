// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <chrono>
#include <functional>
#include "io_context.hpp"
#include "error_code.hpp"

namespace asio {

class steady_timer {
public:
    using clock_type = std::chrono::steady_clock;
    using time_type  = clock_type::time_point;
    using duration_type = clock_type::duration;

    explicit steady_timer(io_context &ctx);
    ~steady_timer();
    steady_timer(const steady_timer &) = delete;
    steady_timer &operator=(const steady_timer &) = delete;

    void expires_at(time_type when);
    time_type expires_at() const;
    void expires_after(duration_type d);
    duration_type expires_after() const;
    void wait();
    void wait(asio::error_code &ec);

    template <typename CompletionToken>
    auto async_wait(CompletionToken &&token);

private:
    io_context &ctx_;
    time_type deadline_;
    bool cancelled_ = false;
};

} // namespace asio
