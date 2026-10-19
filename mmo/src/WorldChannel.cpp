// SPDX-License-Identifier: MIT
/// @file WorldChannel.cpp

#include "snc/mmorpg/legend/WorldChannel.h"
#include <algorithm>
#include <chrono>
#include <cctype>

namespace snc {
namespace mmorpg {
namespace legend {

void WorldChannel::RegisterPlayer(int32_t playerId, PlayerActor::Ptr player) {
    std::lock_guard<std::mutex> lk(mtx_);
    players_[playerId] = player;
}

void WorldChannel::UnregisterPlayer(int32_t playerId) {
    std::lock_guard<std::mutex> lk(mtx_);
    players_.erase(playerId);
    last_send_ts_.erase(playerId);
}

bool WorldChannel::Send(ChatChannel channel, int32_t senderId,
                         const std::string& content, int32_t targetId, int32_t guildId) {
    if (content.size() > static_cast<size_t>(max_msg_len_)) return false;
    if (IsForbidden_(content)) return false;
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!PassCooldown_(senderId, nowMs)) return false;

    ChatMessage msg;
    msg.sender_id    = senderId;
    msg.channel      = channel;
    msg.content      = content;
    msg.timestamp    = nowMs / 1000;
    msg.target_id    = targetId;
    msg.guild_id     = guildId;

    // 查找发送者名称
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = players_.find(senderId);
        if (it != players_.end()) msg.sender_name = it->second->Nickname();
    }

    // 广播
    switch (channel) {
        case ChatChannel::WORLD: {
            world_history_.Push(msg);
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& [pid, player] : players_)
                if (pid != senderId) player->Send(0x1200, msg.content.c_str(), msg.content.size());
            break;
        }
        case ChatChannel::GUILD: {
            guild_histories_[guildId].Push(msg);
            BroadcastToGuild_(guildId, msg);
            break;
        }
        case ChatChannel::WHISPER: {
            if (targetId > 0) {
                std::lock_guard<std::mutex> lk(mtx_);
                auto it = players_.find(targetId);
                if (it != players_.end())
                    it->second->Send(0x1201, msg.content.c_str(), msg.content.size());
            }
            break;
        }
    }

    last_send_ts_[senderId] = nowMs;
    if (on_chat_sent_) on_chat_sent_(msg);
    return true;
}

std::vector<ChatMessage> WorldChannel::GetWorldHistory(int count) const {
    return world_history_.GetRecent(count);
}

std::vector<ChatMessage> WorldChannel::GetGuildHistory(int guildId, int count) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = guild_histories_.find(guildId);
    if (it == guild_histories_.end()) return {};
    return it->second.GetRecent(count);
}

std::vector<PlayerActor::Ptr> WorldChannel::GetAllPlayers() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<PlayerActor::Ptr> result;
    for (auto& [pid, p] : players_) result.push_back(p);
    return result;
}

void WorldChannel::BroadcastToGuild_(int32_t guildId, const ChatMessage& msg) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [pid, player] : players_) {
        if (player->Guild().guild_id == guildId && pid != msg.sender_id)
            player->Send(0x1202, msg.content.c_str(), msg.content.size());
    }
}

bool WorldChannel::IsForbidden_(const std::string& content) const {
    std::string lower = content;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    for (auto& w : forbidden_) {
        std::string lw = w;
        std::transform(lw.begin(), lw.end(), lw.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (lower.find(lw) != std::string::npos) return true;
    }
    return false;
}

bool WorldChannel::PassCooldown_(int32_t playerId, int64_t nowMs) const {
    auto it = last_send_ts_.find(playerId);
    if (it == last_send_ts_.end()) return true;
    return (nowMs - it->second) >= cooldown_ms_;
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
