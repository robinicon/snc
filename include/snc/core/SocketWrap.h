// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

#include "snc/base/Common.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <cstring>
#include "snc/base/Log.h"

namespace snc {
namespace core {

/// SocketWrap - RAII 风格的 TCP 套接字封装。
///
/// 职责：
///   - 管理 socket fd 生命周期，析构时自动关闭
///   - 封装跨平台的非阻塞、Nagle、SO_REUSEADDR 设置
///   - 提供 _send_/_recv_ 接口，内部处理 Windows 宏替换问题
///
/// 注意：Windows 上 send/recv 被 winsock2.h/winnt.h 定义为宏，
///       类方法命名为 _send_/_recv_ 以规避宏冲突，内部调用 (::send)/(::recv)。
class SocketWrap {
public:
    SocketWrap() : fd_(-1) {}
    explicit SocketWrap(int fd) : fd_(fd) {}
    ~SocketWrap() { if (fd_ >= 0) Close(); }

    // 禁止拷贝，允许移动（socket fd 不可拷贝）
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

    /// 创建 TCP 流式套接字（SOCK_STREAM），内部完成 WSADATA 初始化（Windows）。
    static SocketWrap CreateTCP() {
#ifdef _WIN32
        WSADATA wsaData{};
        WSAStartup(0x0202, &wsaData);
#endif
        int fd = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
        if (fd < 0) return SocketWrap(-1);
        return SocketWrap(fd);
    }

    bool Valid() const { return fd_ >= 0; }
    int  Fd()  const { return fd_; }

    /// 关闭套接字并将 fd 置为 -1。可重复调用（幂等）。
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

    /// 设置非阻塞模式（ioctlsocket FIONBIO / fcntl O_NONBLOCK）。
    void SetNonBlocking(bool enable = true) {
#ifdef _WIN32
        unsigned long mode = enable ? 1 : 0;
        ioctlsocket(fd_, FIONBIO, &mode);
#else
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
    }

    /// 禁用 Nagle 算法（TCP_NODELAY），降低小包延迟。
    void SetNoDelay(bool enable = true) {
        int val = enable ? 1 : 0;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&val), sizeof(val));
    }

    /// 允许端口立即复用（SO_REUSEADDR），避免 TIME_WAIT 状态占用端口。
    void SetReuseAddr(bool enable = true) {
        int val = enable ? 1 : 0;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&val), sizeof(val));
    }

    /// 发起 TCP 连接（阻塞调用，须在非阻塞模式下使用或与 epoll/select 配合）。
    bool Connect(const sockaddr_in& addr) {
        return (::connect)(fd_, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    /// 发送数据，返回实际发送字节数（-1 表示错误）。
    /// Windows 上使用 (::send) 防止 send 宏展开。
    ssize_t _send_(const char* buf, size_t len) {
#ifdef _WIN32
        return (::send)(fd_, buf, static_cast<int>(len), 0);
#else
        return ::send(fd_, buf, len, 0);
#endif
    }

    /// 接收数据到 buf，返回实际接收字节数（-1 表示错误或 EOF）。
    /// Windows 上使用 (::recv) 防止 recv 宏展开。
    ssize_t _recv_(char* buf, size_t len) {
#ifdef _WIN32
        return (::recv)(fd_, buf, static_cast<int>(len), 0);
#else
        return ::recv(fd_, buf, len, 0);
#endif
    }

    /// 释放底层 fd 所有权（调用后 fd_ 置为 -1，不调用 close）。
    /// 用于将 fd 移交至 IOCP/kqueue/epoll 等事件循环注册。
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
