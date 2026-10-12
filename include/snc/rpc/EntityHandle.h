// EntityHandle — safe cross-thread reference to an Entity.
//
// Prevents use-after-free when IO thread destroys a connection while
// a Worker thread is processing a task for that connection's Entity.
//
// Usage:
//   1. Create from Entity::Ptr (strong ref):
//      auto handle = EntityHandle::from(entity);
//   2. In Worker task, lock to get temporary strong ref:
//      auto entity = handle.lock();
//      if (!entity) return; // Entity was destroyed
//   3. Entity destructor calls Invalidate() to let all handles become stale
#pragma once

#include <memory>
#include <mutex>
#include <utility>

namespace snc {
namespace rpc {

class Entity; // forward declaration

class EntityHandle {
public:
    using Ptr = std::shared_ptr<Entity>;

    // Default construct — empty (lock() will always return nullptr)
    EntityHandle() = default;

    // Create from a strong shared_ptr — stores weak reference
    static EntityHandle from(const Ptr& entity) {
        if (!entity) return EntityHandle{};
        auto weak = entity.share(); // creates weak_ptr from shared_ptr
        return EntityHandle{std::move(weak)};
    }

    // Lock the handle — returns shared_ptr if Entity is still alive
    // Returns nullptr if Entity has been destroyed (Invalidate() called)
    Ptr lock() const {
        return weak_.lock();
    }

    // Check if the handle is still valid without acquiring a ref
    bool isValid() const {
        return weak_.lock() != nullptr;
    }

    bool operator==(const EntityHandle& other) const {
        return weak_.lock() == other.weak_.lock();
    }

    bool operator!=(const EntityHandle& other) const {
        return !(*this == other);
    }

private:
    explicit EntityHandle(std::weak_ptr<Entity> weak) : weak_(std::move(weak)) {}

    std::weak_ptr<Entity> weak_;
};

} // namespace rpc
} // namespace snc
