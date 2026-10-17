// asio/ip/basic_endpoint.hpp
#pragma once
#include "address.hpp"
#include <cstring>
#include <cstdint>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
#endif

namespace asio {
namespace ip {

// v4_type forward declaration — defined later in tcp.hpp after endpoint is complete
struct v4_type;

template<typename Protocol>
class basic_endpoint {
public:
    using protocol_type = Protocol;

    basic_endpoint() {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
    }

    basic_endpoint(const address& addr, uint16_t port) {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port   = htons(port);
        addr_.sin_addr.s_addr = addr.to_v4();
    }

    basic_endpoint(const Protocol&, uint16_t port) {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port   = htons(port);
    }

    // v4_type constructor — must be declared after v4_type is defined in tcp.hpp
    basic_endpoint(v4_type, uint16_t port);

    void*          data()       { return &addr_; }
    const void*    data() const { return &addr_; }
    std::size_t    size() const { return sizeof(sockaddr_in); }
    uint16_t       port() const { return ntohs(addr_.sin_port); }
    void           set_port(uint16_t p) { addr_.sin_port = htons(p); }
    address        address() const { return address(addr_.sin_addr.s_addr); }
    protocol_type  protocol() const { return protocol_type{}; }

private:
    sockaddr_in addr_;
};

} // namespace ip
} // namespace asio
