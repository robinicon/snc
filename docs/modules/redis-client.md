# Redis 客户端说明

## 概述

`snc_redis` 提供统一的 Redis 客户端接口 `snc::redis::IClient`。
**默认构建模式为 stub（内存模拟），不会链接 hiredis 库。**

## 两种后端

### Stub 后端（默认）

不启用 `SNC_REDIS_CLIENT` 时，所有 Redis 操作在进程内内存中执行：
- `Set`/`Get`/`Del` — 使用 `std::map` 存储字符串
- `HSet`/`HGet`/`HDel`/`HMGet` — 使用嵌套 map 模拟 hash
- `Incr` — 使用内存 counter 模拟自增
- `LPush`/`RPop`/`LLen` — **stub 返回空结果**（无真实队列）
- `ZAdd`/`ZScore`/`ZRem`/`ZRevRangeByScore` — **stub 返回空结果**（无真实有序集合）
- `Subscribe`/`Unsubscribe` — **stub 始终返回 false**（不支持 Pub/Sub）

Stub 后端适用于开发、测试环境，零外部依赖。

### Hiredis 后端（可选）

启用 `SNC_REDIS_CLIENT=ON` 后链接真实 hiredis 库：
- 所有命令均转发到真实 Redis 实例
- 支持 Pub/Sub（`Subscribe`/`Unsubscribe`）
- 完整实现列表命令和有序集合命令

## 切换后端

```bash
# 默认：stub 模式（无需 hiredis）
cmake -B build

# 启用 hiredis 后端（需先安装 hiredis）
cmake -B build -DSNC_REDIS_CLIENT=ON
```

CMake 配置变量：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `SNC_REDIS_CLIENT` | OFF | 启用 hiredis 后端；关闭时使用内存 stub |

## 代码示例

```cpp
#include "snc/redis/Client.h"

// 创建客户端（自动选择 stub 或 hiredis）
auto redis = snc::redis::IClient::Create();
redis->Connect("127.0.0.1", 6379);

// 所有 API 在两种后端下完全一致
redis->Set("player:1001:name", "Alice");
std::string name = redis->Get("player:1001:name");  // "Alice"

redis->HSet("player:1001:profile", "gold", "5000");
std::string gold = redis->HGet("player:1001:profile", "gold");  // "5000"

int64_t seq = redis->Incr("global:seq");
```

## 注意事项

1. **stub 不支持持久化**：进程重启后所有数据丢失
2. **stub 列表/有序集合命令返回空**：如需在测试中使用队列或排行榜，请启用 hiredis 后端
3. **stub Pub/Sub 不可用**：`Subscribe` 始终返回 false，无消息回调
4. **生产环境必须启用 hiredis**：`SNC_REDIS_CLIENT=ON`
