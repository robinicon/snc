// asio/impl/strand.hpp — inline implementation
#pragma once
#include "../strand.hpp"
#include "../io_context.hpp"

namespace asio {

inline strand::strand(io_context& ctx) : impl_(ctx) {}

inline io_context& strand::context() noexcept { return impl_; }
inline const io_context& strand::context() const noexcept { return impl_; }

} // namespace asio
