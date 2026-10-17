// asio/read.hpp — async_read for exactly N bytes
#pragma once
#include "buffer.hpp"
#include "ip/tcp.hpp"
#include "async_result.hpp"
#include <cstddef>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <unistd.h>
#endif

namespace asio {

// Read exactly n bytes from a socket into buf
template<typename MutableBufferSequence, typename ReadHandler>
inline void async_read(
    ip::basic_stream_socket& socket,
    MutableBufferSequence buf,
    ReadHandler handler)
{
    // Use socket's built-in async_read
    socket.async_read(buf, std::move(handler));
}

// sync_read_n: reads exactly n bytes from a socket fd
inline std::size_t sync_read_n(int fd, void* buf, std::size_t n) {
    char* p = reinterpret_cast<char*>(buf);
    std::size_t total = 0;
    while (total < n) {
#ifdef _WIN32
        int r = static_cast<int>(::recv(fd, p + total, static_cast<unsigned long>(n - total), 0));
#else
        ssize_t r = ::read(fd, p + total, n - total);
#endif
        if (r <= 0) break;
        total += static_cast<std::size_t>(r);
    }
    return total;
}

} // namespace asio
