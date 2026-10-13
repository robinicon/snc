# 08 — Redis 客户端配置

**目标**：了解 `snc_redis` 的两种后端实现（Stub / Hiredis），以及如何在 CMake 中切换。

## 概述

`snc_redis` 提供统一接口 `snc::redis::IClient`，屏蔽后端差异：

| 后端 | 条件 | 说明 |
|------|------|------|
| **Stub**（默认） | `SNC_REDIS_CLIENT=OFF` | 进程内内存存储，零外部依赖 |
| **Hiredis** | `SNC_REDIS_CLIENT=ON` | 真实 Redis 实例，支持 Pub/Sub |

头文件入口：

```cpp
#include <snc/redis/Client.h>
```

## Stub 后端（默认）

不启用 `SNC_REDIS_CLIENT` 时，所有命令在进程内通过 `std::map` 模拟执行。

```cpp
#include <snc/redis/Client.h>

// 自动创建 stub 客户端
auto redis = snc::redis::IClient::Create();

// 连接（stub 不需要真实地址，但接口要求调用）
redis->Connect("127.0.0.1", 6379);

// 字符串操作
redis->Set("player:1001:name", "Alice");
std::string name = redis->Get("player:1001:name");   // "Alice"

// Hash 操作
redis->HSet("player:1001:profile", "gold", "5000");
std::string gold = redis->HGet("player:1001:profile", "gold");  // "5000"
auto fields = redis->HMGet("player:1001:profile", {"gold", "diamond"});

// 自增
int64_t seq = redis->Incr("global:seq");   // 每次 +1
redis->Expire("player:1001:session", 3600); // 1 小时后过期

// 判断 key 是否存在
bool exists = redis->Exists("player:1001:name");

// 删除
redis->Del("player:1001:session");
```

### Stub 限制

- **LPush / RPop / LLen**：返回空结果（无真实队列）
- **ZAdd / ZScore / ZRem / ZRevRangeByScore**：返回空结果
- **Subscribe / Unsubscribe**：始终返回 `false`（不支持 Pub/Sub）
- **数据不持久化**：进程重启后所有数据丢失

## Hiredis 后端

启用 `SNC_REDIS_CLIENT=ON` 后链接真实 hiredis：

```bash
# 需预先安装 hiredis（Linux: apt-get install libhiredis-dev；Windows: vcpkg install hiredis）
cmake -B build -DSNC_REDIS_CLIENT=ON
cmake --build build
```

API 与 stub 完全一致，所有命令转发到真实 Redis 实例，包括 Pub/Sub：

```cpp
auto redis = snc::redis::IClient::Create();
redis->Connect("127.0.0.1", 6379);

// 真实队列操作
redis->LPush("chat:room:1", "Hello!");
std::string msg = redis->RPop("chat:room:1");

// 排行榜（有序集合）
redis->ZAdd("leaderboard", 999.0, "player_A");
redis->ZAdd("leaderboard", 888.0, "player_B");
auto top = redis->ZRevRangeByScore("leaderboard", 999.0, 0, 0, 10);

// Pub/Sub
redis->Subscribe("chat:room:1", [](const std::string& channel, const std::string& body) {
    std::cout << "[" << channel << "] " << body << "\n";
});
```

## 两种后端的统一用法

应用层代码无需关心后端类型，直接通过 `IClient::Create()` 获取客户端：

```cpp
#include <snc/redis/Client.h>

void UseRedis() {
    auto redis = snc::redis::IClient::Create();
    if (!redis->IsConnected()) {
        redis->Connect("127.0.0.1", 6379);
    }

    redis->Set("key", "value", /* ttl */ 60);
    std::string val = redis->Get("key");
    redis->Del("key");

    redis->Disconnect();
}
```

## CMake 配置

```cmake
# 根 CMakeLists.txt 中的开关
option(SNC_REDIS_CLIENT "Enable hiredis Redis backend (OFF = stub)" OFF)
```

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SNC_REDIS_CLIENT` | `OFF` | 关闭 → stub（内存模拟）；开启 → hiredis（真实 Redis） |

## 注意事项

1. **生产环境必须启用 hiredis**：`SNC_REDIS_CLIENT=ON`，否则会话、排行榜等功能无法正常工作
2. **Stub 仅用于开发和单元测试**：零依赖，但无法持久化，队列/排行榜命令返回空
3. **`Connect()` 在任何后端下都应调用**：接口契约要求；stub 忽略参数，hiredis 实际建立 TCP 连接
4. **线程安全**：`IClient` 实例不是线程安全的，跨线程使用请各线程持有独立实例或使用锁
5. **Windows 下 hiredis 编译**：需通过 vcpkg 安装 `hiredis`，并在 CMake 中指定工具链文件：
   ```bash
   cmake -B build -DVCPKG_TOOLCHAIN=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake ^
         -DSNC_REDIS_CLIENT=ON
   ```

参见 [10 - 服务注册与发现](10-service-discovery.md)（Redis 作为服务注册表后端）
