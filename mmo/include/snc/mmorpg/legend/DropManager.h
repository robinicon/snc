#pragma once

/// @file DropManager.h
/// @brief 掉落/拾取管理系统
///
/// 掉落表格式（JSON 示例）：
///   {
///     "table_id": 1,
///     "entries": [
///       {"item_id": 1001, "rate": 5000, "min_count": 1, "max_count": 1},
///       {"item_id": 1002, "rate": 1000, "min_count": 1, "max_count": 3},
///       {"item_id": 0,   "rate": 90000}  // 空气（默认兜底）
///     ]
///   }
///   rate 范围 0~100000（万分比），总和应为 100000
///
/// 拾取规则�?
///   - 物品在地图上存在 30 分钟后自动消�?
///   - 仅同一 Game 服内可拾取，不跨�?
///   - 拾取优先级：靠近者优先；同距离时后出现者优先（先到先得队列�?

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include <chrono>
#include <functional>

#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── 掉落表条�?───────────────────────────────────────────────────
struct DropEntry {
    int32_t  item_id    = 0;
    int32_t  rate       = 0; // 万分比概�?
    int32_t  min_count  = 1;
    int32_t  max_count  = 1;
};

// ── 掉落�?───────────────────────────────────────────────────────
struct DropTable {
    int32_t table_id = 0;
    std::vector<DropEntry> entries;
};

// ── 地面掉落�?───────────────────────────────────────────────────
struct GroundDrop {
    int32_t  drop_id   = 0;     // 全局唯一 ID
    int32_t  item_id   = 0;
    int32_t  count     = 1;
    PlayerPos pos;              // 掉落位置
    int32_t  map_id      = 0;
    int64_t  created_at  = 0;   // 创建时间戳（毫秒�?
    static constexpr int64_t kLifetimeMs = 30 * 60 * 1000; // 30 分钟
    bool IsExpired(int64_t nowMs) const {
        return (nowMs - created_at) > kLifetimeMs;
    }
};

// ══════════════════════════════════════════════════════════════════
// DropManager
// ══════════════════════════════════════════════════════════════════
class DropManager {
public:
    using Ptr    = std::shared_ptr<DropManager>;
    using DropPtr = std::shared_ptr<GroundDrop>;

    DropManager() : next_drop_id_(0) {}
    ~DropManager() = default;

    // ── 掉落�?──────────────────────────────────────────────────
    void RegisterTable(const DropTable& table);
    const DropTable* GetTable(int32_t tableId) const;

    // ── 掉落生成 ────────────────────────────────────────────────
    /// 根据掉落表生成掉落列表（从怪物死亡触发�?
    std::vector<std::pair<int32_t, int32_t>> RollDrops(int32_t tableId, int32_t count = 1);

    // ── 地面掉落管理 ────────────────────────────────────────────
    /// 在地图指定位置生成掉落物
    DropPtr CreateDrop(int32_t mapId, int32_t x, int32_t y,
                        int32_t itemId, int32_t count);
    /// 批量生成（从 RollDrops 结果�?
    void CreateDrops(int32_t mapId, int32_t x, int32_t y,
                      const std::vector<std::pair<int32_t, int32_t>>& items);
    /// 移除并清理过期掉�?
    void RemoveDrop(int32_t dropId);
    /// 清理所有过期掉�?
    int CleanupExpired(int64_t nowMs);

    // ── 拾取 ────────────────────────────────────────────────────
    /// 尝试拾取范围内的所有掉落物
    /// 返回实际拾取的物品数量（成功次数�?
    int PickUp(PlayerActor::Ptr player, int32_t pickupRange = 2);
    /// 拾取单个掉落�?
    bool PickUpDrop(PlayerActor::Ptr player, int32_t dropId);

    // ── 查询 ────────────────────────────────────────────────────
    DropPtr GetDrop(int32_t dropId) const;
    std::vector<DropPtr> GetDropsInMap(int32_t mapId) const;
    std::vector<DropPtr> GetDropsInRange(int32_t mapId, int32_t x, int32_t y, int32_t range) const;
    size_t DropCount(int32_t mapId) const;

private:
    int32_t            next_drop_id_;
    std::mutex         mtx_;
    std::unordered_map<int32_t, DropTable> tables_;
    std::unordered_map<int32_t, DropPtr>   drops_;     // dropId -> DropPtr
    std::unordered_map<int32_t, std::vector<int32_t>> map_drop_ids_; // mapId -> [dropIds]
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
