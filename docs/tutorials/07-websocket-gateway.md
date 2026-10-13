# 07 — WebSocket 网关

**目标**：了解如何用 `snc_websocket` 搭建 WebSocket 网关进程，实现客户端连接、JWT 鉴权、消息转发到游戏服务器。

## 概述

`snc_websocket` 是一个独立的 WebSocket 网关模块，设计为**单独进程**运行。它负责：

1. 接受客户端 WebSocket 连接（RFC 6455）
2. 通过 `IGatewayBridge::VerifyToken()` 验证 JWT
3. 将玩家消息转发给游戏服务器（通过 snc_rpc）
4. 将游戏服务器的推送消息转发给对应玩家

网关与游戏服务器之间使用 `snc_rpc` TCP 协议通信，与客户端之间使用标准 WebSocket 协议。两者事件循环独立。

## 配置 CMake

```cmake
option(SNC_ENABLE_WEBSOCKET "Enable WebSocket gateway module" ON)
add_subdirectory(libs/snc_websocket)
target_link_libraries(my_app PRIVATE snc_websocket snc_auth snc_base)
```

> **注意**：`snc_websocket` 当前使用 `select` 事件循环，适用于中小规模连接（< 1000）。
> 大规模部署时建议后续接入 `snc_core` 的 IOCP/epoll 事件循环。

头文件入口：

```cpp
#include <snc/ws/WsGateway.h>
```

## 核心抽象：IGatewayBridge

用户必须实现 `IGatewayBridge` 纯虚接口，将业务逻辑注入网关：

```cpp
class IGatewayBridge {
public:
    virtual ~IGatewayBridge() = default;

    // 玩家连接并鉴权成功（WebSocket 握手完成且 Token 有效）
    virtual void OnPlayerConnected(int32_t playerId, WsConnectionPtr conn) = 0;

    // 玩家断开连接（含异常断开）
    virtual void OnPlayerDisconnected(int32_t playerId, WsConnectionPtr conn) = 0;

    // 将玩家发送的消息转发给游戏服务器
    // 返回 true 表示转发成功
    virtual bool ForwardToGame(int32_t playerId, const char* payload, size_t len) = 0;

    // 将游戏服务器的推送消息转发给指定玩家
    virtual void RelayFromGame(int32_t playerId, const char* payload, size_t len) = 0;

    // 验证 JWT token，返回 playerId；失败返回 -1
    virtual int32_t VerifyToken(const std::string& token) = 0;
};
```

## WsConnection（供 Bridge 使用）

```cpp
class WsConnection : public std::enable_shared_from_this<WsConnection> {
public:
    int     Fd()         const;   // 底层 socket fd
    WsState State()      const;   // 当前状态
    int32_t PlayerId()   const;   // 已绑定的玩家 ID
    bool    IsAuthenticated() const;

    void SendBinary(const char* data, size_t len);  // 发送二进制帧
    void SendClose(int code = 1000, const char* reason = "");
    void SendPing();   // 心跳 ping

    using CloseCb = std::function<void(int32_t playerId, int closeCode)>;
    void SetCloseCallback(CloseCb cb);
};
```

## WsGateway

```cpp
class WsGateway {
public:
    explicit WsGateway(IGatewayBridge& bridge);

    bool Listen(int port);   // 监听端口，返回 true 表示成功
    void Run();              // 阻塞运行事件循环
    void Stop();             // 停止事件循环

    size_t ActiveConnectionCount() const;
};
```

## 完整示例

```cpp
#include <snc/ws/WsGateway.h>
#include <snc/auth/AuthService.h>
#include <iostream>
#include <unordered_map>

// ── 业务层 Bridge 实现 ────────────────────────────────────────
class MyGatewayBridge : public snc::ws::IGatewayBridge {
public:
    MyGatewayBridge(snc::auth::JwtService& jwt) : jwt_(jwt) {}

    int32_t VerifyToken(const std::string& token) override {
        auto jwtToken = jwt_.Verify(token);
        if (!jwtToken) return -1;
        // jwtToken->subject holds the "sub" claim (user/player ID string).
        // If the token was signed with a numeric player_id in "sub", parse it here.
        try {
            return static_cast<int32_t>(std::stol(jwtToken->subject));
        } catch (...) {
            return -1;
        }
    }

    void OnPlayerConnected(int32_t playerId, snc::ws::WsConnectionPtr conn) override {
        std::cout << "[WS] Player " << playerId << " connected\n";
        // TODO: 通知游戏服务器有新玩家上线
        // gameRpc->NotifyPlayerLogin(playerId);
    }

    void OnPlayerDisconnected(int32_t playerId, snc::ws::WsConnectionPtr conn) override {
        std::cout << "[WS] Player " << playerId << " disconnected\n";
        // TODO: 通知游戏服务器玩家离线
        // gameRpc->NotifyPlayerLogout(playerId);
    }

    bool ForwardToGame(int32_t playerId, const char* payload, size_t len) override {
        // TODO: 通过 snc_rpc 转发到游戏服务器
        // return gameRpc->SendToGame(playerId, payload, len);
        std::cout << "[WS] Forward to game: player=" << playerId
                  << " len=" << len << "\n";
        return true;
    }

    void RelayFromGame(int32_t playerId, const char* payload, size_t len) override {
        // TODO: 查找玩家对应的 WsConnection，发送消息
        // auto conn = FindConnection(playerId);
        // if (conn) conn->SendBinary(payload, len);
        (void)playerId; (void)payload; (void)len;
    }

private:
    snc::auth::JwtService& jwt_;
};

// ── 主函数 ─────────────────────────────────────────────────────
int main() {
    // 1. JWT 服务（与 HTTP 管理后台共用同一密钥）
    snc::auth::JwtService jwt("ws-secret-key-must-be-32-bytes!!", 7200, "", "HS256");

    // 2. 业务 Bridge
    MyGatewayBridge bridge(jwt);

    // 3. 启动 WebSocket 网关
    snc::ws::WsGateway gateway(bridge);
    if (!gateway.Listen(8765)) {
        std::cerr << "Failed to listen on :8765\n";
        return 1;
    }
    std::cout << "WebSocket gateway on :8765\n";
    gateway.Run();
    return 0;
}
```

## 连接状态机

```
                    ┌──────────────┐
                    │  HANDSHAKE   │  ← HTTP Upgrade 请求到达
                    └──────┬───────┘
                           │ 握手成功
                           ▼
              ┌──────────────────────┐
              │ UNAUTHENTICATED      │  ← 等待客户端发送 Token
              └──────┬───────────────┘
                     │ VerifyToken() 成功
                     ▼
           ┌──────────────────┐
           │ AUTHENTICATED    │  ← 正常收发消息
           └────────┬─────────┘
                    │ 关闭帧 / 超时
                    ▼
              ┌──────────────┐
              │   CLOSING    │
              └──────────────┘
```

## 常量说明

| 常量 | 值 | 说明 |
|------|-----|------|
| `kWsMaxFramePayload` | 16 MB | 单帧最大载荷 |
| `kWsHeartbeatIntervalMs` | 30000 | 心跳间隔（30s） |
| `kWsHeartbeatMissMax` | 3 | 连续丢失 pong 次数上限 |
| `kWsHandshakeTimeoutMs` | 5000 | 握手超时 |

## 与 snc_http 的区别

| 特性 | snc_http | snc_websocket |
|------|----------|---------------|
| 协议 | HTTP/1.1 | WebSocket (RFC 6455) |
| 连接模型 | 请求-响应，短连接 | 全双工长连接 |
| 适用场景 | 管理后台、REST API | 游戏实时消息推送 |
| 事件循环 | 内置 select | 内置 select（独立线程） |

> **架构提示**：生产环境中，HTTP 网关和 WebSocket 网关通常是两个独立进程，共用同一份业务逻辑（可通过 Lua 脚本或共享库加载）。

参见 [04-http-server.md](./04-http-server.md)、[05-jwt-auth.md](./05-jwt-auth.md)
