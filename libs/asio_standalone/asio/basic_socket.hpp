// asio/basic_socket.hpp — minimal basic_socket template
#pragma once
#include "io_context.hpp"
#include "error_code.hpp"

namespace asio {

template<typename SocketService>
class basic_socket {
public:
    using executor_type = io_context::executor;
    using native_handle_type = typename SocketService::native_handle_type;

    explicit basic_socket(io_context& ctx) : ctx_(ctx) {}
    explicit basic_socket(const executor_type& ex) : ctx_(*ex.context()) {}

    io_context& get_io_context() { return ctx_; }
    const io_context& get_io_context() const { return ctx_; }
    executor_type get_executor() noexcept { return ctx_.get_executor(); }

protected:
    io_context& ctx_;
};

} // namespace asio
