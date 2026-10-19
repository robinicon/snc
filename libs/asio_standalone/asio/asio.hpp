// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.
// Provides io_context via select() on Windows, with strand, socket, timer.

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
#include "ip/tcp.hpp"
#include "ip/address.hpp"
