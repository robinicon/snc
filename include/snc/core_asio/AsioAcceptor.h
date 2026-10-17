#pragma once

#include <functional>
#include <string>
#include <memory>
#include <asio.hpp>
#include "snc/core/IConnection.h"

namespace snc {
namespace core {

class AsioTcpConnection;

using NewConnCallback = std::function<void(IConnection::Ptr)>;

class AsioAcceptor {
public:
    explicit AsioAcceptor(asio::io_context& ioCtx);
    ~AsioAcceptor();

    AsioAcceptor(const AsioAcceptor&) = delete;
    AsioAcceptor& operator=(const AsioAcceptor&) = delete;

    bool Listen(const char* host, uint16_t port);
    bool Listen(uint16_t port, const std::string& host = "0.0.0.0");

    void SetNewConnectionCallback(NewConnCallback cb) {
        callback_ = std::move(cb);
    }

    void StartAccept(asio::io_context& ioCtx);
    void Shutdown();
    bool IsListening() const { return acceptor_.is_open(); }

private:
    void DoAccept();

    asio::io_context*       ioCtx_     = nullptr;
    asio::ip::tcp::acceptor acceptor_;
    NewConnCallback         callback_;
};

} // namespace core
} // namespace snc