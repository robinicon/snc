#pragma once

#include <functional>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "snc/core/SocketWrap.h"
#include "snc/core/IConnection.h"
#include "snc/core/TcpConnection.h"

namespace snc {
namespace core {

using NewConnCallback = std::function<void(IConnection::Ptr)>;

class Acceptor {
public:
    Acceptor() : fd_(-1) {}
    ~Acceptor() { if (fd_ >= 0) CloseFd(); }

    bool Listen(const char* host, uint16_t port) {
        SocketWrap sock = SocketWrap::CreateTCP();
        if (!sock.Valid()) return false;
        sock.SetReuseAddr();
        sock.SetNonBlocking();

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (host && std::strcmp(host, "0.0.0.0") != 0) {
            inet_pton(AF_INET, host, &addr.sin_addr);
        } else {
            addr.sin_addr.s_addr = INADDR_ANY;
        }

        if (::bind(sock.Fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return false;
        if (::listen(sock.Fd(), 128) < 0) return false;

        fd_ = sock.Release();
        return true;
    }

    void SetNewConnectionCallback(NewConnCallback cb) { callback_ = std::move(cb); }

    // Accept one connection and invoke callback with new TcpConnection.
    // Must be called from the event loop thread.
    void AcceptOnce() {
        if (fd_ < 0 || !callback_) return;
        sockaddr_in clientAddr{};
        socklen_t addrlen = sizeof(clientAddr);
        int clientFd = ::accept(fd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrlen);
        if (clientFd < 0) return;
        SocketWrap clientSock(clientFd);
        clientSock.SetNonBlocking();
        clientSock.SetNoDelay();
        auto conn = std::make_shared<TcpConnection>(std::move(clientSock));
        callback_(conn);
    }

    int Fd() const { return fd_; }

private:
    void CloseFd() {
#ifdef _WIN32
        closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = -1;
    }

    int fd_;
    NewConnCallback callback_;
};

} // namespace core
} // namespace snc
