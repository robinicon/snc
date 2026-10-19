// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <cstddef>
#include <cstdint>
#include "basic_string_view.hpp"

namespace asio {

class buffer {
public:
    buffer(void *data, size_t size);
    buffer(const void *data, size_t size);
    void *data();
    const void *data() const;
    size_t size() const;
private:
    void *data_;
    size_t size_;
};

inline buffer::buffer(void *data, size_t size) : data_(data), size_(size) {}
inline buffer::buffer(const void *data, size_t size) : data_(const_cast<void *>(data)), size_(size) {}
inline void *buffer::data() { return data_; }
inline const void *buffer::data() const { return data_; }
inline size_t buffer::size() const { return size_; }

inline buffer make_buffer(void *data, size_t size) { return buffer(data, size); }
inline buffer make_buffer(const void *data, size_t size) { return buffer(data, size); }
inline buffer buffer_cast(void *p) { return buffer(p, 0); }

} // namespace asio
