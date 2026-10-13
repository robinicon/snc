# 19 — Actor 生命周期管理

**目标**：理解 PlayerActor 的完整生命周期（登录 → 在线 → 踢出 → 析构），以及如何通过 `weak_ptr` 安全地跨线程管理连接。

## 背景

在 MMORPG 服务器中，每个玩家对应一个 **Actor**。Actor 的生命周期与网络连接紧密绑定：

```
登录 ──→ 在线（Tick 驱动）──→ 断线/踢出 ──→ 析构
  │                          │
  │  conn_ (weak_ptr)        │  OnMessage / Tick 通过 lock() 检查有效性
  │  跨线程安全访问           │  若 lock() 返回 nullptr 则跳过操作
```

核心挑战：**IO 线程**关闭连接时，**Worker 线程**可能仍持有对该连接的引用。使用 `weak_ptr` 包装 `IConnection::Ptr` 可避免 use-after-free。

## PlayerActor 设计

`snc::mmorpg::PlayerActor` 是游戏服务中的玩家实体基类：

```cpp
// include/snc/mmorpg/GameService.h
class PlayerActor : public Actor {
public:
    PlayerActor(int32_t id, const std::string& name,
                core::IConnection::Ptr conn, MapManager::Ptr mapMgr);

    // 发送消息：安全地通过 weak_ptr 获取连接
    void Send(int32_t msgId, const char* data, size_t len);

    // 获取连接（可返回 nullptr）
    core::IConnection::Ptr GetConn() const { return conn_.lock(); }

private:
    std::weak_ptr<core::IConnection> conn_;  // ← 关键：弱引用
    // ...
};
```

### 为什么用 `weak_ptr` 而非 `shared_ptr`？

| 方式 | 问题 |
|------|------|
| `shared_ptr<IConnection>` | 持有强引用，连接无法析构 → 内存泄漏 |
| `raw pointer` | 连接关闭后悬空指针 → use-after-free |
| `weak_ptr<IConnection>` ✅ | 不延长生命周期，`lock()` 安全获取临时强引用 |

## 生命周期流程

### 1. 登录（CreatePlayer_）

```cpp
// GameService 内部
PlayerActor::Ptr GameService::CreatePlayer_(int32_t playerId,
                                            const std::string& name,
                                            core::IConnection::Ptr conn)
{
    auto player = std::make_shared<PlayerActor>(playerId, name, std::move(conn), map_mgr_);
    {
        std::lock_guard<std::mutex> lock(players_mtx_);
        players_[playerId] = player;
    }
    // 可选：向 Gate 注册玩家信息
    return player;
}
```

- 传入的 `conn` 是 `shared_ptr`，存入 `PlayerActor::conn_` 时被自动转换为 `weak_ptr`
- `players_` 哈希表持有 `shared_ptr<PlayerActor>`，保证 Actor 不被析构

### 2. 在线（Tick）

```cpp
void PlayerActor::Tick(long long elapsedMs)
{
    // 每次 Tick 检查连接是否仍然有效
    auto conn = conn_.lock();
    if (!conn) {
        // 连接已关闭，触发清理（通知 GameService 移除玩家）
        OnDisconnected();
        return;
    }

    // 正常业务逻辑（移动、战斗等）
    // ...
}
```

**要点**：
- `lock()` 返回临时 `shared_ptr`，仅在当前作用域有效
- 两次 Tick 之间连接可能已关闭，每次使用前必须重新 `lock()`
- 不要缓存 `lock()` 结果跨 Tick 使用

### 3. 断线/踢出（RemovePlayer_）

当玩家主动断开或管理员踢出时：

```cpp
void GameService::RemovePlayer_(int32_t playerId)
{
    PlayerActor::Ptr player;
    {
        std::lock_guard<std::mutex> lock(players_mtx_);
        auto it = players_.find(playerId);
        if (it == players_.end()) return;
        player = it->second;
        players_.erase(it);   // 移除强引用
    }

    // player 析构 → PlayerActor::~PlayerActor()
    // conn_.weak_ptr 自动失效，后续 lock() 返回 nullptr
}
```

- 从 `players_` 中移除 `shared_ptr<PlayerActor>`
- PlayerActor 析构 → `conn_` 被销毁（weak_ptr 失效）
- IO 线程那边的 `TcpConnection` 也已在断开回调中被释放

### 4. 析构顺序

```
PlayerActor 析构
  └─ conn_ (weak_ptr) 销毁 ← 不触发 IConnection 析构
  └─ map_mgr_ (shared_ptr) 引用计数 -1

IConnection（TcpConnection）析构
  └─ 仅当所有 shared_ptr 都释放后才发生
```

`weak_ptr` 不会延长 `IConnection` 的生命周期，因此不会造成循环引用或内存泄漏。

## 完整示例：PlayerActor 集成到 GameService

```cpp
#include <snc/mmorpg/GameService.h>
#include <snc/core/TcpServer.h>
#include <snc/base/Log.h>

using namespace snc;
using namespace snc::mmorpg;
using namespace snc::core;

void GameService::Start(uint16_t rpcPort, const std::string& serviceName)
{
    running_ = true;

    // 1. 启动 RPC 服务端（接收 Gate 转发消息）
    rpc_server_ = std::make_shared<rpc::RpcServer>();
    rpc_server_->RegisterHandler(kMsgGameFromGate,
        [this](IConnection::Ptr conn, rpc::RpcSession::Ptr session,
               Msg::Ptr msg,
               std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
            OnFromGate_(conn, session, std::move(msg), respond);
        });
    rpc_server_->Listen(rpcPort);

    // 2. 注册到 Master 服
    // master_rpc_->Register(serviceName, ...);

    // 3. 启动心跳线程
    heartbeat_thread_ = std::thread(&GameService::HeartbeatLoop_, this);

    SNC_LOG_INFO("Game", "Game service started on RPC port %d", rpcPort);
}

void GameService::OnFromGate_(IConnection::Ptr conn, rpc::RpcSession::Ptr session,
                               Msg::Ptr msg,
                               std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond)
{
    int32_t msgId   = msg->MsgId();
    auto    payload = msg->Payload();
    size_t  len     = msg->PayloadLen();

    if (msgId == /* 登录 */ 0x2001) {
        int32_t playerId = 0;
        std::memcpy(&playerId, payload, sizeof(int32_t));
        std::string name = std::string(payload + sizeof(int32_t),
                                       payload + len);
        auto player = CreatePlayer_(playerId, name, conn);
        SNC_LOG_INFO("Game", "Player login: id=%d name=%s", playerId, name.c_str());
        respond(RpcError::OK, nullptr, 0);

    } else if (msgId == /* 业务消息 */ 0x2002) {
        // 查找当前连接的 player，执行逻辑
        // ...
        respond(RpcError::OK, nullptr, 0);
    }
}

void GameService::RemovePlayer_(int32_t playerId)
{
    PlayerPtr player;
    {
        std::lock_guard<std::mutex> lock(players_mtx_);
        auto it = players_.find(playerId);
        if (it == players_.end()) return;
        player = it->second;
        players_.erase(it);
    }
    // player 析构，conn_ weak_ptr 失效
    SNC_LOG_INFO("Game", "Player removed: id=%d", playerId);
}

void GameService::Shutdown()
{
    running_ = false;
    // 等待所有玩家退出（最多 30 秒）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!players_.empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 强制移除剩余玩家
    {
        std::lock_guard<std::mutex> lock(players_mtx_);
        players_.clear();
    }
    if (rpc_server_) rpc_server_->Stop();
    heartbeat_thread_.join();
    SNC_LOG_INFO("Game", "Game service shut down.");
}
```

## 常见陷阱

### 陷阱 1：缓存 lock() 结果

```cpp
// ❌ 错误：两次 lock 之间连接可能已关闭
auto conn = player->GetConn();
// ... 若干帧之后 ...
conn->Send(msg);  // 💥 若连接已关闭则崩溃

// ✅ 正确：每次使用前重新 lock()
auto conn = player->GetConn();
if (conn) conn->Send(msg);
```

### 陷阱 2：在析构函数中使用 conn_

```cpp
// ❌ 错误：析构函数中 conn_ 正在被销毁，不能调用 lock()
PlayerActor::~PlayerActor() {
    auto conn = conn_.lock();  // 未定义行为
    if (conn) conn->Close();
}

// ✅ 正确：析构函数不做网络操作
PlayerActor::~PlayerActor() = default;
```

### 陷阱 3：跨线程未加锁访问 players_

```cpp
// ❌ 错误：HeartbeatLoop 直接访问 players_，无锁保护
void HeartbeatLoop_() {
    for (auto& [id, player] : players_) {  // 与 RemovePlayer_ 并发！
        player->Tick(elapsedMs);
    }
}

// ✅ 正确：迭代前拷贝，或使用读写锁
void HeartbeatLoop_() {
    while (running_) {
        std::vector<PlayerPtr> snapshot;
        {
            std::lock_guard<std::mutex> lock(players_mtx_);
            snapshot.reserve(players_.size());
            for (auto& [id, p] : players_) snapshot.push_back(p);
        }
        for (auto& player : snapshot) player->Tick(elapsedMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
```

## 与 EntityHandle 的关系

`PlayerActor::GetConn()` 返回的是 `shared_ptr<IConnection>`（通过 `weak_ptr::lock()` 临时获取）。

在 Worker 线程中更推荐使用 `EntityHandle<IConnection>`，它可以更优雅地处理跨线程传递：

```cpp
// 在 Worker 线程中使用 EntityHandle
workerPool_->Submit([handle = player->GetConnHandle()]() {
    auto conn = handle.lock();
    if (!conn) return;     // 连接已关闭
    conn->Send(msg);
});
```

参见 [12 — EntityHandle 详解](./12-entity-handle.md)

## 关键 API 速查

| 方法 | 说明 |
|------|------|
| `PlayerActor(id, name, conn, mapMgr)` | 构造函数，conn 转为 weak_ptr 存储 |
| `PlayerActor::Send(msgId, data, len)` | 安全发送，conn 无效时静默忽略 |
| `PlayerActor::GetConn()` | 返回 `shared_ptr`，可能为 `nullptr` |
| `GameService::CreatePlayer_(id, name, conn)` | 创建并注册玩家 |
| `GameService::RemovePlayer_(id)` | 移除玩家，触发析构 |
| `GameService::Shutdown()` | 优雅停机，等待玩家退出后清理 |

参见 [03 - 连接生命周期](./03-connection-lifecycle.md)、[12 — EntityHandle 详解](./12-entity-handle.md)
