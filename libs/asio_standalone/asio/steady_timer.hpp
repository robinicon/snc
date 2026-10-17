// asio/steady_timer.hpp — Minimal steady_timer that integrates with io_context
#pragma once

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#endif

#include "io_context.hpp"
#include "error.hpp"

#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace asio {

class steady_timer {
public:
    using executor_type = io_context::executor;
    using time_point    = std::chrono::steady_clock::time_point;
    using duration      = std::chrono::steady_clock::duration;

    explicit steady_timer(io_context& ctx)
        : ctx_(ctx)
        , expires_at_((std::chrono::steady_clock::time_point::max)())
        , active_(false)
    {}

    steady_timer(io_context& ctx, const duration& rel)
        : ctx_(ctx)
        , expires_at_(std::chrono::steady_clock::now() + rel)
        , active_(false)
    {}

    steady_timer(const steady_timer&) = delete;
    steady_timer& operator=(const steady_timer&) = delete;

    ~steady_timer() { cancel(); }

    executor_type get_executor() noexcept { return ctx_.get_executor(); }

    void cancel() {
        std::lock_guard<std::mutex> lock(mux_);
        active_ = false;
        ready_.notify_all();
    }

    void expires_at(time_point tp) {
        std::lock_guard<std::mutex> lock(mux_);
        expires_at_ = tp;
    }

    time_point expiry() const { return expires_at_; }

    // async_wait: post a timer callback that checks expiry on the io_context
    template<typename WaitHandler>
    void async_wait(WaitHandler&& handler) {
        auto* state = new TimerState{ std::forward<WaitHandler>(handler) };
        std::lock_guard<std::mutex> lock(mux_);
        if (active_) delete last_state_;
        last_state_ = state;
        active_ = true;
        // Wake up io_context so it can check timers
        ctx_.post([this]() { check_timers(); });
    }

    // Called from io_context run loop to process pending timers
    void check_timers() {
        std::unique_lock<std::mutex> lock(mux_);
        if (!active_) return;
        auto now = std::chrono::steady_clock::now();
        auto wait = expires_at_ - now;
        if (wait > duration{0}) {
            ready_.wait_for(lock, wait, [this]{ return !active_; });
        }
        if (!active_) return;
        auto* state = last_state_;
        last_state_ = nullptr;
        active_ = false;
        lock.unlock();
        state->handler(error_code());
        delete state;
    }

private:
    struct TimerState {
        std::function<void(error_code)> handler;
    };

    io_context&        ctx_;
    time_point         expires_at_;
    std::mutex         mux_;
    std::condition_variable ready_;
    bool               active_;
    TimerState*        last_state_ = nullptr;
};

} // namespace asio
