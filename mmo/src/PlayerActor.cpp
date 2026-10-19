// SPDX-License-Identifier: MIT
/// @file PlayerActor.cpp
/// @brief 传奇版增强玩家 Actor 实现

#include "snc/mmorpg/legend/PlayerActor.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace snc {
namespace mmorpg {
namespace legend {

// ── 背包操作 ─────────────────────────────────────────────────────

int PlayerActor::AddItem(int32_t itemId, int32_t count) {
    // 先尝试堆叠已有同类物品
    constexpr int kMaxStack = 99;
    int added = 0;
    for (int i = 0; i < kBagSlots && count > 0; ++i) {
        if (bag_[i].item_id == itemId && bag_[i].count > 0) {
            int32_t can_add = kMaxStack - bag_[i].count;
            if (can_add < 0) can_add = 0;
            can_add = std::min(count, can_add);
            bag_[i].count += can_add;
            count -= can_add;
            added += can_add;
        }
    }
    // 再找空槽
    for (int i = 0; i < kBagSlots && count > 0; ++i) {
        if (!bag_[i].IsValid()) {
            bag_[i].item_id = itemId;
            bag_[i].count   = count;
            added += count;
            count = 0;
        }
    }
    return added;
}

int PlayerActor::RemoveItem(int32_t itemId, int32_t count) {
    int removed = 0;
    for (int i = 0; i < kBagSlots && removed < count; ++i) {
        if (bag_[i].item_id == itemId) {
            int take = std::min(count - removed, bag_[i].count);
            bag_[i].count -= take;
            removed += take;
            if (bag_[i].count <= 0) bag_[i].item_id = 0, bag_[i].count = 0;
        }
    }
    return removed;
}

int PlayerActor::FindItemSlot(int32_t itemId) const {
    for (int i = 0; i < kBagSlots; ++i)
        if (bag_[i].item_id == itemId) return i;
    return -1;
}

bool PlayerActor::HasItem(int32_t itemId, int32_t count) const {
    int total = 0;
    for (int i = 0; i < kBagSlots; ++i)
        if (bag_[i].item_id == itemId) total += bag_[i].count;
    return total >= count;
}

// ── 装备操作 ─────────────────────────────────────────────────────

void PlayerActor::Equip(int8_t slot, int32_t itemId, int32_t dur) {
    if (slot < 0 || slot >= kEquipSlots) return;
    equip_[slot] = {itemId, dur};
    // 重新计算装备属性（简化：直接 sum）
    BaseStats bonus{};
    for (int i = 0; i < kEquipSlots; ++i) {
        if (equip_[i].IsValid()) {
            // 简化：默认每个装备提供 +1 全属性（真实项目从 ItemDef 读取）
            bonus.str  += 1;
            bonus.def  += 1;
            bonus.hpMax += 5;
        }
    }
    equip_stats_ = bonus;
}

void PlayerActor::Unequip(int8_t slot) {
    if (slot < 0 || slot >= kEquipSlots) return;
    equip_[slot] = ItemSlot{};
    // 重新计算装备属性
    BaseStats bonus{};
    for (int i = 0; i < kEquipSlots; ++i) {
        if (equip_[i].IsValid()) {
            bonus.str  += 1;
            bonus.def  += 1;
            bonus.hpMax += 5;
        }
    }
    equip_stats_ = bonus;
}

// ── 经验/升级 ────────────────────────────────────────────────────

int64_t PlayerActor::ExpNeeded(int32_t level) {
    // 传奇经典公式：level^2 × 100
    return static_cast<int64_t>(level) * level * 100;
}

bool PlayerActor::AddExp(int64_t exp) {
    exp_ += exp;
    while (exp_ >= ExpNeeded(level_)) {
        exp_ -= ExpNeeded(level_);
        LevelUp();
    }
    return level_ > 1; // 是否发生了升级
}

void PlayerActor::LevelUp() {
    ++level_;
    // 升级提升基础属性（简化：每级 +5 HP，+1 全属性）
    base_stats_.hpMax += 5;
    base_stats_.str   += 1;
    base_stats_.def   += 1;
    current_hp_ = TotalMaxHp(); // 升级回满血
    SNC_LOG_INFO("legend.player", "Player %d (Lv.%d) leveled up!", EntityId(), level_);
}

// ── PK 操作 ──────────────────────────────────────────────────────

void PlayerActor::OnKillPlayer() {
    ++pk_info_.kill_count;
    pk_info_.red_name_val += 50;
    pk_info_.last_kill_ts  = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (pk_info_.red_name_val > 200)
        pk_info_.status = PKStatus::VERY_RED;
    else if (pk_info_.red_name_val > 100)
        pk_info_.status = PKStatus::RED_NAME;
}

// ── 行会操作 ─────────────────────────────────────────────────────

void PlayerActor::JoinGuild(int32_t guildId, const std::string& name, int32_t rank) {
    guild_info_.guild_id     = guildId;
    guild_info_.guild_name   = name;
    guild_info_.guild_rank   = rank;
}

void PlayerActor::LeaveGuild() {
    guild_info_.guild_id     = 0;
    guild_info_.guild_rank   = 0;
    guild_info_.guild_name.clear();
}

// ── 移动 ─────────────────────────────────────────────────────────

bool PlayerActor::MoveTo(int32_t mapId, int32_t x, int32_t y) {
    if (mapId <= 0 || x < 0 || x >= kMapMaxCoord || y < 0 || y >= kMapMaxCoord) return false;
    pos_.mapId = mapId;
    pos_.x     = x;
    pos_.y     = y;
    return true;
}

// ── 属性计算 ─────────────────────────────────────────────────────

BaseStats PlayerActor::CalcTotalStats() const {
    BaseStats s = base_stats_;
    s.str  += equip_stats_.str;
    s.def  += equip_stats_.def;
    s.hpMax += equip_stats_.hpMax;
    return s;
}

int32_t PlayerActor::TotalMaxHp() const {
    return base_stats_.hpMax + equip_stats_.hpMax;
}

int32_t PlayerActor::TotalMaxMp() const {
    return base_stats_.mpMax + equip_stats_.mpMax;
}

// ── 主循环 ───────────────────────────────────────────────────────

void PlayerActor::Tick(long long elapsedMs) {
    Regenerate(elapsedMs);
}

void PlayerActor::Regenerate(long long elapsedMs) {
    // 每秒恢复少量 HP/MP
    int32_t hpTick = (class_type_ == ClassType::TAOIST) ? 2 : 1;
    int32_t mpTick = (class_type_ == ClassType::MAGE || class_type_ == ClassType::TAOIST) ? 1 : 0;
    int ticks = static_cast<int>(elapsedMs / 1000);
    if (ticks <= 0) return;
    current_hp_ = std::min(TotalMaxHp(), current_hp_ + hpTick * ticks);
    current_mp_ = std::min(TotalMaxMp(), current_mp_ + mpTick * ticks);
}

// ── 调试序列化 ───────────────────────────────────────────────────

std::string PlayerActor::DumpState() const {
    std::ostringstream oss;
    oss << "{\"id\":" << EntityId()
        << ",\"name\":\"" << nickname_ << "\""
        << ",\"cls\":\"" << ClassTypeName(class_type_) << "\""
        << ",\"level\":" << level_
        << ",\"exp\":" << exp_
        << ",\"gold\":" << gold_
        << ",\"hp\":" << current_hp_ << "/" << TotalMaxHp()
        << ",\"mp\":" << current_mp_ << "/" << TotalMaxMp()
        << ",\"pos\":{\"map\":" << pos_.mapId << ",\"x\":" << pos_.x << ",\"y\":" << pos_.y << "}"
        << ",\"pk\":{\"val\":" << pk_info_.red_name_val
        << ",\"status\":" << static_cast<int>(pk_info_.status) << "}"
        << ",\"guild\":{\"id\":" << guild_info_.guild_id
        << ",\"rank\":" << guild_info_.guild_rank
        << ",\"name\":\"" << guild_info_.guild_name << "\"}}"
        << "}";
    return oss.str();
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
