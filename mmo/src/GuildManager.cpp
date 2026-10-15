/// @file GuildManager.cpp

#include "snc/mmorpg/legend/GuildManager.h"
#include <algorithm>
#include <numeric>

namespace snc {
namespace mmorpg {
namespace legend {

int32_t GuildManager::AssignNextId_() {
    return next_guild_id_++;
}

void GuildManager::NotifyChanged_(int32_t guildId) {
    if (on_changed_) on_changed_(guildId);
}

// ── 创建/加入/退�?───────────────────────────────────────────────

int32_t GuildManager::CreateGuild(PlayerActor::Ptr leader, const std::string& name,
                                   int32_t levelReq, int32_t goldCost) {
    (void)levelReq;
    if (!leader || leader->Gold() < goldCost) return 0;
    // 检查同�?
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (name_to_id_.count(name)) return 0;
    }
    auto guild = std::make_shared<GuildInfo>();
    guild->guild_id    = AssignNextId_();
    guild->name        = name;
    guild->master_id   = std::to_string(leader->PlayerId());
    guild->treasury    = 0;
    guild->max_members = 100;

    GuildMember member;
    member.player_id  = leader->PlayerId();
    member.nickname   = leader->Nickname();
    member.rank       = 1; // 会长
    member.joined_at  = 0;
    guild->members.push_back(member);
    guild->member_count = 1;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        guilds_[guild->guild_id] = guild;
        name_to_id_[name] = guild->guild_id;
        player_to_guild_[leader->PlayerId()] = guild->guild_id;
    }
    leader->JoinGuild(guild->guild_id, name, 1);
    leader->SpendGold(goldCost);
    NotifyChanged_(guild->guild_id);
    return guild->guild_id;
}

bool GuildManager::JoinGuild(PlayerActor::Ptr player, int32_t guildId) {
    GuildPtr guild;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = guilds_.find(guildId);
        if (it == guilds_.end()) return false;
        guild = it->second;
        if (guild->member_count >= guild->max_members) return false;
    }
    GuildMember member;
    member.player_id  = player->PlayerId();
    member.nickname   = player->Nickname();
    member.rank       = 3; // 普通成�?
    member.joined_at  = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        guild->members.push_back(member);
        guild->member_count++;
        player_to_guild_[player->PlayerId()] = guildId;
    }
    player->JoinGuild(guildId, guild->name, 3);
    NotifyChanged_(guildId);
    return true;
}

bool GuildManager::KickMember(PlayerActor::Ptr operator_, int32_t targetId) {
    int32_t myGuildId = operator_->Guild().guild_id;
    if (!myGuildId) return false;
    GuildPtr guild;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = guilds_.find(myGuildId);
        if (it == guilds_.end()) return false;
        guild = it->second;
    }
    // 权限检查：仅会�?副会可踢�?
    bool canKick = false;
    for (auto& m : guild->members) {
        if (m.player_id == operator_->PlayerId() && (m.rank == 1 || m.rank == 2)) {
            canKick = true; break;
        }
    }
    if (!canKick) return false;
    // 不能踢会�?
    if (targetId == std::stoi(guild->master_id)) return false;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        guild->members.erase(
            std::remove_if(guild->members.begin(), guild->members.end(),
                [targetId](const GuildMember& m){ return m.player_id == targetId; }),
            guild->members.end());
        guild->member_count--;
        player_to_guild_.erase(targetId);
    }
    NotifyChanged_(myGuildId);
    return true;
}

void GuildManager::LeaveGuild(PlayerActor::Ptr player) {
    int32_t guildId = player->Guild().guild_id;
    if (!guildId) return;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = guilds_.find(guildId);
        if (it == guilds_.end()) return;
        auto& guild = it->second;
        guild->members.erase(
            std::remove_if(guild->members.begin(), guild->members.end(),
                [player](const GuildMember& m){ return m.player_id == player->PlayerId(); }),
            guild->members.end());
        guild->member_count--;
        player_to_guild_.erase(player->PlayerId());
        if (guild->members.empty()) {
            // 解散
            name_to_id_.erase(guild->name);
            guilds_.erase(it);
        }
    }
    player->LeaveGuild();
    NotifyChanged_(guildId);
}

void GuildManager::DissolveGuild(PlayerActor::Ptr leader) {
    int32_t guildId = leader->Guild().guild_id;
    if (!guildId) return;
    // 仅会长可解散
    auto guild = GetGuild(guildId);
    if (!guild || guild->master_id != std::to_string(leader->PlayerId())) return;

    std::vector<int32_t> memberIds;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& m : guild->members) memberIds.push_back(m.player_id);
        name_to_id_.erase(guild->name);
        guilds_.erase(guildId);
        for (int pid : memberIds)
            player_to_guild_.erase(pid);
    }
    for (int pid : memberIds)
        (void)pid;  // 每个成员通过 NotifyChanged_ 收到公会解散通知
    leader->LeaveGuild();
    NotifyChanged_(guildId);
}

// ── 查询 ──────────────────────────────────────────────────────────

GuildManager::GuildPtr GuildManager::GetGuild(int32_t guildId) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = guilds_.find(guildId);
    return (it != guilds_.end()) ? it->second : nullptr;
}

GuildManager::GuildPtr GuildManager::GetGuildByName(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) return nullptr;
    auto git = guilds_.find(it->second);
    return (git != guilds_.end()) ? git->second : nullptr;
}

GuildMember* GuildManager::GetMember(int32_t guildId, int32_t playerId) {
    auto guild = GetGuild(guildId);
    if (!guild) return nullptr;
    for (auto& m : guild->members)
        if (m.player_id == playerId) return &m;
    return nullptr;
}

std::vector<GuildManager::GuildPtr> GuildManager::ListAllGuilds() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<GuildPtr> result;
    for (auto& [id, g] : guilds_) result.push_back(g);
    return result;
}

// ── 权限检�?─────────────────────────────────────────────────────

bool GuildManager::HasPermission(const GuildInfo& guild, int32_t memberRank, int32_t action) const {
    (void)guild; (void)action;
    // 会长(1) 全权限；副会(2) 除解散外均可；成�?3) 仅读�?
    if (memberRank == 1) return true;
    if (memberRank == 2) return (action != 4); // 不能解散
    return false; // 成员无操作权�?
}

// ── 行会仓库 ──────────────────────────────────────────────────────

bool GuildManager::PutGold(int32_t guildId, int32_t playerId, int64_t amount) {
    if (amount <= 0) return false;
    auto guild = GetGuild(guildId);
    if (!guild) return false;
    // 检查是否有权限（会�?副会�?
    bool hasPerm = false;
    for (auto& m : guild->members)
        if (m.player_id == playerId && (m.rank == 1 || m.rank == 2)) { hasPerm = true; break; }
    if (!hasPerm) return false;
    guild->treasury += amount;
    NotifyChanged_(guildId);
    return true;
}

bool GuildManager::TakeGold(int32_t guildId, int32_t playerId, int64_t amount) {
    if (amount <= 0) return false;
    auto guild = GetGuild(guildId);
    if (!guild || guild->treasury < amount) return false;
    bool hasPerm = false;
    for (auto& m : guild->members)
        if (m.player_id == playerId && (m.rank == 1 || m.rank == 2)) { hasPerm = true; break; }
    if (!hasPerm) return false;
    guild->treasury -= amount;
    NotifyChanged_(guildId);
    return true;
}

// ── 攻城�?────────────────────────────────────────────────────────

void GuildManager::StartSiege(int32_t guildId, int32_t mapId) {
    (void)mapId;
    auto guild = GetGuild(guildId);
    if (guild) {
        std::lock_guard<std::mutex> lk(mtx_);
        guild->is_at_war = true;
    }
}

void GuildManager::EndSiege(int32_t guildId) {
    auto guild = GetGuild(guildId);
    if (guild) {
        std::lock_guard<std::mutex> lk(mtx_);
        guild->is_at_war = false;
    }
}

bool GuildManager::IsAtWar(int32_t guildId) const {
    auto guild = GetGuild(guildId);
    return guild ? guild->is_at_war : false;
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
