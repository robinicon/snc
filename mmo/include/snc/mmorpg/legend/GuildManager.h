#pragma once

/// @file GuildManager.h
/// @brief 行会系统 �?创建/加入/退�?解散、行会成员管理、行会仓库、攻城战
///
/// 行会等级�?
///   Lv.1（创建条件：5�?5000金币�?
///   每升一级需要：成员数�?000经验
///
/// 行会职位�?
///   1=会长（Master�? 2=副会（Vice�? 3=成员（Member�?
///
/// 权限矩阵�?
///   会长：全权限（踢�?招新/转移/解散/仓库�?
///   副会：招�?踢普通成�?仓库存取
///   成员：仓库读�?聊天

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include <functional>

#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── 行会信息 ─────────────────────────────────────────────────────
struct GuildMember {
    int32_t player_id    = 0;
    std::string nickname;
    int32_t rank         = 3; // 1=会长 2=副会 3=成员
    int64_t joined_at    = 0;
    int32_t total_kills  = 0; // 行会累计击杀
};

struct GuildInfo {
    int32_t          guild_id    = 0;
    std::string      name;
    int32_t          level     = 1;
    int64_t          exp       = 0; // 行会经验
    int64_t          treasury  = 0; // 行会仓库金币
    std::string      master_id; // 会长 player_id
    std::vector<GuildMember> members;
    int32_t          member_count = 0;
    int32_t          max_members  = 100; // Lv.1 上限 100
    bool             is_at_war    = false; // 当前是否在攻城战�?
};

// ══════════════════════════════════════════════════════════════════
// GuildManager
// ══════════════════════════════════════════════════════════════════
class GuildManager {
public:
    using Ptr      = std::shared_ptr<GuildManager>;
    using GuildPtr = std::shared_ptr<GuildInfo>;
    using OnGuildChanged = std::function<void(int32_t guildId)>;

    GuildManager() = default;
    ~GuildManager() = default;

    // ── 创建/加入/退�?──────────────────────────────────────────
    /// 创建行会，成功返�?guild_id�?0），失败返回 0
    int32_t CreateGuild(PlayerActor::Ptr leader, const std::string& name, int32_t levelReq = 5, int32_t goldCost = 5000);
    /// 申请加入行会
    bool  JoinGuild(PlayerActor::Ptr player, int32_t guildId);
    /// 踢出玩家（仅会长/副会可操作）
    bool  KickMember(PlayerActor::Ptr operator_, int32_t targetId);
    /// 退出行�?
    void  LeaveGuild(PlayerActor::Ptr player);
    /// 解散行会（仅会长�?
    void  DissolveGuild(PlayerActor::Ptr leader);

    // ── 信息查询 ────────────────────────────────────────────────
    GuildPtr GetGuild(int32_t guildId) const;
    GuildPtr GetGuildByName(const std::string& name) const;
    GuildMember* GetMember(int32_t guildId, int32_t playerId);
    std::vector<GuildPtr> ListAllGuilds() const;
    int32_t NextGuildId() const { return next_guild_id_; }

    // ── 权限检�?────────────────────────────────────────────────
    bool HasPermission(const GuildInfo& guild, int32_t memberRank, int32_t action) const;
    // action: 0=none 1=kick 2=invite 3=transfer 4=dissolve 5=treasury_put 6=treasury_take

    // ── 行会仓库 ────────────────────────────────────────────────
    bool PutGold(int32_t guildId, int32_t playerId, int64_t amount);
    bool TakeGold(int32_t guildId, int32_t playerId, int64_t amount);

    // ── 攻城�?──────────────────────────────────────────────────
    void StartSiege(int32_t guildId, int32_t mapId);
    void EndSiege(int32_t guildId);
    bool IsAtWar(int32_t guildId) const;

    // ── 回调 ────────────────────────────────────────────────────
    void SetChangedCallback(OnGuildChanged cb) { on_changed_ = std::move(cb); }

private:
    int32_t AssignNextId_();
    void    NotifyChanged_(int32_t guildId);

    mutable std::mutex mtx_;
    std::unordered_map<int32_t, GuildPtr> guilds_;
    std::unordered_map<std::string, int32_t> name_to_id_; // 名称索引（大小写不敏感存储用小写副本�?
    std::unordered_map<int32_t, int32_t> player_to_guild_; // player_id -> guild_id
    int32_t next_guild_id_ = 1;
    OnGuildChanged on_changed_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
