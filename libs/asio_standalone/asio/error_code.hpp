// asio/error_code.hpp — Minimal standalone implementation
#pragma once

#include <cerrno>
#include <system_error>
#include <string>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#endif

namespace asio {
namespace error {

enum basic_errors {
    already_open                  = 1,
    broken_pipes                  = 2,
    connection_aborted            = 3,
    connection_refused            = 4,
    connection_reset              = 5,
    end_of_file                   = 6,
    host_unreachable              = 8,
    interrupted                   = 9,
    invalid_argument              = 10,
    message_size                  = 11,
    network_down                  = 12,
    network_reset                 = 13,
    network_unreachable           = 14,
    no_descriptors                = 15,
    no_permission                 = 16,
    no_protocol_option            = 17,
    no_route_to_host              = 18,
    operation_not_supported       = 19,
    protocol_error                = 20,
    protocol_not_supported        = 21,
    resource_deadlock_would_occur = 24,
    resource_unavailable_try_again= 25,
    success                       = 26,
    wrong_protocol_type           = 28,
    operation_aborted             = 9999,
    would_block                   = 100,
    address_family_not_supported  = 101,
    address_already_in_use        = 102,
    no_buffer_space               = 103,
    timed_out                     = 104
};

} // namespace error

// error_code is just std::error_code — enables implicit conversion from
// make_error_code(error::basic_errors) via ADL.
using error_code       = std::error_code;
using error_condition = std::error_condition;

inline error_code make_error_code(error::basic_errors e) noexcept {
    return error_code(static_cast<int>(e), std::generic_category());
}

inline error_condition make_error_condition(error::basic_errors e) noexcept {
    return error_condition(static_cast<int>(e), std::generic_category());
}

// Comparison operators for error_code with basic_errors
inline bool operator==(const error_code& ec, error::basic_errors e) noexcept {
    return ec == error_code(static_cast<int>(e), std::generic_category());
}

inline bool operator!=(const error_code& ec, error::basic_errors e) noexcept {
    return !(ec == e);
}

inline bool operator==(error::basic_errors e, const error_code& ec) noexcept {
    return ec == e;
}

inline bool operator!=(error::basic_errors e, const error_code& ec) noexcept {
    return !(ec == e);
}

} // namespace asio

// boost::system compatibility shim
namespace boost {
namespace system {

class error_category : public std::error_category {
public:
    const char* name() const noexcept override { return "asio"; }
    std::string message(int val) const override { return std::system_category().message(val); }
};

using error_code       = std::error_code;
using error_condition = std::error_condition;

} // namespace system
} // namespace boost
