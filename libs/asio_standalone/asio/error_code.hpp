// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
//
// This is an independent implementation of a subset of Boost.Asio,
// not derived from or linked to the Boost Software Library.

#pragma once

#include <cerrno>
#include <string>

namespace asio {

class error_code {
public:
    error_code() : val_(0), cat_(&system_category()) {}
    explicit error_code(int val, const std::error_category &cat) : val_(val), cat_(&cat) {}
    error_code(int val, const std::error_category &cat, const std::string &msg)
        : val_(val), cat_(&cat), msg_(msg) {}
    bool operator==(const error_code &other) const { return val_ == other.val_ && cat_ == other.cat_; }
    bool operator!=(const error_code &other) const { return !(*this == other); }
    int value() const { return val_; }
    const std::error_category &category() const { return *cat_; }
    std::string message() const;
    explicit operator bool() const { return val_ != 0; }
    static const error_code &system_category();
    static const error_code &posix_category();
private:
    int val_;
    const std::error_category *cat_;
    std::string msg_;
};

inline const error_code &error_code::system_category() {
    static const error_code cat(0, std::system_category());
    return cat;
}
inline const error_code &error_code::posix_category() {
    static const error_code cat(0, std::generic_category());
    return cat;
}

} // namespace asio
