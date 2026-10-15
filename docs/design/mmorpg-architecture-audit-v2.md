# SNC 架构审计报告 & 落地方案

> 日期：2026-10-14
> 范围：snc 框架层 + mmo 传奇层
> 目标：提高复用率、合理分层、高内聚、低耦合

---

## 一、现状概览

```
snc/
├── include/snc/          # 框架头文件（纯头文件库）
│   ├── core/             # 网络核心（EventLoop/Tcp/MsgRouter/EntityHandle）
│   ├── rpc/              # RPC 协议层
│   ├── mmorpg/           # 通用 MMORPG 服务（Account/Gate/Master/World）
│   ├── auth/db/serial/   # 支撑子系统
│   └── ...
├── mmo/                  # 传奇游戏逻辑层
│   ├── include/snc/mmorpg/legend/
│   └── src/
├── libs/                 # 各 CMake 子库
└── examples/             # 5 个独立示例
```

### 当前问题分类

| 级别 | 类型 | 数量 | 说明 |
|------|------|------|------|
| 🔴 P0 | 破坏性缺陷 | 3 | 编译失败/重复实现导致维护陷阱 |
| 🟡 P1 | 架构质量 | 4 | 命名混乱/职责不清 |
| 🟢 P2 | 代码整洁 | 3 | 垃圾文件/示例不可用 |

---

## 二、P0 — 破坏性缺陷（必须修复）

### P0-1：根目录冗余文件 & 垃圾文件

**文件：**
- `c:\Users\10422\Documents\code\snc\PlayerActor.h` — 重复的 player actor 头文件，中文乱码
- `compile_player_errors.txt`, `compile_combat_errors.txt`, `build_log*.txt`, `build_output*.txt`, `preprocess_output.txt`, `build_error_*.txt` — 编译日志垃圾文件
- `docs/architecture-audit.md`（旧版，已被 v2.1-plan 替代）

**影响：** 污染根目录，可能导致 include 路径混乱（MSVC 根目录默认加入 include path）。

**修复方案：** 删除上述所有文件。

---

### P0-2：`rpc::EntityHandle` 死代码 & 非标准 API

**现状：**
- `include/snc/rpc/EntityHandle.h` 定义了 `snc::rpc::EntityHandle`（非模板，绑定到 `rpc::Entity`）
- 实际使用情况：**零外部引用**，所有调用方均使用 `core::EntityHandle<T>`
- `rpc::Entity.h` 中 `using core::EntityHandle;` 已经正确引入了核心版本
- `rpc::EntityHandle::from()` 使用 `.share()` 而非标准 `std::weak_ptr(shared_ptr)` 构造

**修复方案：**
1. 删除 `include/snc/rpc/EntityHandle.h`
2. 从 `rpc_impl.cpp` 中移除对应的 include
3. 确认 `rpc::Entity::GetHandle()` 已正确使用 `core::EntityHandle<Entity>`（已确认）

---

### P0-3：`core::MsgRouterCpp` 命名混乱

**现状：**
- `include/snc/core/MsgRouter.h` 中的类名为 `MsgRouterCpp`（非常规名）
- `include/snc/rpc/MsgRouter.h` 中有同名功能类 `MsgRouter`
- 两者实现完全一致（按 msgId 分发 Handler）
- 名称 `MsgRouterCpp` 暗示"C++ 实现"，但它是唯一实现，命名自相矛盾

**修复方案：**
1. 将 `core/MsgRouter.h` 中的 `MsgRouterCpp` 重命名为 `MsgRouter`
2. 删除 `rpc/MsgRouter.h`（rpc 层复用 core 版本）
3. 更新 `rpc.h` 和 `rpc_impl.cpp` 中的 include 路径
4. 更新教程文档引用

---

## 三、P1 — 架构质量问题

### P1-1：传奇层 `MapManager` 与框架层 `MapManager` 混淆

**现状：**
- `include/snc/mmorpg/MapManager.h` — 框架通用版（`Broadcast(data, len)`）
- `mmo/include/snc/mmorpg/legend/MapManager.h` — 传奇增强版（`Broadcast(msgId, data, len)`）
- 两者都在 `snc::mmorpg` 命名空间下，但传奇版在 `snc::mmorpg::legend` 子命名空间
- 传奇版的 `MapManager` 已经覆盖了框架版的所有功能并扩展了传奇特性

**问题分析：**
框架层 `MapManager` 的设计假设是"纯网络广播"，而传奇层需要"带消息 ID 的路由广播"。两者职责确实不同，但命名相同容易误导。

**修复方案（二选一）：**

**方案 A（推荐）：** 将框架层 `MapManager` 升级为泛型版本，支持 `msgId` 参数，传奇层不再需要独立实现。
- 优点：消除重复，统一维护
- 缺点：框架层 API 变化

**方案 B：** 将传奇层 `MapManager` 重命名为 `LegendMapManager`，明确标识专属用途。
- 优点：无 breaking change
- 缺点：命名更长

**推荐方案 A**，因为传奇层的扩展（safe_zone, dungeon, bg_name）应当作为可选参数，而不是独立类。

---

### P1-2：`example_04_gate.cpp` 损坏且阻塞编译

**现状：**
- 注释标注"暂不可编译"，但 CMakeLists 仍将其加入构建
- `gateSvc.OnPlayerLogin(c, userId)` 调用不存在的方法
- GateService 的实际 API 是 `Start(host, port)`，内部处理连接生命周期

**修复方案：**
重写 `example_04_gate.cpp`，使用正确的 GateService API：
```cpp
GateService gateSvc(registry, jwtCfg);
gateSvc.Start("0.0.0.0", 7777);
// 连接生命周期由 GateService 内部管理
```

---

### P1-3：JWT 密钥硬编码传播

**现状：**
- `examples/example_01_account.cpp` 和 `example_06_mmorpg.cpp` 均硬编码密钥
- 虽然添加了警告注释，但容易被直接复制

**修复方案：**
添加一个 `SNC_JWT_SECRET` 环境变量读取，无环境变量时使用警告+默认值：
```cpp
std::string secret = []() {
    const char* env = std::getenv("SNC_JWT_SECRET");
    return (env && env[0]) ? env : "snc-dev-jwt-secret-key-must-be-at-least-32-bytes!";
}();
if (!std::getenv("SNC_JWT_SECRET")) {
    std::cerr << "[WARN] Using default JWT secret. Set SNC_JWT_SECRET in production.\n";
}
```

---

## 四、P2 — 代码整洁

### P2-1：目录结构优化建议

当前 `libs/` 和 `mmo/` 是平行关系，但 `mmo` 实际依赖 `snc_mmorpg`（libs 子库）。

**建议分层：**
```
snc/
├── framework/          # 原 include/snc/ + src/ + libs/snc_*
│   ├── core/           # 网络核心
│   ├── rpc/            # RPC 协议
│   ├── mmorpg/         # 通用 MMORPG 服务
│   └── ...
└── games/              # 游戏逻辑层（原 mmo/）
    └── legend/         # 传奇游戏逻辑
```

**影响：** 较大重构，建议作为长期目标（P3）。

---

### P2-2：序列化 stub 完善或标记

`PbSerializer` 和 `FbsSerializer` 均为 stub，缺少完整实现。

**修复方案：** 在头文件顶部添加明确注释标记实验状态：
```cpp
/// @warning ProtoSerializer 和 FlatBuffersSerializer 当前为实验性 stub。
///          生产环境请使用 snc::core::Msg 二进制协议。
```

---

### P2-3：日志系统补充 trace_id

当前日志无跨服追踪能力。建议为 `Log.h` 添加可选的 trace_id 字段：
```cpp
void SetTraceId(int64_t id);
int64_t GetTraceId();  // 返回当前线程的 trace_id（或 0 表示无）
```

这是一个小的加法改动，不影响现有代码。

---

## 五、实施顺序

```
批次 1（P0，当天可完成）：
  ✅ P0-1: 删除根目录垃圾文件
  ✅ P0-2: 删除 rpc::EntityHandle.h 死代码
  ✅ P0-3: 重命名 core::MsgRouterCpp → core::MsgRouter

批次 2（P1，次日内完成）：
  ✅ P1-1: 合并/重命名传奇 MapManager
  ✅ P1-2: 重写 example_04_gate.cpp
  ✅ P1-3: JWT 密钥环境变量化

批次 3（P2，按需）：
  ✅ P2-1: 目录结构重构（大型，单独 PR）
  ✅ P2-2: 序列化 stub 标记
  ✅ P2-3: 日志 trace_id 支持
```

---

## 六、验证方式

每个批次完成后：
1. `cmake --preset=normal -DBUILD_EXAMPLES=ON && cmake --build --config Release` 全量编译通过
2. `ctest --output-on-failure` 所有测试通过
3. `example_01_account` ~ `example_06_mmorpg` 启动正常
4. 无新增编译器 warning
