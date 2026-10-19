// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <mutex>
#include "snc/core/IConnection.h"
#include "snc/core/EntityHandle.h"

namespace snc {
namespace rpc {

using core::IConnection;
using core::EntityHandle;

class Entity : public std::enable_shared_from_this<Entity> {
public:
    using Ptr = std::shared_ptr<Entity>;

    explicit Entity(IConnection::Ptr conn) : conn_(std::move(conn)) {}
    virtual ~Entity() { Invalidate(); }

    IConnection::Ptr GetConn() const { return conn_; }
    void SetConn(IConnection::Ptr conn) { conn_ = std::move(conn); }

    // Returns an EntityHandle for safe cross-thread use.
    // Worker threads should lock() the handle before accessing this Entity.
    EntityHandle<Entity> GetHandle() {
        auto self = shared_from_this(); // shared_ptr<Entity>
        return EntityHandle<Entity>::from(self);
    }

    // Called when the connection is closed. Clears the raw conn_ so no
    // stale pointer is used after Invalidate().
    // After Invalidate(), GetConn() returns nullptr.
    void Invalidate() {
        std::lock_guard<std::mutex> lock(invalidate_mtx_);
        if (invalidated_) return;
        invalidated_ = true;
        conn_.reset();
    }

    bool IsInvalidated() const {
        std::lock_guard<std::mutex> lock(invalidate_mtx_);
        return invalidated_;
    }

private:
    IConnection::Ptr conn_;
    mutable std::mutex invalidate_mtx_;
    bool invalidated_ = false;
};

} // namespace rpc
} // namespace snc
