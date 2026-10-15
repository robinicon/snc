#pragma once

/// @file WorldService.h
/// @brief World 服 — 跨服场景管理 + 场景锁状态机。
///
/// 职责：
///   - 管理跨服场景（多人跨服战斗/活动）
///   - 场景锁状态机：IDLE → CREATING → ACTIVE → ENDING → IDLE
///   - 协调多个 Game 服参与同一场景
///   - 通知 Master 服场景状态变更
///
/// 场景锁状态机（ADR-003）：
///   IDLE         — 无场景运行
///   CREATING     — 正在创建场景，拒绝新玩家加入
///   ACTIVE       — 场景运行中，允许玩家进出
///   ENDING       — 场景即将结束，拒绝新玩家，等待老玩家退出
///
/// 使用方式：
///   WorldService world(master_rpc_client, registry);
///   world.Start(9100);

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>

#include "snc/discovery/ServiceRegistry.h"
#include "snc/rpc/RpcClient.h"
#include "snc/rpc/RpcServer.h"
#include "snc/core/Msg.h"
#include "snc/base/Log.h"

namespace snc {
namespace mmorpg {

// ── 场景锁状态 ───────────────────────────────────────────────────
enum class SceneState : int {
    IDLE      = 0,  ///< 空闲
    CREATING  = 1,  ///< 创建中
    ACTIVE    = 2,  ///< 运行中
    ENDING    = 3,  ///< 即将结束
};

// ── 场景描述 ─────────────────────────────────────────────────────
struct SceneInfo {
    int32_t           sceneId   = 0;
    std::string       name;
    SceneState        state     = SceneState::IDLE;
    std::vector<std::string> participating_games;  ///< 参与此场景的 Game 服名称列表
    int64_t           create_ts = 0;   ///< 创建时间戳
    int64_t           end_ts    = 0;   ///< 预期结束时间戳
};

// ── WorldService ─────────────────────────────────────────────────
class WorldService {
public:
    using Ptr     = std::shared_ptr<WorldService>;
    using ScenePtr = std::shared_ptr<SceneInfo>;

    /// master_rpc — 与 Master 服的 RPC 客户端（用于上报状态）
    /// registry   — 本地 ServiceRegistry（记录参与场景的 Game 服）
    WorldService(rpc::RpcClient::Ptr master_rpc,
                 discovery::ServiceRegistry::Ptr registry);

    ~WorldService() = default;

    /// 启动 World 服（阻塞调用）。
    void Start(uint16_t port);

    /// 停止 World 服。
    void Stop();

    /// 创建新场景，状态变为 CREATING。
    /// 返回 sceneId，失败返回 -1。
    int32_t CreateScene(const std::string& sceneName,
                        const std::vector<std::string>& gameList);

    /// 启动场景（状态变为 ACTIVE）。
    bool StartScene(int32_t sceneId);

    /// 结束场景（状态变为 ENDING，等待所有玩家退出后回到 IDLE）。
    bool EndScene(int32_t sceneId);

    /// 查询场景状态。
    std::string QueryScene(int32_t sceneId) const;

    /// 获取所有场景列表。
    std::vector<std::string> ListScenes() const;

    /// 创建跨服场景（多 Game 服参与同一场景）。
    /// 向每个参与的 Game 服发送 MSG_SCENE_BEGIN 事件。
    /// @param gameServerIds 参与此场景的 Game 服 ID 列表
    /// @return sceneId，失败返回 -1
    int32_t CreateCrossScene(const std::string& sceneName,
                             const std::vector<int32_t>& gameServerIds);

    /// Game 服掉线时调用，标记该服玩家离线并继续场景至结束。
    void OnGameServerDisconnected(int32_t gameServerId, int32_t sceneId);

    /// 结束跨服场景，向所有参与 Game 服发送 MSG_SCENE_END。
    bool EndCrossScene(int32_t sceneId);

    /// 注册 RPC 处理器（在 RpcServer 上调用）。
    void RegisterRpcHandlers(rpc::RpcServer& rpcServer);

private:
    // ── RPC 消息处理器 ─────────────────────────────────────────
    void OnSceneRequest_(core::IConnection::Ptr conn,
                         rpc::RpcSession::Ptr session,
                         core::Msg::Ptr msg,
                         std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond);

    // ── 状态转换辅助 ───────────────────────────────────────────
    bool Transition_(int32_t sceneId, SceneState from, SceneState to);
    void NotifyMaster_(int32_t sceneId, SceneState newState);
    void NotifyGameServers_(int32_t sceneId, SceneState newState,
                            const std::vector<int32_t>& gameIds);

    // ── 跨服场景路由 ───────────────────────────────────────────
    // sceneId -> (gameServerId -> RpcClient)
    std::unordered_map<int32_t, std::unordered_map<int32_t, rpc::RpcClient::Ptr>>
        cross_scene_clients_;
    mutable std::mutex cross_mtx_;

    rpc::RpcClient::Ptr             master_rpc_;
    discovery::ServiceRegistry::Ptr registry_;
    std::unordered_map<int32_t, ScenePtr> scenes_;
    mutable std::mutex                      scenes_mtx_;
    std::atomic<bool>                       running_{false};
    int32_t                                 next_scene_id_ = 1;
};

} // namespace mmorpg
} // namespace snc
