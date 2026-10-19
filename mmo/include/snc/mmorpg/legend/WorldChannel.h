// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
#pragma once

/// @file WorldChannel.h
/// @brief 世界频道广播 �?跨所有在线玩家的全服聊天
///
/// 支持频道�?
///   0 = 世界频道（所有玩家可见）
///   1 = 行会频道（同行会成员可见�?
///   2 = 私聊（点对点�?
///
/// 过滤规则�?
///   - 安全区内禁止辱骂（关键词过滤，可扩展�?
///   - 消息长度限制�?00字符�?
///   - 防刷屏（每玩�?1 �?cooldown�?

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <mutex>
#include <deque>
#include <functional>

#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace mmorpg {
namespace legend {

// ── 聊天频道 ─────────────────────────────────────────────────────
enum class ChatChannel : int8_t {
    WORLD  = 0, // 世界频道
    GUILD  = 1, // 行会频道
    WHISPER = 2, // 私聊
};

constexpr const char* ChannelName(ChatChannel ch) {
    switch (ch) {
        case ChatChannel::WORLD:    return "WORLD";
        case ChatChannel::GUILD:    return "GUILD";
        case ChatChannel::WHISPER:  return "WHISPER";
        default:                    return "UNKNOWN";
    }
}

// ── 聊天消息 ─────────────────────────────────────────────────────
struct ChatMessage {
    int32_t  sender_id    = 0;
    std::string sender_name;
    ChatChannel channel   = ChatChannel::WORLD;
    std::string content;
    int64_t  timestamp    = 0; // Unix �?
    int32_t  target_id    = 0; // 私聊目标（channel=WHISPER 时有效）
    int32_t  guild_id     = 0; // 行会频道�?
};

// ── 聊天历史记录（环形缓冲）──────────────────────────────────────
struct ChatHistory {
    static constexpr int kMaxEntries = 50;
    std::deque<ChatMessage> messages;
    void Push(const ChatMessage& msg) {
        messages.push_back(msg);
        while (messages.size() > kMaxEntries) messages.pop_front();
    }
    const std::vector<ChatMessage>& GetRecent(int count) const {
        static std::vector<ChatMessage> buf;
        buf.clear();
        int n = std::min(count, (int)messages.size());
        for (int i = (int)messages.size() - 1; i >= (int)messages.size() - n; --i)
            buf.push_back(messages[i]);
        return buf;
    }
};

// ══════════════════════════════════════════════════════════════════
// WorldChannel
// ══════════════════════════════════════════════════════════════════
class WorldChannel {
public:
    using Ptr       = std::shared_ptr<WorldChannel>;
    using PlayerMap = std::unordered_map<int32_t, PlayerActor::Ptr>;
    using OnChatSent = std::function<void(const ChatMessage& msg)>;

    WorldChannel() = default;
    ~WorldChannel() = default;

    // ── 玩家注册/注销 ───────────────────────────────────────────
    void RegisterPlayer(int32_t playerId, PlayerActor::Ptr player);
    void UnregisterPlayer(int32_t playerId);

    // ── 发送聊�?────────────────────────────────────────────────
    /// 发送聊天消息，返回 true=成功（已通过过滤�?
    bool Send(ChatChannel channel, int32_t senderId,
              const std::string& content, int32_t targetId = 0,
              int32_t guildId = 0);

    // ── 查询 ────────────────────────────────────────────────────
    /// 获取最�?N 条世界频道消�?
    std::vector<ChatMessage> GetWorldHistory(int count = 20) const;
    /// 获取最�?N 条行会频道消�?
    std::vector<ChatMessage> GetGuildHistory(int guildId, int count = 20) const;
    /// 获取所有在线玩家列表（供广播使用）
    std::vector<PlayerActor::Ptr> GetAllPlayers() const;

    // ── 过滤配置 ────────────────────────────────────────────────
    void SetMaxMessageLen(int len) { max_msg_len_ = len; }
    void SetCooldownMs(int64_t ms) { cooldown_ms_ = ms; }
    void AddForbiddenWord(const std::string& word) { forbidden_.insert(word); }

    // ── 回调 ────────────────────────────────────────────────────
    void SetOnChatSent(OnChatSent cb) { on_chat_sent_ = std::move(cb); }

private:
    bool IsForbidden_(const std::string& content) const;
    bool PassCooldown_(int32_t playerId, int64_t nowMs) const;
    void BroadcastToGuild_(int32_t guildId, const ChatMessage& msg);

    PlayerMap           players_;
    ChatHistory         world_history_;
    std::unordered_map<int32_t, ChatHistory> guild_histories_; // guildId -> history
    std::unordered_map<int32_t, int64_t> last_send_ts_; // playerId -> 上次发送时间戳(ms)

    int  max_msg_len_  = 200;
    int64_t cooldown_ms_ = 1000;
    std::unordered_set<std::string> forbidden_;
    OnChatSent on_chat_sent_;
    mutable std::mutex mtx_;
};

} // namespace legend
} // namespace mmorpg
} // namespace snc
