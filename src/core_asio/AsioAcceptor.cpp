// AsioAcceptor — Boost.Asio-based non-polling TCP acceptor implementation.
//
// Replaces the synchronous Acceptor (include/snc/core/Acceptor.h) with a true
// async_accept chain that re-arms itself after every successful accept,
// requiring zero sleep_for busy-wait loops.

#include "snc/core_asio/AsioAcceptor.h"
#include "snc/core_asio/AsioTcpConnection.h"
#include "snc/base/Log.h"

#include <cstring>

namespace snc {
namespace core {

AsioAcceptor::AsioAcceptor(asio::io_context& ioCtx)
    : ioCtx_(&ioCtx)
    , acceptor_(*ioCtx_)
{
}

AsioAcceptor::~AsioAcceptor()
{
    Shutdown();
}

bool AsioAcceptor::Listen(const char* host, uint16_t port)
{
    try {
        asio::ip::tcp::endpoint endpoint;
        if (host && std::strcmp(host, "0.0.0.0") != 0) {
            endpoint = asio::ip::tcp::endpoint(asio::ip::make_address(host), port);
        } else {
            endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
        }
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::ip::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen(128);
        return true;
    } catch (const std::exception& e) {
        SNC_LOG_ERROR("core_asio", "AsioAcceptor::Listen failed: %s", e.what());
        return false;
    }
}

bool AsioAcceptor::Listen(uint16_t port, const std::string& host)
{
    return Listen(host.c_str(), port);
}

void AsioAcceptor::StartAccept(asio::io_context& ioCtx)
{
    ioCtx_ = &ioCtx;
    DoAccept();
}

void AsioAcceptor::Shutdown()
{
    if (acceptor_.is_open()) {
        asio::error_code ec;
        acceptor_.close(ec);
    }
}

void AsioAcceptor::DoAccept()
{
    if (!acceptor_.is_open() || !callback_ || !ioCtx_) return;

    auto sock = std::make_shared<asio::ip::tcp::socket>(ioCtx_->get_executor());

    acceptor_.async_accept(*sock, [this, sock](const asio::error_code& ec) {
        if (ec) {
            // acceptor 已关闭（Shutdown）或 io_context 已 stop，正常退出
            return;
        }

        // 通过工厂方法创建 AsioTcpConnection，socket 所有权转入连接对象
        auto conn = AsioTcpConnection::CreateFromAcceptedSocket(*ioCtx_, std::move(*sock));

        if (callback_) callback_(conn);

        // 继续 arm 下一个 async_accept（零轮询）
        DoAccept();
    });
}

} // namespace core
} // namespace snc