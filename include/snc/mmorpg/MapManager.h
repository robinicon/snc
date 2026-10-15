#pragma once

/// @file MapManager.h
/// @brief 地图实例管理器 — Game 服内管理多个地图实例。
///
/// 职责：
///   - 创建 / 销毁地图实例
///   - 按 mapId 查找地图
///   - 广播消息到指定地图的所有在线玩家
///   - 传送玩家到新地图
///
/// 设计说明（ADR-002）：开发阶段 Map 服合并进 Game 服，
/// 生产部署时 MapManager 可拆分到独立 Map 进程。
///
/// 使用方式：
///   MapManager mgr;
///   mgr.CreateMap(1, "map_001.json");
///   mgr.BroadcastToMap(1, msgId, msg);
///   mgr.Teleport(playerId, 2, 100, 200);

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>

#include "snc/core/IConnection.h"
#include "snc/mmorpg/PlayerPos.h"

namespace snc {
namespace mmorpg {

// ── 玩家实体句柄（游戏服层）───────────────────────────────────────
// 持有 IConnection 的 EntityHandle，用于跨线程安全访问。
using conn_handle_t = core::EntityHandle<core::IConnection>;

// ── 地图元数据 ────────────────────────────────────────────────────
struct MapInfo {
    int32_t  mapId        = 0;      ///< 地图唯一 ID
    std::string name;               ///< 地图名称（如 "新手村"）
    std::string configPath;         ///< 地图配置文件路径
    int32_t  width  = 0;            ///< 地图宽度（格子数）
    int32_t  height = 0;            ///< 地图高度（格子数）

    // 传奇版扩展字段
    bool     is_safe_zone     = false;  ///< 安全区（比奇/盟重/沙巴克城内，禁止PK）
    bool     is_dungeon       = false;  ///< 副本地图
    int32_t  min_level        = 1;     ///< 进入最低等级要求
    std::string bg_name;                ///< 帮派战地图名（空=非战场）
    int32_t  bg_monster_count = 0;     ///< 攻城战期间额外怪物数量
};

// ── 地图实例类 ────────────────────────────────────────────────────
class MapInstance {
public:
    using Ptr = std::shared_ptr<MapInstance>;

    explicit MapInstance(const MapInfo& info) : info_(info) {}
    virtual ~MapInstance() = default;

    const MapInfo&    Info()      const { return info_; }
    int32_t           MapId()     const { return info_.mapId; }
    bool              IsSafeZone()const { return info_.is_safe_zone; }
    bool              IsDungeon() const { return info_.is_dungeon; }
    bool              IsEmpty()   const { return players_.empty(); }
    size_t            PlayerCount() const { return players_.size(); }

    /// 添加玩家；返回 true 成功。
    bool AddPlayer(int32_t playerId, conn_handle_t conn);

    /// 移除玩家（玩家离开地图时调用）。
    void RemovePlayer(int32_t playerId);

    /// 广播消息给地图内所有玩家。
    void Broadcast(int32_t msgId, const char* data, size_t len);

    /// 查找玩家连接。
    conn_handle_t FindPlayer(int32_t playerId) const;

protected:
    MapInfo       info_;
    std::unordered_map<int32_t, conn_handle_t> players_;
};

// ── 地图实例工厂函数类型 ─────────────────────────────────────────
using MapFactoryFn = std::function<std::shared_ptr<MapInstance>(const MapInfo&)>;

// ── MapManager — 地图实例管理器 ───────────────────────────────────
class MapManager {
public:
    using Ptr = std::shared_ptr<MapManager>;

    MapManager() = default;
    ~MapManager() = default;

    /// 注册自定义地图工厂（默认使用 MapInstance）。
    void SetFactory(MapFactoryFn fn) { factory_ = std::move(fn); }

    /// 创建地图实例，失败返回 false。
    bool CreateMap(const MapInfo& info);

    /// 销毁地图（所有玩家强制下线）。
    void DestroyMap(int32_t mapId);

    /// 获取地图实例（线程安全）。
    MapInstance::Ptr GetMap(int32_t mapId);

    /// 获取地图元数据（只读），无此地图返回 nullptr。
    const MapInfo* GetMapInfo(int32_t mapId) const;

    /// 广播消息到指定地图所有玩家，返回实际接收者数量。
    size_t BroadcastToMap(int32_t mapId, int32_t msgId, const char* data, size_t len);

    /// 传送玩家到新地图坐标，返回目标地图内的玩家数量。
    size_t Teleport(int32_t playerId, int32_t targetMapId, int32_t x, int32_t y);

    /// 统计所有地图的玩家总数。
    size_t TotalPlayerCount();

    /// 列出所有已加载的地图 ID。
    std::vector<int32_t> ListMapIds() const;

private:
    mutable std::mutex                         mtx_;
    std::unordered_map<int32_t, MapInstance::Ptr> maps_;
    MapFactoryFn                               factory_;
};

} // namespace mmorpg
} // namespace snc
