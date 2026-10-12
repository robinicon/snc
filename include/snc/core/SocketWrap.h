#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int ssize_t;
#else  // _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include <cstring>
#include "snc/base/Common.h"
#include "snc/base/Log.h"

namespace snc {
namespace core {

class SocketWrap {
public:
    SocketWrap() : fd_(-1) {}
    explicit SocketWrap(int fd) : fd_(fd) {}
    ~SocketWrap() { if (fd_ >= 0) Close(); }

    SocketWrap(const SocketWrap&) = delete;
    SocketWrap& operator=(const SocketWrap&) = delete;
    SocketWrap(SocketWrap&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    SocketWrap& operator=(SocketWrap&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) Close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    static SocketWrap CreateTCP() {
#ifdef _WIN32
        WSADATA wsaData{};
        WSAStartup(0x0202, &wsaData);
#endif
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return SocketWrap(-1);
        return SocketWrap(fd);
    }

    bool Valid() const { return fd_ >= 0; }
    int  Fd()  const { return fd_; }

    void Close() {
        if (fd_ >= 0) {
#ifdef _WIN32
            closesocket(fd_);
#else
            ::close(fd_);
#endif
            fd_ = -1;
        }
    }

    void SetNonBlocking(bool enable = true) {
#ifdef _WIN32
        unsigned long mode = enable ? 1 : 0;
        ioctlsocket(fd_, FIONBIO, &mode);
#else
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
    }

    void SetNoDelay(bool enable = true) {
        int val = enable ? 1 : 0;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&val), sizeof(val));
    }

    void SetReuseAddr(bool enable = true) {
        int val = enable ? 1 : 0;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&val), sizeof(val));
    }

    bool Connect(const sockaddr_in& addr) {
        return ::connect(fd_, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    ssize_t Send(const char* buf, size_t len) {
#ifdef _WIN32
        return ::send(fd_, buf, static_cast<int>(len), 0);
#else
        return ::send(fd_, buf, len, 0);
#endif
    }

    ssize_t Recv(char* buf, size_t len) {
#ifdef _WIN32
        return ::recv(fd_, buf, static_cast<int>(len), 0);
#else
        return ::recv(fd_, buf, len, 0);
#endif
    }

    int Release() {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

} // namespace core
} // namespace snc
