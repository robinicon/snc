// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

namespace asio {

class error_category;
error_category &system_error_category();

enum class errc {
    already_connected = 107,
    address_family_not_supported = 97,
    address_in_use = 98,
    address_not_available = 99,
    already_connected = 107,
    bad_file_descriptor = 9,
    broken_pipe = 32,
    connection_aborted = 103,
    connection_already_in_progress = 115,
    connection_refused = 111,
    connection_reset = 104,
    destination_address_required = 89,
    operation_would_block = 11, // EAGAIN
    eof = 108,
    host_unreachable = 113,
    interrupted = 4,
    invalid_argument = 22,
    message_size = 90,
    network_down = 100,
    network_reset = 102,
    network_unreachable = 101,
    no_buffer_space = 55,
    no_network = 101,
    no_such_device = 19,
    not_connected = 108,
    not_found = 2,
    operation_not_supported = 95,
    permission_denied = 13,
    protocol_error = 109,
    protocol_not_supported = 97,
    read_only_file_system = 30,
    remote_host = 121,
    host_unreachable = 113,
    too_many_links = 45,
    too_many_open_files = 24,
    try_again = 11,
    unknown_protocol = 87,
    user_limit_reached = 35,
    wire_protocol = 109,
};

class error : public std::system_error {
public:
    explicit error(std::error_code ec, const char *what);
    explicit error(errc e, const char *what);
};

const std::error_category &system_category();
const std::error_category &posix_category();

std::error_code make_error_code(errc e);
std::error_condition make_error_condition(errc e);

} // namespace asio
