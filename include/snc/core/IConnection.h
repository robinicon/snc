#pragma once

#include <memory>
#include <functional>
#include "snc/core/Msg.h"
#include "snc/core/EntityHandle.h"
#include "snc/core/DisconnectReason.h"

namespace snc {
namespace core {

class IConnection : public std::enable_shared_from_this<IConnection> {
public:
    using Ptr = std::shared_ptr<IConnection>;
    using MessageCallback = std::function<void(EntityHandle<IConnection> conn, Msg::Ptr msg)>;
    // CloseCallback carries a DisconnectReason so callers can distinguish
    // NORMAL close, TIMEOUT, CONN_ERROR, etc.
    using CloseCallback = std::function<void(Ptr conn, DisconnectReason reason)>;
    // EnqueueFn routes parsed messages into the MessageQueue / WorkerPool pipeline.
    using EnqueueFn = std::function<bool(Msg::Ptr)>;

    virtual ~IConnection() {}
    virtual int Fd() const = 0;
    virtual bool Send(Msg::Ptr msg) = 0;
    virtual bool Close() = 0;
    virtual void SetMessageCallback(MessageCallback cb) = 0;
    virtual void SetCloseCallback(CloseCallback cb) = 0;
    virtual void SetEnqueueFn(EnqueueFn fn) = 0;
    virtual void DispatchToQueue(Msg::Ptr msg) = 0;
};

} // namespace core
} // namespace snc
