// asio/io_context.hpp — select()-based io_context for Windows/POSIX
#pragma once

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define ASIO_SOCKET_INVALID INVALID_SOCKET
  #define ASIO_SOCKET_CLOSE(s) closesocket(s)
  #define ASIO_SET_NONBLOCK(s) { u_long arg = 1; ioctlsocket(s, FIONBIO, &arg); }
#else
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  #include <fcntl.h>
  #define ASIO_SOCKET_INVALID (-1)
  #define ASIO_SOCKET_CLOSE(s) close(s)
  #define ASIO_SET_NONBLOCK(s) fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK)
#endif

#include "error.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <set>

namespace asio {

namespace chrono = std::chrono;

class io_context {
public:
    // Platform-specific native socket handle type
#ifdef _WIN32
    using native_handle_type = SOCKET;
#else
    using native_handle_type = int;
#endif

    class executor {
        friend class io_context;
    public:
        executor() noexcept : ctx_(nullptr) {}
        explicit executor(io_context* ctx) noexcept : ctx_(ctx) {}
        executor(const executor&) noexcept = default;
        executor& operator=(const executor&) noexcept = default;
        bool operator==(const executor& o) const noexcept { return ctx_ == o.ctx_; }
        bool operator!=(const executor& o) const noexcept { return ctx_ != o.ctx_; }
        io_context* context() const noexcept { return ctx_; }
    private:
        io_context* ctx_ = nullptr;
    };

    using executor_type = executor;

    io_context() = default;
    explicit io_context(size_t /*concurrency_hint*/) {}
    ~io_context() { stop(); }

    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;

    executor get_executor() noexcept { return executor(this); }

    // ── Run ──────────────────────────────────────────────────────────────
    void run() {
        if (stopped_.load(std::memory_order_acquire)) return;
        running_.store(true, std::memory_order_release);
        while (!stopped_.load(std::memory_order_acquire)) {
            process_pending();
            if (!has_work()) wait_for_work();
        }
        running_.store(false, std::memory_order_release);
    }

    std::size_t poll_one() {
        return try_one() ? 1 : 0;
    }

    void stop() {
        stopped_.store(true, std::memory_order_release);
        wakeup_.store(true, std::memory_order_release);
        cond_.notify_all();
        running_.store(false, std::memory_order_release);
    }

    bool is_stopped() const noexcept {
        return stopped_.load(std::memory_order_acquire);
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    // ── post ─────────────────────────────────────────────────────────────
    void post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(qmux_);
            queue_.push(std::move(fn));
        }
        pending_count_.fetch_add(1, std::memory_order_relaxed);
        wakeup_.store(true, std::memory_order_release);
        cond_.notify_one();
    }

    void impl_post_unlocked_(std::function<void()> fn) {
        queue_.push(std::move(fn));
    }

    void wakeup_from_strand_() {
        wakeup_.store(true, std::memory_order_release);
        cond_.notify_one();
    }

    // ── Registered socket tracking ───────────────────────────────────────
    void register_socket(native_handle_type fd) {
        if (fd == ASIO_SOCKET_INVALID) return;
        std::lock_guard<std::mutex> lock(sockmux_);
        sock_set_.insert(fd);
    }

    void unregister_socket(native_handle_type fd) {
        if (fd == ASIO_SOCKET_INVALID) return;
        std::lock_guard<std::mutex> lock(sockmux_);
        sock_set_.erase(fd);
    }

    void notify_socket_readable(native_handle_type fd) {
        (void)fd;
        wakeup_.store(true, std::memory_order_release);
        cond_.notify_one();
    }

private:
    void process_pending() {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(qmux_);
            if (queue_.empty()) return;
            fn = std::move(queue_.front());
            queue_.pop();
        }
        if (fn) { fn(); pending_count_.fetch_sub(1, std::memory_order_relaxed); }
    }

    bool try_one() {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(qmux_);
            if (queue_.empty()) return false;
            fn = std::move(queue_.front());
            queue_.pop();
        }
        if (fn) { fn(); pending_count_.fetch_sub(1, std::memory_order_relaxed); return true; }
        return false;
    }

    bool has_work() {
        std::lock_guard<std::mutex> lock(qmux_);
        return !queue_.empty();
    }

    void wait_for_work() {
        std::set<native_handle_type> fds;
        {
            std::lock_guard<std::mutex> lock(sockmux_);
            fds = sock_set_;
        }
        if (fds.empty()) {
            std::unique_lock<std::mutex> lk(condmux_);
            cond_.wait_for(lk, std::chrono::milliseconds(50));
            return;
        }
        fd_set rset;
        FD_ZERO(&rset);
        native_handle_type maxfd = ASIO_SOCKET_INVALID;
        for (auto fd : fds) {
            FD_SET(fd, &rset);
            if (fd > maxfd) maxfd = fd;
        }
        timeval tv{0, 10000};
        int ret = static_cast<int>(::select(static_cast<int>(maxfd) + 1, &rset, nullptr, nullptr, &tv));
        if (ret > 0) {
            wakeup_.store(true, std::memory_order_release);
            cond_.notify_one();
        }
    }

    std::atomic<bool>       stopped_{false};
    std::atomic<bool>       running_{false};
    std::atomic<bool>       wakeup_{false};
    std::atomic<int>        pending_count_{0};
    std::queue<std::function<void()>> queue_;
    std::mutex              qmux_;
    std::mutex              condmux_;
    std::condition_variable cond_;
    std::set<native_handle_type> sock_set_;
    std::mutex              sockmux_;
};

inline io_context::native_handle_type invalid_socket() noexcept { return ASIO_SOCKET_INVALID; }

} // namespace asio
