// asio/handler_invoke_hook.hpp
#pragma once
namespace asio {
namespace handler_invoke {
template <typename Function>
inline void invoke(Function& f) { f(); }
}
}
