// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <functional>
#include <vector>

namespace asio {

class io_context;

// A strand ensures handlers are executed sequentially.
class strand {
public:
    explicit strand(io_context &ctx);
    ~strand();
    strand(const strand &) = delete;
    strand &operator=(const strand &) = delete;

    template <typename Handler>
    void dispatch(Handler &&h);

    template <typename Handler>
    void post(Handler &&h);

private:
    io_context &ctx_;
    std::vector<std::function<void()>> pending_;
    bool running_ = false;
};

} // namespace asio
