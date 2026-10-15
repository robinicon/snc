/// @file NPCManager.cpp

#include "snc/mmorpg/legend/NPCManager.h"
#include <algorithm>
#include <sstream>

namespace snc {
namespace mmorpg {
namespace legend {

void NPCManager::RegisterNPC(const NPCDef& def) {
    std::lock_guard<std::mutex> lk(mtx_);
    npc_defs_[def.npc_id] = def;
}

bool NPCManager::PlaceNPC(int32_t mapId, int32_t npcId, int32_t x, int32_t y) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = npc_defs_.find(npcId);
    if (it == npc_defs_.end()) return false;
    auto inst = std::make_shared<NPCInstance>();
    inst->def = it->second;
    inst->def.x = x;
    inst->def.y = y;
    map_npcs_[mapId][npcId] = inst;
    return true;
}

void NPCManager::RemoveNPC(int32_t mapId, int32_t npcId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto mit = map_npcs_.find(mapId);
    if (mit == map_npcs_.end()) return;
    mit->second.erase(npcId);
    if (mit->second.empty()) map_npcs_.erase(mit);
}

std::vector<NPCManager::NPCPtr> NPCManager::GetNearbyNPCs(int32_t mapId, int32_t x, int32_t y, int32_t range) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<NPCManager::NPCPtr> result;
    auto mit = map_npcs_.find(mapId);
    if (mit == map_npcs_.end()) return result;
    for (auto& [nid, inst] : mit->second) {
        int dx = inst->def.x - x, dy = inst->def.y - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx + dy <= range) result.push_back(inst);
    }
    return result;
}

std::string NPCManager::Talk(PlayerActor::Ptr player, int32_t npcId) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [mid, npcs] : map_npcs_) {
        auto it = npcs.find(npcId);
        if (it != npcs.end()) {
            auto& def = it->second->def;
            std::ostringstream oss;
            oss << "{\"npc_id\":" << npcId << ",\"name\":\"" << def.name
                << "\",\"dialog\":\"" << def.dialog_text << "\"}";
            return oss.str();
        }
    }
    return "{\"error\":\"npc_not_found\"}";
}

std::string NPCManager::Buy(PlayerActor::Ptr player, int32_t npcId, int32_t itemId, int32_t count) {
    std::lock_guard<std::mutex> lk(mtx_);
    int32_t total_cost = 0;
    for (auto& [mid, npcs] : map_npcs_) {
        auto it = npcs.find(npcId);
        if (it == npcs.end()) continue;
        auto& items = it->second->def.shop_items;
        for (auto& entry : items) {
            if (entry.item_id == itemId) {
                total_cost = entry.price * count;
                break;
            }
        }
        if (total_cost > 0) break;
    }
    if (total_cost == 0) return "{\"error\":\"item_not_in_shop\"}";
    if (player->Gold() < total_cost) return "{\"error\":\"not_enough_gold\"}";
    player->SpendGold(total_cost);
    player->AddItem(itemId, count);
    if (shop_cb_) shop_cb_(player, npcId, itemId, count);
    return "{\"status\":\"ok\",\"cost\":" + std::to_string(total_cost) + "}";
}

std::string NPCManager::Sell(PlayerActor::Ptr player, int32_t npcId, int32_t itemId, int32_t count) {
    (void)npcId;
    int received = player->RemoveItem(itemId, count);
    if (received == 0) return "{\"error\":\"item_not_found\"}";
    // 卖出价为购买价的一半（简化：固定 50%）
    int64_t gold = static_cast<int64_t>(received) * 10; // 简化：每个物品 10 金币
    player->AddGold(gold);
    return "{\"status\":\"ok\",\"gold\":" + std::to_string(gold) + "}";
}

std::string NPCManager::Teleport(PlayerActor::Ptr player, int32_t npcId, int32_t destIndex) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [mid, npcs] : map_npcs_) {
        auto it = npcs.find(npcId);
        if (it == npcs.end()) continue;
        auto& tps = it->second->def.teleports;
        if (destIndex < 0 || destIndex >= (int)tps.size()) return "{\"error\":\"invalid_dest\"}";
        auto& dest = tps[destIndex];
        if (player->Gold() < dest.cost_gold) return "{\"error\":\"not_enough_gold\"}";
        player->SpendGold(dest.cost_gold);
        player->MoveTo(dest.target_map_id, dest.tx, dest.ty);
        return "{\"status\":\"ok\",\"map\":" + std::to_string(dest.target_map_id)
             + ",\"x\":" + std::to_string(dest.tx) + ",\"y\":" + std::to_string(dest.ty) + "}";
    }
    return "{\"error\":\"npc_not_found\"}";
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
