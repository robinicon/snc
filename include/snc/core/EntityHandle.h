// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

// EntityHandle — 跨线程安全引用实体（连接/玩家）的封装。
//
// 解决场景：IO 线程销毁了连接对象，但 Worker 线程正在处理该连接的任务，
//          导致 use-after-free 崩溃。
//
// 原理：内部存储 weak_ptr，worker 线程 lock() 获取临时强引用；
//      若对象已销毁则返回 nullptr，避免非法访问。
//
// 使用方式：
//   1. 从 shared_ptr<T> 创建：auto handle = EntityHandle::from(entity);
//   2. Worker 任务中锁定获取临时强引用：
//        auto entity = handle.lock();
//        if (!entity) return; // 实体已被销毁
//   3. 实体析构时调用 Invalidate()，使所有持有者失效
#pragma once

#include <memory>
#include <type_traits>

namespace snc {
namespace core {

template<typename T>
class EntityHandle {
public:
    using Ptr = std::shared_ptr<T>;

    // Default construct — empty (lock() will always return nullptr)
    EntityHandle() = default;

    // Create from a strong shared_ptr — stores weak reference internally
    static EntityHandle from(const Ptr& obj) {
        if (!obj) return EntityHandle{};
        return EntityHandle{std::weak_ptr<T>(obj)};
    }

    // Lock the handle — returns shared_ptr if object is still alive
    // Returns nullptr if the object has been destroyed
    Ptr lock() const {
        return weak_.lock();
    }

    // Check if the handle is still valid without acquiring a ref
    explicit operator bool() const {
        return weak_.lock() != nullptr;
    }

    bool operator==(const EntityHandle& other) const {
        return weak_.lock() == other.weak_.lock();
    }

    bool operator!=(const EntityHandle& other) const {
        return !(*this == other);
    }

private:
    explicit EntityHandle(std::weak_ptr<T> weak) : weak_(std::move(weak)) {}

    std::weak_ptr<T> weak_;
};

} // namespace core
} // namespace snc
