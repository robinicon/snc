#pragma once

#include <memory>
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcClient.h"

namespace snc {
namespace rpc {

class RpcServiceNode {
public:
    using Ptr = std::shared_ptr<RpcServiceNode>;

    RpcServiceNode() : client_(nullptr) {}
    ~RpcServiceNode() {}

    void Server(RpcServer::Ptr server) { server_ = std::move(server); }
    RpcServer::Ptr Server() const { return server_; }

    void SetClient(RpcClient::Ptr client) { client_ = std::move(client); }
    RpcClient::Ptr Client() const { return client_; }

private:
    RpcServer::Ptr server_;
    RpcClient::Ptr client_;
};

} // namespace rpc
} // namespace snc
