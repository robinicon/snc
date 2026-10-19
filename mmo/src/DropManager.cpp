// SPDX-License-Identifier: MIT
/// @file DropManager.cpp

#include "snc/mmorpg/legend/DropManager.h"
#include <algorithm>
#include <random>
#include <numeric>

namespace snc {
namespace mmorpg {
namespace legend {

void DropManager::RegisterTable(const DropTable& table) {
    std::lock_guard<std::mutex> lk(mtx_);
    tables_[table.table_id] = table;
}

const DropTable* DropManager::GetTable(int32_t tableId) const {
    auto it = tables_.find(tableId);
    return (it != tables_.end()) ? &it->second : nullptr;
}

std::vector<std::pair<int32_t, int32_t>> DropManager::RollDrops(int32_t tableId, int32_t count) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = tables_.find(tableId);
    if (it == tables_.end() || it->second.entries.empty()) return {};

    const auto& table = it->second;
    // 计算总权重
    int64_t totalWeight = 0;
    for (auto& e : table.entries) totalWeight += e.rate;
    if (totalWeight == 0) return {};

    std::vector<std::pair<int32_t, int32_t>> result;
    std::mt19937 rng(static_cast<unsigned>(tableId));

    for (int i = 0; i < count; ++i) {
        std::uniform_int_distribution<int64_t> dist(1, totalWeight);
        int64_t roll = dist(rng);
        int64_t cum = 0;
        for (auto& e : table.entries) {
            cum += e.rate;
            if (roll <= cum) {
                if (e.item_id == 0) continue; // 空气（未掉落）
                int32_t qty = e.min_count;
                if (e.max_count > e.min_count) {
                    std::uniform_int_distribution<int32_t> qtyDist(e.min_count, e.max_count);
                    qty = qtyDist(rng);
                }
                result.emplace_back(e.item_id, qty);
                break;
            }
        }
    }
    return result;
}

DropManager::DropPtr DropManager::CreateDrop(int32_t mapId, int32_t x, int32_t y,
                                              int32_t itemId, int32_t count) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto drop = std::make_shared<GroundDrop>();
    drop->drop_id = ++next_drop_id_;
    drop->item_id = itemId;
    drop->count   = count;
    drop->pos.mapId = mapId;
    drop->pos.x     = x;
    drop->pos.y     = y;
    drop->map_id    = mapId;
    drop->created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    drops_[drop->drop_id] = drop;
    map_drop_ids_[mapId].push_back(drop->drop_id);
    return drop;
}

void DropManager::CreateDrops(int32_t mapId, int32_t x, int32_t y,
                               const std::vector<std::pair<int32_t, int32_t>>& items) {
    for (auto& [itemId, count] : items)
        CreateDrop(mapId, x, y, itemId, count);
}

void DropManager::RemoveDrop(int32_t dropId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = drops_.find(dropId);
    if (it == drops_.end()) return;
    int32_t mapId = it->second->map_id;
    auto& ids = map_drop_ids_[mapId];
    ids.erase(std::remove(ids.begin(), ids.end(), dropId), ids.end());
    drops_.erase(it);
}

int DropManager::CleanupExpired(int64_t nowMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    int removed = 0;
    for (auto it = drops_.begin(); it != drops_.end();) {
        if (it->second->IsExpired(nowMs)) {
            int32_t mapId = it->second->map_id;
            auto& ids = map_drop_ids_[mapId];
            ids.erase(std::remove(ids.begin(), ids.end(), it->first), ids.end());
            it = drops_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

int DropManager::PickUp(PlayerActor::Ptr player, int32_t pickupRange) {
    int picked = 0;
    auto drops = GetDropsInRange(player->Pos().mapId, player->Pos().x, player->Pos().y, pickupRange);
    for (auto& drop : drops) {
        if (PickUpDrop(player, drop->drop_id)) ++picked;
    }
    return picked;
}

bool DropManager::PickUpDrop(PlayerActor::Ptr player, int32_t dropId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = drops_.find(dropId);
    if (it == drops_.end()) return false;
    auto drop = it->second;
    // 检查距离
    int dx = drop->pos.x - player->Pos().x;
    int dy = drop->pos.y - player->Pos().y;
    if (std::abs(dx) > 3 || std::abs(dy) > 3) return false;
    // 拾取
    player->AddItem(drop->item_id, drop->count);
    int32_t mapId = drop->map_id;
    auto& ids = map_drop_ids_[mapId];
    ids.erase(std::remove(ids.begin(), ids.end(), dropId), ids.end());
    drops_.erase(it);
    return true;
}

DropManager::DropPtr DropManager::GetDrop(int32_t dropId) const {
    auto it = drops_.find(dropId);
    return (it != drops_.end()) ? it->second : nullptr;
}

std::vector<DropManager::DropPtr> DropManager::GetDropsInMap(int32_t mapId) const {
    std::vector<DropPtr> result;
    auto it = map_drop_ids_.find(mapId);
    if (it == map_drop_ids_.end()) return result;
    for (int32_t id : it->second) {
        auto dit = drops_.find(id);
        if (dit != drops_.end()) result.push_back(dit->second);
    }
    return result;
}

std::vector<DropManager::DropPtr> DropManager::GetDropsInRange(int32_t mapId, int32_t x, int32_t y, int32_t range) const {
    std::vector<DropPtr> result;
    auto it = map_drop_ids_.find(mapId);
    if (it == map_drop_ids_.end()) return result;
    for (int32_t id : it->second) {
        auto dit = drops_.find(id);
        if (dit == drops_.end()) continue;
        auto d = dit->second;
        int dx = d->pos.x - x, dy = d->pos.y - y;
        if (std::abs(dx) <= range && std::abs(dy) <= range)
            result.push_back(d);
    }
    return result;
}

size_t DropManager::DropCount(int32_t mapId) const {
    auto it = map_drop_ids_.find(mapId);
    return (it != map_drop_ids_.end()) ? it->second.size() : 0;
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
