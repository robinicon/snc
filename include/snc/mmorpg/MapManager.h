// SPDX-License-Identifier: MIT
#pragma once

/// @file MapManager.h
///
/// @deprecated
///   **MapManager 已废弃，请使用 SceneMgr 替代。**
///
///   SceneMgr（传奇版场景管理器，见 `snc/mmorpg/legend/SceneMgr.h`）提供：
///   - 多场景切换、动态加载/卸载
///   - 实体（玩家/NPC/怪物）跨场景统一管理
///   - 高频战斗逻辑（C++），低频业务逻辑（Lua 脚本）
///
///   MapManager 保留仅用于示例代码和向后兼容，不再进行功能维护。
///   新开发请直接使用 `snc_mmo_legend::SceneMgr`。
///
/// @see snc/mmorpg/legend/SceneMgr.h
/// @see docs/tutorials/22-legend-overview.md

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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>

#include "snc/core/IConnection.h"
#include "snc/core/EntityHandle.h"
#include "snc/mmorpg/PlayerPos.h"

namespace snc {
namespace mmorpg {

using conn_handle_t = core::EntityHandle<core::IConnection>;

/// @brief 地图实例描述。
struct MapInfo {
    int32_t mapId    = 0;
    std::string name;
    int32_t width    = 0;
    int32_t height   = 0;
    std::string file_path;
};

/// @brief 单张地图的实例，包含在线玩家列表。
class MapInstance {
public:
    using Ptr = std::shared_ptr<MapInstance>;

    explicit MapInstance(MapInfo info);

    const MapInfo& Info() const { return info_; }
    int32_t MapId() const { return info_.mapId; }

    bool AddPlayer(int32_t playerId, conn_handle_t conn);
    void RemovePlayer(int32_t playerId);
    bool ContainsPlayer(int32_t playerId) const;
    conn_handle_t FindPlayer(int32_t playerId) const;
    bool IsEmpty() const { return players_.empty(); }
    size_t PlayerCount() const { return players_.size(); }

    void Broadcast(int32_t msgId, const char* data, size_t len);

private:
    MapInfo info_;
    std::mutex mtx_;
    std::unordered_map<int32_t, conn_handle_t> players_;
};

/// @brief 地图管理器（⚠️ 已废弃，请使用 SceneMgr）
///
/// @deprecated 见文件顶部说明。
class MapManager {
public:
    using Ptr       = std::shared_ptr<MapManager>;
    using MapPtr    = MapInstance::Ptr;
    using FactoryFn = std::function<MapPtr(const MapInfo&)>;

    MapManager() = default;
    explicit MapManager(FactoryFn factory) : factory_(std::move(factory)) {}

    bool       CreateMap(const MapInfo& info);
    void       DestroyMap(int32_t mapId);
    MapPtr     GetMap(int32_t mapId);
    const MapInfo* GetMapInfo(int32_t mapId) const;
    size_t     BroadcastToMap(int32_t mapId, int32_t msgId, const char* data, size_t len);
    size_t     Teleport(int32_t playerId, int32_t targetMapId, int32_t x, int32_t y);
    size_t     TotalPlayerCount();
    std::vector<int32_t> ListMapIds() const;
    size_t     MapCount() const;

private:
    mutable std::mutex          mtx_;
    std::unordered_map<int32_t, MapPtr> maps_;
    FactoryFn                     factory_;
};

} // namespace mmorpg
} // namespace snc
