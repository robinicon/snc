// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <chrono>
#include <functional>
#include <queue>
#include "error_code.hpp"
#include "basic_io_object.hpp"
#include "handler_invoke_hook.hpp"

namespace asio {

class io_context : public basic_io_object<io_context> {
public:
    using clock_type = std::chrono::steady_clock;
    using time_type  = clock_type::time_point;
    using duration_type = clock_type::duration;

    io_context();
    ~io_context();
    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    void run();
    void run_one();
    std::size_t poll();
    std::size_t poll_one();
    void stop();
    bool stopped() const;
    void reset();
    bool is_stopped() const;

    void post(std::function<void()> fn);
    void dispatch(std::function<void()> fn);

    template <typename Token>
    auto post_immediate(Token &&token);

private:
    struct task { std::function<void()> fn; time_type due; };
    std::queue<task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

} // namespace asio
