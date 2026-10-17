// asio/buffer.hpp
#pragma once
#include <cstddef>
#include <cstring>
#include <vector>

namespace asio {

class mutable_buffer {
public:
    mutable_buffer(void* data, std::size_t n) : data_(data), size_(n) {}
    void*          data()    const noexcept { return data_; }
    std::size_t    size()    const noexcept { return size_; }
private:
    void*          data_;
    std::size_t    size_;
};

inline mutable_buffer buffer(void* data, std::size_t n) {
    return mutable_buffer(data, n);
}
inline mutable_buffer buffer(void* data, std::size_t n, std::size_t max_n) {
    (void)max_n;
    return mutable_buffer(data, n);
}
inline mutable_buffer buffer(const void* data, std::size_t n) {
    return mutable_buffer(const_cast<void*>(data), n);
}
inline mutable_buffer buffer(std::vector<char>& v) {
    return mutable_buffer(v.data(), v.size());
}
template<std::size_t N>
inline mutable_buffer buffer(char (&arr)[N]) {
    return mutable_buffer(arr, N);
}

// Helper: create a single buffer from a char* + size
inline mutable_buffer make_buffer(char* p, std::size_t n) {
    return mutable_buffer(p, n);
}

} // namespace asio
