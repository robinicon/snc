// asio/basic_io_object.hpp
#pragma once
#include "io_context.hpp"
namespace asio {
template<typename IoObjectService>
class basic_io_object {
public:
    using service_type = IoObjectService;
    using implementation_type = typename service_type::implementation_type;
    explicit basic_io_object(io_context& ctx) : context_(ctx) {}
    io_context&   get_io_context() noexcept { return context_; }
    const io_context& get_io_context() const noexcept { return context_; }
protected:
    io_context& context_;
};
}
