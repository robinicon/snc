#pragma once

/// @file Item.h
/// @brief 传奇版统一物品模型 �?背包/仓库/地面掉落共用同一套数据结�?

#include <cstdint>
#include <string>
#include <memory>

namespace snc {
namespace mmorpg {
namespace legend {

// ── 统一物品结构 ─────────────────────────────────────────────
struct Item {
    int32_t  item_id  = 0;  // 物品定义 ID（关�?ItemDef�?
    int32_t  count    = 0;  // 数量
    int32_t  uid      = 0;  // 唯一实例 ID（背包内可复用，地面掉落必须唯一�?
    int64_t  expire_ts = 0; // 0=永久（背�?装备），>0=过期时间戳ms（地面掉落）

    bool IsPermanent() const { return expire_ts == 0; }
    bool IsExpired(int64_t nowMs) const {
        return !IsPermanent() && expire_ts < nowMs;
    }
};

using ItemPtr = std::shared_ptr<Item>;

} // namespace legend
} // namespace mmorpg
} // namespace snc
