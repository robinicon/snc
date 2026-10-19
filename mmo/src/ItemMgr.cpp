// SPDX-License-Identifier: MIT
/// @file ItemMgr.cpp
/// @brief 物品管理器实现

#include "snc/mmorpg/legend/ItemMgr.h"

namespace snc {
namespace mmorpg {
namespace legend {

ItemPtr ItemMgr::CreateItem(int32_t item_id, int32_t count, int64_t expire_ts) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto item = std::make_shared<Item>();
    item->item_id  = item_id;
    item->count    = count;
    item->uid      = next_uid_++;
    item->expire_ts = expire_ts;
    items_[item->uid] = item;
    return item;
}

ItemPtr ItemMgr::GetItem(int32_t uid) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = items_.find(uid);
    return (it != items_.end()) ? it->second : nullptr;
}

bool ItemMgr::DeleteItem(int32_t uid) {
    std::lock_guard<std::mutex> lk(mtx_);
    return items_.erase(uid) > 0;
}

int ItemMgr::CleanupExpired(int64_t nowMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    int count = 0;
    for (auto it = items_.begin(); it != items_.end(); ) {
        if (it->second->expire_ts > 0 && it->second->expire_ts < nowMs) {
            it = items_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

size_t ItemMgr::TotalCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return items_.size();
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
