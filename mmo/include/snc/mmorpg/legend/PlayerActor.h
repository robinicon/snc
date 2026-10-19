// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file PlayerActor.h
/// @brief 传奇版增强玩�?Actor �?职业/背包/装备/技�?PK/行会系统
///
/// 继承 Entity，统一实体基类�?
/// 注：过渡期保�?map_mgr_ 兼容接口，新代码请使�?SceneMgr�?

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>

#include "snc/discovery/ServiceRegistry.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcServer.h"
#include "snc/rpc/RpcSession.h"
#include "snc/core/IConnection.h"
#include "snc/core/Msg.h"
#include "snc/core/DisconnectReason.h"
#include "snc/auth/AuthService.h"
#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/SceneMgr.h"
#include "snc/mmorpg/legend/Item.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {
namespace legend {

class MapManager; // 前向声明（过渡兼容）
class ItemMgr;    // 前向声明

// ── 职业 ─────────────────────────────────────────────────────────
enum class ClassType : int8_t {
    WARRIOR = 0,   // 战士
    MAGE    = 1,   // 法师
    TAOIST  = 2,   // 道士
};

constexpr const char* ClassTypeName(ClassType cls) {
    switch (cls) {
        case ClassType::WARRIOR: return "WARRIOR";
        case ClassType::MAGE:    return "MAGE";
        case ClassType::TAOIST:  return "TAOIST";
        default:                 return "UNKNOWN";
    }
}

// 传奇经典初始属性（战士/法师/道士�?
struct BaseStats {
    int32_t str   = 10;  // 力量 �?影响物理攻击
    int32_t mag   = 10;  // 魔法 �?影响法术攻击
    int32_t def   = 10;  // 防御
    int32_t hpMax = 100; // 最大生命�?
    int32_t mpMax = 0;   // 最大魔法值（道士>0，战�?0�?
    int32_t speed = 0;   // 移动速度
};

inline BaseStats DefaultBaseStats(ClassType cls) {
    BaseStats s{};
    switch (cls) {
        case ClassType::WARRIOR:
            s.str  = 15; s.mag = 2; s.def = 12;
            s.hpMax = 150; s.mpMax = 0;  s.speed = 4; break;
        case ClassType::MAGE:
            s.str  = 5;  s.mag = 18; s.def = 6;
            s.hpMax = 80;  s.mpMax = 100; s.speed = 3; break;
        case ClassType::TAOIST:
            s.str  = 8;  s.mag = 12; s.def = 8;
            s.hpMax = 110; s.mpMax = 60;  s.speed = 3; break;
    }
    return s;
}

// ── 物品槽（背包/装备用）───────────────────────────────────────────
enum class ItemType : int16_t {
    NONE      = 0,
    WEAPON    = 1,
    ARMOR     = 2,
    HELM      = 3,
    BOOT      = 4,
    RING      = 5,
    POTION    = 6,   // 红药/蓝药
    MATERIAL  = 7,
    QUEST     = 8,
};

struct ItemDef {
    int32_t  id        = 0;
    std::string name;
    ItemType type      = ItemType::NONE;
    int32_t  attack    = 0; // 武器攻击
    int32_t  defense   = 0; // 防具防御
    int32_t  magicAtk  = 0; // 法术攻击加成
    int32_t  maxHp     = 0; // HP加成
    int32_t  maxMp     = 0; // MP加成
    int32_t  price     = 0; // 商店售价（金币）
    int32_t  stackMax  = 99; // 堆叠上限�?=不堆叠）
};

struct ItemSlot {
    int32_t  item_id = 0;
    int32_t  count   = 0; // 数量（可堆叠物品�?
    int32_t  dur     = 0; // 耐久度（装备类）
    bool IsValid() const { return item_id > 0 && count > 0; }
};

static constexpr int kBagSlots  = 30;
static constexpr int kEquipSlots = 10;

enum EquipmentSlot : int8_t {
    SLOT_WEAPON   = 0,  // 武器
    SLOT_HELM     = 1,  // 头盔
    SLOT_NECKLACE = 2,  // 项链
    SLOT_ARMOR    = 3,  // 盔甲
    SLOT_SHIELD   = 4,  // 盾牌
    SLOT_GLOVES   = 5,  // 手套
    SLOT_BOOTS    = 6,  // 鞋子
    SLOT_RING_L   = 7,  // 左手戒指
    SLOT_RING_R   = 8,  // 右手戒指
    SLOT_BRACELET = 9,  // 手镯
};

// ── PK 状�?──────────────────────────────────────────────────────
enum class PKStatus : int8_t {
    NORMAL   = 0,
    RED_NAME = 1,    // 红名 (>100 红名�?
    VERY_RED = 2,    // 血红名 (>200 红名�?
};

struct PKInfo {
    int32_t  kill_count   = 0;
    int32_t  red_name_val = 0;
    int64_t  last_kill_ts = 0;
    PKStatus status       = PKStatus::NORMAL;
};

// ── 行会信息（本地缓存，仅存 ID/名称/职位，完整数据在 GuildManager）──
struct PlayerGuildCache {
    int32_t guild_id     = 0;
    int32_t guild_rank   = 0;   // 1=会长 2=副会 3=成员
    std::string guild_name;
};

// ══════════════════════════════════════════════════════════════════
// PlayerActor �?继承 Entity，玩家实�?
// ══════════════════════════════════════════════════════════════════
class PlayerActor : public Entity {
public:
    using Ptr     = std::shared_ptr<PlayerActor>;
    using PlayerMap = std::unordered_map<int32_t, Ptr>;

    PlayerActor(int32_t id, const std::string& name, ClassType cls,
                core::IConnection::Ptr conn)
        : Entity()
        , nickname_(name)
        , class_type_(cls)
        , conn_(std::move(conn))
        , base_stats_(DefaultBaseStats(cls))
        , login_time_(std::chrono::steady_clock::now()) {
        SetEntityId(id);
        pos_.mapId  = Maps::kMapBegin;
        pos_.x      = 50;
        pos_.y      = 50;
        spawn_pos_  = pos_;
        current_hp_ = base_stats_.hpMax;
        current_mp_ = base_stats_.mpMax;
    }

    ~PlayerActor() override = default;

    // ── Entity 接口实现 ─────────────────────────────────────────
    EntityType Type() const override { return EntityType::PLAYER; }
    void OnTick(int64_t nowMs, int32_t dtMs) override {
        Tick(dtMs);
    }

    // ── 基础访问�?──────────────────────────────────────────────
    int32_t  PlayerId()        const { return EntityId(); }
    const std::string& Nickname() const { return nickname_; }
    ClassType  GetClassType() const { return class_type_; }
    int32_t    Level()         const { return level_; }
    int64_t    Exp()           const { return exp_; }
    int64_t    Gold()          const { return gold_; }
    int32_t    CurrentHp()     const { return current_hp_; }
    int32_t    CurrentMp()     const { return current_mp_; }
    int32_t    MaxHp()         const { return TotalMaxHp(); }
    int32_t    MaxMp()         const { return TotalMaxMp(); }
    const PlayerPos& Pos()     const { return pos_; }
    const PlayerPos& SpawnPos() const { return spawn_pos_; }
    const PKInfo& GetPKInfo() const { return pk_info_; }
    const PlayerGuildCache& Guild()   const { return guild_info_; }
    const BaseStats& GetBaseStats() const { return base_stats_; }
    const BaseStats& EquipStats()   const { return equip_stats_; }
    const BaseStats& TotalStats()   const { return CalcTotalStats(); }

    // ── 发送消�?────────────────────────────────────────────────
    void Send(int32_t msgId, const char* data, size_t len) {
        auto c = conn_.lock();
        if (c) c->Send(core::Msg::Create(msgId, data, len));
    }
    void Send(int32_t msgId, const std::string& json) {
        Send(msgId, json.data(), json.size());
    }

    // ── 背包操作 ────────────────────────────────────────────────
    int AddItem(int32_t itemId, int32_t count);
    int RemoveItem(int32_t itemId, int32_t count);
    int FindItemSlot(int32_t itemId) const;
    bool HasItem(int32_t itemId, int32_t count = 1) const;
    const ItemSlot& GetBag(int32_t slot) const {
        return (slot >= 0 && slot < kBagSlots) ? bag_[slot] : ItemSlot{};
    }

    // ── 装备操作 ────────────────────────────────────────────────
    void Equip(int8_t slot, int32_t itemId, int32_t dur);
    void Unequip(int8_t slot);
    const ItemSlot& GetEquip(int8_t slot) const {
        return (slot >= 0 && slot < kEquipSlots) ? equip_[slot] : ItemSlot{};
    }
    bool IsEquipped(int8_t slot) const { return equip_[slot].IsValid(); }

    // ── 经验/升级 ───────────────────────────────────────────────
    static int64_t ExpNeeded(int32_t level);
    bool AddExp(int64_t exp);
    void LevelUp();

    // ── 金币 ────────────────────────────────────────────────────
    void SpendGold(int64_t amount) { gold_ = std::max(int64_t(0), gold_ - amount); }
    void AddGold(int64_t amount)   { gold_ += amount; }

    // ── PK 操作 ─────────────────────────────────────────────────
    void OnKillPlayer();
    void SetPKStatus(PKStatus st) { pk_info_.status = st; }

    // ── 行会操作 ────────────────────────────────────────────────
    void JoinGuild(int32_t guildId, const std::string& name, int32_t rank);
    void LeaveGuild();

    // ── 移动 ────────────────────────────────────────────────────
    bool MoveTo(int32_t mapId, int32_t x, int32_t y);

    // ── 主循�?──────────────────────────────────────────────────
    void Tick(long long elapsedMs);
    void Regenerate(long long elapsedMs);

    // ── 连接 ────────────────────────────────────────────────────
    core::IConnection::Ptr GetConn() const { return conn_.lock(); }
    bool IsOnline() const { return conn_.lock() != nullptr; }

    // ── HP/MP 操作 ──────────────────────────────────────────────
    void TakeDamage(int32_t dmg) { current_hp_ = std::max(0, current_hp_ - dmg); }
    void SetHp(int32_t hp) { current_hp_ = std::min(TotalMaxHp(), hp); }
    void SetMp(int32_t mp) { current_mp_ = std::min(TotalMaxMp(), mp); }

    // ── 调试序列�?──────────────────────────────────────────────
    std::string DumpState() const;

private:
    BaseStats CalcTotalStats() const;
    int32_t   TotalMaxHp() const;
    int32_t   TotalMaxMp() const;

    std::string                  nickname_;
    const ClassType              class_type_;
    std::weak_ptr<core::IConnection> conn_;

    PKInfo    pk_info_;
    PlayerGuildCache guild_info_;

    int32_t   level_   = 1;
    int64_t  exp_     = 0;
    int64_t  gold_    = 0;

    BaseStats base_stats_;
    BaseStats equip_stats_;
    int32_t   current_hp_ = 0;
    int32_t   current_mp_ = 0;

    ItemSlot bag_[kBagSlots]      = {};
    ItemSlot equip_[kEquipSlots]  = {};
    std::unordered_map<int32_t, int32_t> skills_; // skillId -> level

    PlayerPos spawn_pos_;  // 出生点（副本/传送用�?

    int32_t       attack_target_id_ = 0;
    int64_t       last_attack_ts_   = 0;
    int64_t       last_skill_ts_    = 0;

    std::chrono::steady_clock::time_point login_time_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
