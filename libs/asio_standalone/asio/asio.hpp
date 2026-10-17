// asio/asio.hpp — Minimal standalone Asio-compatible header
//
// Provides only the subset of Boost.Asio needed by the SNC Gate server.
// Implements io_context via select() on Windows, with strand, socket, timer.
#pragma once

#include "error.hpp"
#include "error_code.hpp"
#include "buffer.hpp"
#include "read.hpp"
#include "write.hpp"
#include "io_context.hpp"
#include "strand.hpp"
#include "steady_timer.hpp"
#include "basic_io_object.hpp"
#include "basic_socket.hpp"
#include "ip/address.hpp"
#include "ip/basic_endpoint.hpp"
#include "ip/tcp.hpp"
#include "async_result.hpp"
