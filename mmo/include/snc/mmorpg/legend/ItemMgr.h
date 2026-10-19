// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file ItemMgr.h
/// @brief 传奇版物品管理器 �?统一管理背包、仓库、地面掉落物�?

#include "snc/mmorpg/legend/Item.h"
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace snc {
namespace mmorpg {
namespace legend {

class ItemMgr {
public:
    using Ptr = std::shared_ptr<ItemMgr>;

    ItemMgr() = default;
    ~ItemMgr() = default;

    // 创建物品（分�?uid�?
    ItemPtr CreateItem(int32_t item_id, int32_t count, int64_t expire_ts = 0);

    // �?uid 查询/删除
    ItemPtr    GetItem(int32_t uid) const;
    bool       DeleteItem(int32_t uid);

    // 地面掉落清理（返回清理数量）
    int CleanupExpired(int64_t nowMs);

    // 统计
    size_t TotalCount() const;

private:
    mutable std::mutex          mtx_;
    std::unordered_map<int32_t, ItemPtr> items_; // uid �?Item
    int32_t next_uid_ = 1;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
