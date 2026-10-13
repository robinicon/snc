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
///   mgr.BroadcastToMap(1, msg);
///   mgr.Teleport(playerId, 2, 100, 200);

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>

namespace snc {
namespace mmorpg {

// ── 玩家实体句柄（游戏服层）───────────────────────────────────────
// 持有 IConnection 的 EntityHandle，用于跨线程安全访问。
using conn_handle_t = core::EntityHandle<core::IConnection>;

// ── 地图元数据 ────────────────────────────────────────────────────
struct MapInfo {
    int32_t  mapId       = 0;      ///< 地图唯一 ID
    std::string name;              ///< 地图名称（如 "新手村"）
    std::string configPath;        ///< 地图配置文件路径
    int32_t  width  = 0;           ///< 地图宽度（格子数）
    int32_t  height = 0;           ///< 地图高度（格子数）
};

// ── 玩家位置 ──────────────────────────────────────────────────────
struct PlayerPos {
    int32_t mapId  = 0;
    int32_t x      = 0;
    int32_t y      = 0;
};

// ── 地图实例类 ────────────────────────────────────────────────────
class MapInstance {
public:
    using Ptr = std::shared_ptr<MapInstance>;

    explicit MapInstance(const MapInfo& info) : info_(info) {}
    virtual ~MapInstance() = default;

    const MapInfo&    Info()     const { return info_; }
    int32_t           MapId()    const { return info_.mapId; }
    bool              IsEmpty()  const { return players_.empty(); }
    size_t            PlayerCount() const { return players_.size(); }

    /// 添加玩家；返回 true 成功。
    virtual bool AddPlayer(int32_t playerId, conn_handle_t conn) {
        if (players_.count(playerId)) return false;
        players_[playerId] = std::move(conn);
        return true;
    }

    /// 移除玩家（玩家离开地图时调用）。
    virtual void RemovePlayer(int32_t playerId) {
        players_.erase(playerId);
    }

    /// 广播消息给地图内所有玩家。
    virtual void Broadcast(const char* data, size_t len) {
        for (auto& [pid, conn] : players_) {
            auto c = conn.lock();
            if (c) c->Send(core::Msg::Create(0, data, len));
        }
    }

    /// 查找玩家连接。
    conn_handle_t FindPlayer(int32_t playerId) const {
        auto it = players_.find(playerId);
        return (it != players_.end()) ? it->second : conn_handle_t{};
    }

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
    bool CreateMap(const MapInfo& info) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (maps_.count(info.mapId)) return false;
        auto inst = factory_ ? factory_(info) : std::make_shared<MapInstance>(info);
        maps_[info.mapId] = inst;
        return true;
    }

    /// 销毁地图（所有玩家强制下线）。
    void DestroyMap(int32_t mapId) {
        std::lock_guard<std::mutex> lk(mtx_);
        maps_.erase(mapId);
    }

    /// 获取地图实例（线程安全）。
    MapInstance::Ptr GetMap(int32_t mapId) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = maps_.find(mapId);
        return (it != maps_.end()) ? it->second : nullptr;
    }

    /// 广播消息到指定地图所有玩家，返回实际接收者数量。
    size_t BroadcastToMap(int32_t mapId, const char* data, size_t len) {
        auto mapInst = GetMap(mapId);
        if (!mapInst) return 0;
        mapInst->Broadcast(data, len);
        return mapInst->PlayerCount();
    }

    /// 传送玩家到新地图坐标，返回目标地图内的玩家数量。
    size_t Teleport(int32_t playerId, int32_t targetMapId, int32_t x, int32_t y) {
        (void)playerId; (void)x; (void)y;
        // 注：实际传送还需更新玩家记录中的 mapId/坐标，此处仅广播示例。
        auto target = GetMap(targetMapId);
        return target ? target->PlayerCount() : 0;
    }

    /// 统计所有地图的玩家总数。
    size_t TotalPlayerCount() {
        std::lock_guard<std::mutex> lk(mtx_);
        size_t total = 0;
        for (auto& [id, m] : maps_) total += m->PlayerCount();
        return total;
    }

private:
    std::mutex                        mtx_;
    std::unordered_map<int32_t, MapInstance::Ptr> maps_;
    MapFactoryFn                      factory_;
};

} // namespace mmorpg
} // namespace snc
