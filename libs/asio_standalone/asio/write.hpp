// asio/write.hpp — async_write for exactly N bytes
#pragma once
#include "buffer.hpp"
#include "ip/tcp.hpp"
#include <cstddef>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <unistd.h>
#endif

namespace asio {

// Write all data from buf to a socket
template<typename ConstBufferSequence, typename WriteHandler>
inline void async_write(
    ip::basic_stream_socket& socket,
    ConstBufferSequence buf,
    WriteHandler handler)
{
    socket.async_write(buf, std::move(handler));
}

inline std::size_t sync_write_n(int fd, const void* buf, std::size_t n) {
    const char* p = reinterpret_cast<const char*>(buf);
    std::size_t total = 0;
    while (total < n) {
#ifdef _WIN32
        int r = static_cast<int>(::send(fd, p + total, static_cast<unsigned long>(n - total), 0));
#else
        ssize_t r = ::write(fd, p + total, n - total);
#endif
        if (r <= 0) break;
        total += static_cast<std::size_t>(r);
    }
    return total;
}

} // namespace asio
