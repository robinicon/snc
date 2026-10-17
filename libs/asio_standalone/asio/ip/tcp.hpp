// asio/ip/tcp.hpp — tcp socket, acceptor, and protocol options
#pragma once
#include "../io_context.hpp"
#include "../error.hpp"
#include "../buffer.hpp"
#include "basic_endpoint.hpp"
#include "address.hpp"
#include <memory>
#include <functional>
#include <vector>
#include <cstring>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

namespace asio {
namespace ip {

// --------------------------------------------------------------------------
// no_delay option
// --------------------------------------------------------------------------
struct no_delay {
    explicit no_delay(bool enabled) : enabled_(enabled) {}
    template<typename SocketType>
    void set_option(SocketType& sock, io_context&) const {
        int opt = enabled_ ? 1 : 0;
#ifdef _WIN32
        setsockopt(sock.native_handle(), IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(sock.native_handle(), IPPROTO_TCP, TCP_NODELAY,
                   &opt, sizeof(opt));
#endif
    }
    bool enabled_;
};

inline no_delay nop(const no_delay&) { return no_delay(true); }

// --------------------------------------------------------------------------
// reuse_address option
// --------------------------------------------------------------------------
struct reuse_address {
    explicit reuse_address(bool enabled) : enabled_(enabled) {}
    template<typename AcceptorType>
    void set_option(AcceptorType& acc, io_context&) const {
        int opt = enabled_ ? 1 : 0;
#ifdef _WIN32
        setsockopt(acc.native_handle(), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(acc.native_handle(), SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt));
#endif
    }
    bool enabled_;
};

inline reuse_address reusr(const reuse_address&) { return reuse_address(true); }

// --------------------------------------------------------------------------
// protocol (v4 marker)
// --------------------------------------------------------------------------
struct v4_type {};
inline v4_type v4() { return v4_type{}; }

// --------------------------------------------------------------------------
// basic_stream_socket — wraps a TCP socket with async read/write via select()
// --------------------------------------------------------------------------
class basic_stream_socket {
public:
    using protocol_type  = struct tcp;
    using executor_type  = io_context::executor;
    using native_handle_type = io_context::native_handle_type;

    explicit basic_stream_socket(io_context& ctx)
        : ctx_(&ctx), fd_(ASIO_SOCKET_INVALID), is_open_(false)
    {}

    explicit basic_stream_socket(const executor_type& ex)
        : ctx_(ex.context()), fd_(ASIO_SOCKET_INVALID), is_open_(false)
    {}

    basic_stream_socket(basic_stream_socket&& other) noexcept
        : ctx_(other.ctx_), fd_(other.fd_), is_open_(other.is_open_)
    { other.fd_ = ASIO_SOCKET_INVALID; other.is_open_ = false; }

    basic_stream_socket& operator=(basic_stream_socket&& other) noexcept {
        if (this != &other) {
            close();
            ctx_     = other.ctx_;
            fd_      = other.fd_;
            is_open_ = other.is_open_;
            other.fd_      = ASIO_SOCKET_INVALID;
            other.is_open_ = false;
        }
        return *this;
    }

    basic_stream_socket(const basic_stream_socket&) = delete;
    basic_stream_socket& operator=(const basic_stream_socket&) = delete;

    ~basic_stream_socket() { close(); }

    io_context&        get_io_context() { return *ctx_; }
    const io_context&  get_io_context() const { return *ctx_; }
    bool               is_open() const noexcept { return is_open_; }
    native_handle_type native_handle() const noexcept { return fd_; }

    void close() {
        if (is_open_ && fd_ != ASIO_SOCKET_INVALID) {
            ASIO_SOCKET_CLOSE(fd_);
            fd_ = ASIO_SOCKET_INVALID;
            is_open_ = false;
            if (ctx_) ctx_->unregister_socket(fd_);
        }
    }

    void close(error_code& ec) {
        close();
        ec = error_code();
    }

    native_handle_type release() {
        native_handle_type f = fd_;
        fd_ = ASIO_SOCKET_INVALID;
        is_open_ = false;
        return f;
    }

    void open(const v4_type&) {
        close();
        fd_ = static_cast<native_handle_type>(::socket(AF_INET, SOCK_STREAM, 0));
        if (fd_ != ASIO_SOCKET_INVALID) {
            is_open_ = true;
            if (ctx_) ctx_->register_socket(fd_);
        }
    }

    // Called by acceptor after accepting a connection
    void attach(native_handle_type fd, io_context& ctx) {
        close();
        ctx_ = &ctx;
        fd_ = fd;
        is_open_ = true;
        ctx.register_socket(fd);
    }

    template<typename Option>
    void set_option(const Option& opt, error_code& ec) {
        try { opt.set_option(*this, *ctx_); ec = error_code(); }
        catch (...) { ec = error_code(errno, std::generic_category()); }
    }

    template<typename Option>
    void set_option(const Option& opt) {
        error_code ec;
        set_option(opt, ec);
    }

    executor_type get_executor() noexcept { return ctx_ ? ctx_->get_executor() : executor_type(); }

    // ── async_read ───────────────────────────────────────────────────
    template<typename MutableBufferSequence, typename ReadHandler>
    void async_read(MutableBufferSequence buf, ReadHandler handler) {
        auto* state = new AsyncOpState{ std::move(handler), this, buf };
        pending_ops_[fd_] = state;
        if (ctx_) ctx_->notify_socket_readable(fd_);
        try_async_read_(state);
    }

    // ── async_write ──────────────────────────────────────────────────
    template<typename ConstBufferSequence, typename WriteHandler>
    void async_write(ConstBufferSequence buf, WriteHandler handler) {
        auto* state = new AsyncOpState{ std::move(handler), this, buf };
        pending_ops_[fd_] = state;
        if (ctx_) ctx_->notify_socket_readable(fd_);
        try_async_write_(state);
    }

    void cancel(error_code&) {}

private:
    struct AsyncOpState {
        std::function<void(error_code, std::size_t)> handler;
        basic_stream_socket* socket;
        mutable_buffer buf;
    };

    std::mutex                              ops_mutex_;
    std::unordered_map<native_handle_type, AsyncOpState*> pending_ops_;

    io_context*         ctx_;
    native_handle_type  fd_;
    bool                is_open_;

    void try_async_read_(AsyncOpState* st) {
        if (!is_open_) { st->handler(error_code(EBADF, std::generic_category()), 0); cleanup_(st); return; }
        const char* p = static_cast<const char*>(st->buf.data());
        std::size_t n = st->buf.size();
        if (n == 0) { st->handler(error_code(), 0); cleanup_(st); return; }
#ifdef _WIN32
        int r = static_cast<int>(::recv(fd_, const_cast<char*>(p), static_cast<unsigned long>(n), 0));
#else
        ssize_t r = ::recv(fd_, const_cast<char*>(p), n, 0);
#endif
        if (r > 0) {
            st->handler(error_code(), static_cast<std::size_t>(r));
            cleanup_(st);
        } else if (r == 0) {
            st->handler(error_code(), 0);
            cleanup_(st);
        } else {
            int err = 0;
#ifdef _WIN32
            err = WSAGetLastError();
#else
            err = errno;
#endif
            if (err == WSAEWOULDBLOCK || err == EAGAIN || err == EWOULDBLOCK) {
                if (ctx_) {
                    ctx_->post([this, st]() {
                        std::lock_guard<std::mutex> lock(ops_mutex_);
                        auto it = pending_ops_.find(fd_);
                        if (it == pending_ops_.end() || it->second != st) return;
                        try_async_read_(st);
                    });
                }
            } else {
                st->handler(error_code(err, std::generic_category()), 0);
                cleanup_(st);
            }
        }
    }

    void try_async_write_(AsyncOpState* st) {
        if (!is_open_) { st->handler(error_code(EBADF, std::generic_category()), 0); cleanup_(st); return; }
        const char* p = static_cast<const char*>(st->buf.data());
        std::size_t n = st->buf.size();
        if (n == 0) { st->handler(error_code(), 0); cleanup_(st); return; }
#ifdef _WIN32
        int r = static_cast<int>(::send(fd_, p, static_cast<unsigned long>(n), 0));
#else
        ssize_t r = ::send(fd_, p, n, 0);
#endif
        if (r > 0) {
            st->handler(error_code(), static_cast<std::size_t>(r));
            cleanup_(st);
        } else {
            int err = 0;
#ifdef _WIN32
            err = WSAGetLastError();
#else
            err = errno;
#endif
            if (err == WSAEWOULDBLOCK || err == EAGAIN || err == EWOULDBLOCK) {
                if (ctx_) {
                    ctx_->post([this, st]() {
                        std::lock_guard<std::mutex> lock(ops_mutex_);
                        auto it = pending_ops_.find(fd_);
                        if (it == pending_ops_.end() || it->second != st) return;
                        try_async_write_(st);
                    });
                }
            } else {
                st->handler(error_code(err, std::generic_category()), 0);
                cleanup_(st);
            }
        }
    }

    void cleanup_(AsyncOpState* st) {
        std::lock_guard<std::mutex> lock(ops_mutex_);
        auto it = pending_ops_.find(fd_);
        if (it != pending_ops_.end() && it->second == st)
            pending_ops_.erase(it);
        delete st;
    }
};

// --------------------------------------------------------------------------
// tcp protocol type — defined BEFORE acceptor so endpoint/socket are known
// --------------------------------------------------------------------------
struct tcp {
    using endpoint  = basic_endpoint<tcp>;
    using socket    = basic_stream_socket;
    using acceptor  = class acceptor;  // forward-declared; defined below via friend
    using no_delay  = ::asio::ip::no_delay;
    using reuse_address = ::asio::ip::reuse_address;
    static endpoint endpoint_from_raw(const char* bytes, uint16_t port) {
        return endpoint(address::make_from_raw(bytes), port);
    }
    static v4_type v4() { return ::asio::ip::v4(); }
};

// --------------------------------------------------------------------------
// tcp::acceptor
// --------------------------------------------------------------------------
class acceptor {
public:
    using executor_type  = io_context::executor;
    using native_handle_type = io_context::native_handle_type;
    using socket_type    = basic_stream_socket;

    explicit acceptor(io_context& ctx) : ctx_(&ctx), fd_(ASIO_SOCKET_INVALID), is_open_(false) {}
    ~acceptor() { close(); }
    acceptor(const acceptor&) = delete;
    acceptor& operator=(const acceptor&) = delete;

    void open(const v4_type&) {
        close();
        fd_ = static_cast<native_handle_type>(::socket(AF_INET, SOCK_STREAM, 0));
        if (fd_ != ASIO_SOCKET_INVALID) {
            is_open_ = true;
            ctx_->register_socket(fd_);
        }
    }

    void open(const tcp&) { open(v4_type{}); }

    template<typename Option>
    void set_option(const Option& opt) {
        error_code ec;
        opt.set_option(*this, *ctx_); (void)ec;
    }

    void bind(const tcp::endpoint& ep) {
        if (!is_open_) return;
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(ep.port());
        addr.sin_addr.s_addr = INADDR_ANY;
        ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    void listen(int backlog = 128) {
        if (!is_open_) return;
        ::listen(fd_, backlog);
    }

    void close(error_code& ec) {
        if (is_open_ && fd_ != ASIO_SOCKET_INVALID) {
            ASIO_SOCKET_CLOSE(fd_);
            fd_ = ASIO_SOCKET_INVALID;
            is_open_ = false;
            if (ctx_) ctx_->unregister_socket(fd_);
        }
        ec = error_code();
    }

    void close() { error_code ec; close(ec); }

    bool         is_open()   const noexcept { return is_open_; }
    native_handle_type native_handle() const noexcept { return fd_; }
    executor_type get_executor() noexcept { return ctx_ ? ctx_->get_executor() : executor_type(); }

    // async_accept: accept one connection and invoke cb(ec)
    void async_accept(socket_type& peer, std::function<void(error_code)> cb) {
        auto* state = new AcceptState{ &peer, std::move(cb), this };
        do_accept_once_(state);
    }

private:
    struct AcceptState {
        basic_stream_socket* peer;
        std::function<void(error_code)> cb;
        acceptor* owner;
    };

    void do_accept_once_(AcceptState* st) {
        if (!st->owner->is_open_) { st->cb(error_code(EBADF, std::generic_category())); delete st; return; }

        sockaddr_in addr{};
        socklen_t   addrlen = sizeof(addr);
        native_handle_type client_fd = ::accept(st->owner->fd_, reinterpret_cast<sockaddr*>(&addr), &addrlen);

        if (client_fd == ASIO_SOCKET_INVALID) {
            int err = 0;
#ifdef _WIN32
            err = WSAGetLastError();
#else
            err = errno;
#endif
            if (err == WSAEWOULDBLOCK || err == EAGAIN || err == EWOULDBLOCK) {
                if (st->owner->ctx_) {
                    st->owner->ctx_->post([this, st]() { do_accept_once_(st); });
                }
                return;
            }
            st->cb(error_code(err, std::generic_category()));
            delete st;
            return;
        }

        // Set non-blocking
#ifdef _WIN32
        u_long arg = 1;
        ioctlsocket(client_fd, FIONBIO, &arg);
#else
        fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);
#endif

        // Transfer socket into peer via public method — no private access
        st->peer->attach(client_fd, *st->owner->ctx_);

        st->cb(error_code());
        delete st;
    }

    io_context* ctx_;
    native_handle_type fd_;
    bool is_open_;
};

} // namespace ip

// --------------------------------------------------------------------------
// posix namespace stub — stream_descriptor is POSIX-only, not used on Windows
// --------------------------------------------------------------------------
namespace posix {
class stream_descriptor {
public:
    using native_handle_type = int;
    explicit stream_descriptor(io_context&) {}
    ~stream_descriptor() = default;
    stream_descriptor(const stream_descriptor&) = delete;
    stream_descriptor& operator=(const stream_descriptor&) = delete;
    void cancel() {}
    void wait_read() {}
    void wait_write() {}
    void close() {}
    native_handle_type native_handle() const { return -1; }
};
} // namespace posix

// --------------------------------------------------------------------------
// basic_endpoint<tcp> v4_type constructor — defined after tcp is complete
// --------------------------------------------------------------------------
namespace ip {
inline basic_endpoint<tcp>::basic_endpoint(v4_type, uint16_t port)
    : basic_endpoint(tcp{}, port)
{}
} // namespace ip

} // namespace asio
