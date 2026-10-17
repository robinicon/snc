// asio/async_result.hpp — Minimal async_result for void(error_code, size_t)
#pragma once
#include "error_code.hpp"
#include <cstddef>

namespace asio {

template<typename Signature>
struct async_result;

template<typename Return, typename Arg1, typename Arg2>
struct async_result<Return(Arg1, Arg2)> {
    using completion_handler_type = Arg1;
    using return_type = Return;

    completion_handler_type& completion_handler;
    async_result(completion_handler_type& h) : completion_handler(h) {}
    return_type get() { return Return(); }
};

template<typename Arg1, typename Arg2>
struct async_result<void(Arg1, Arg2)> {
    using completion_handler_type = Arg1;
    using return_type = void;

    completion_handler_type& completion_handler;
    async_result(completion_handler_type& h) : completion_handler(h) {}
    return_type get() {}
};

} // namespace asio
