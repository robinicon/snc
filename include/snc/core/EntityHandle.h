// EntityHandle — safe cross-thread reference to an Entity.
//
// Prevents use-after-free when IO thread destroys a connection while
// a Worker thread is processing a task for that connection's Entity.
//
// Usage:
//   1. Create from shared_ptr<T>:
//      auto handle = EntityHandle::from(entity);
//   2. In Worker task, lock to get temporary strong ref:
//      auto entity = handle.lock();
//      if (!entity) return; // Entity was destroyed
//   3. Entity destructor calls Invalidate() to let all handles become stale
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
