// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file NPCManager.h
/// @brief NPC 管理系统 — 商店、传送、对话
///
/// 职责：
///   - 管理地图内的 NPC 列表
///   - 处理 NPC 交互（对话/购买/传送）
///   - 支持 NPC 刷新（定时重生）

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>

#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── NPC 类型 ─────────────────────────────────────────────────────
enum class NPCType : int8_t {
    SHOP      = 0,  // 商店（买卖物品）
    TELEPORT  = 1,  // 传送员（传送到其他地图）
    DIALOG    = 2,  // 对话（任务/剧情）
    QUEST     = 3,  // 任务 NPC
    TRAINING  = 4,  // 训练师（技能升级）
};

// ── NPC 数据结构 ─────────────────────────────────────────────────
struct NPCDef {
    int32_t npc_id    = 0;
    std::string name;
    NPCType type      = NPCType::DIALOG;
    int32_t map_id    = 0;
    int32_t x         = 0;
    int32_t y         = 0;

    // 商店相关（type=SHOP）
    struct ShopEntry {
        int32_t item_id = 0;
        int32_t price   = 0; // 商店售价（购买价）
        int32_t buy_price = 0; // 出售给NPC价（通常为售价的一半）
    };
    std::vector<ShopEntry> shop_items;

    // 传送相关（type=TELEPORT）
    struct TeleportDest {
        int32_t target_map_id = 0;
        int32_t tx = 0;
        int32_t ty = 0;
        int32_t cost_gold = 0; // 传送费用
        std::string label;     // 显示名称（如"新手村"）

        TeleportDest() = default;
        TeleportDest(int32_t map, int32_t x, int32_t y, int32_t cost, std::string name)
            : target_map_id(map), tx(x), ty(y), cost_gold(cost), label(std::move(name)) {}
    };
    std::vector<TeleportDest> teleports;

    // 对话文本（type=DIALOG/QUEST）
    std::string dialog_text;
    std::vector<std::pair<int32_t, std::string>> dialog_options; // (option_id, text)
};

// ── NPC 实例（运行时状态）────────────────────────────────────────
struct NPCInstance {
    NPCDef def;
    bool   is_interacting = false; // 当前是否正在对话中
};

// ══════════════════════════════════════════════════════════════════
// NPCManager
// ══════════════════════════════════════════════════════════════════
class NPCManager {
public:
    using Ptr   = std::shared_ptr<NPCManager>;
    using NPCPtr = std::shared_ptr<NPCInstance>;
    using ShopCallback = std::function<void(PlayerActor::Ptr player,
                                             int32_t npcId,
                                             int32_t itemId,
                                             int32_t count)>;

    NPCManager() = default;
    ~NPCManager() = default;

    // ── NPC 管理 ──────────────────────────────────────────────
    /// 注册一个 NPC 定义（可多次调用）
    void RegisterNPC(const NPCDef& def);
    /// 在指定地图放置 NPC（通过 npc_id 查找）
    bool PlaceNPC(int32_t mapId, int32_t npcId, int32_t x, int32_t y);
    /// 移除地图上的 NPC
    void RemoveNPC(int32_t mapId, int32_t npcId);
    /// 获取地图内所有 NPC（按距离排序）
    std::vector<NPCPtr> GetNearbyNPCs(int32_t mapId, int32_t x, int32_t y, int32_t range = 5);

    // ── 交互处理 ──────────────────────────────────────────────
    /// 与 NPC 对话（返回对话选项 JSON）
    std::string Talk(PlayerActor::Ptr player, int32_t npcId);
    /// 购买物品
    std::string Buy(PlayerActor::Ptr player, int32_t npcId, int32_t itemId, int32_t count);
    /// 出售物品
    std::string Sell(PlayerActor::Ptr player, int32_t npcId, int32_t itemId, int32_t count);
    /// 使用传送
    std::string Teleport(PlayerActor::Ptr player, int32_t npcId, int32_t destIndex);

    // ── 回调 ──────────────────────────────────────────────────
    void SetShopCallback(ShopCallback cb) { shop_cb_ = std::move(cb); }

private:
    // npc_id -> NPCDef
    std::unordered_map<int32_t, NPCDef> npc_defs_;
    // mapId -> (npcId -> NPCInstance)
    std::unordered_map<int32_t, std::unordered_map<int32_t, NPCPtr>> map_npcs_;
    ShopCallback shop_cb_;
    std::mutex mtx_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
