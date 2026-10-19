# SNC 开源拆分方案

> 评估日期：2026-10-19
> 许可证：MIT（当前 LICENSE 文件兼容）

---

## 一、合理性评估

### 结论：✅ 可行，且架构分层已为此做好准备

| 维度 | 评估 |
|------|------|
| 依赖边界 | 清晰。`snc_mmo_legend` 只依赖 4 个 snc 库 + 可选 Lua + 可选 FlatBuffers |
| 头文件耦合 | 中度。所有库共用 `include/snc/` 单一头文件目录，拆分后需重新规划公共头文件路径 |
| 外部依赖 | 干净。仅 `asio_standalone`（内置）、`doctest`（测试用）、可选 Lua/protobuf/flatbuffers/hiredis |
| 许可证 | MIT，无合规障碍 |
| CMake 设计 | 良好。各库均为独立 `project()` target，可通过 `find_package(snc)` 消费 |
| 构建脚本 | 现有 vcpkg.json 已描述所有可选依赖 |

**主要风险点**：
1. `include/` 是扁平的公共头文件目录——拆分后框架仓库需保留此结构，mmo 仓库引用时需通过 `target_link_libraries` 透传
2. `CMAKE_SOURCE_DIR` 引用出现在所有库的 CMakeLists 中（用于指向 `include/` 和 `src/`）
3. `asio_standalone` 当前以内置目录形式存在，需改为 submodule 或 vcpkg

---

## 二、目标仓库结构

### 仓库一：`snc-framework`（核心网络框架）

```
snc-framework/
├── CMakeLists.txt               # 根构建，导出 sncTargets
├── LICENSE
├── vcpkg.json
├── README.md                    # 英文版
├── cmake/
│   └── sncConfig.cmake.in
├── include/snc/                 # 公共头文件（全部保留）
│   ├── base.h / core.h / rpc.h / snc.h
│   ├── base/     (Common.h, Log.h, LogLevel.h, ByteOrder.h, DataStruct.h, ThreadTools.h, Time.h, Protocol.h)
│   ├── core/     (EventLoop.h, IEventLoop.h, TcpServer.h, TcpConnection.h, ...)
│   ├── core_asio/
│   ├── rpc/      (+ lua/ subfolder)
│   ├── serial/
│   ├── discovery/
│   ├── gateway/
│   ├── db/
│   ├── redis/
│   ├── http/
│   ├── auth/
│   ├── admin/
│   ├── ws/
│   ├── monitor/
│   └── mmorpg/               # ← 保留：通用 MMORPG 服务层头文件
│       ├── AccountService.h
│       ├── GameService.h
│       ├── GateService.h
│       ├── MasterService.h
│       ├── WorldService.h
│       ├── MapManager.h
│       ├── PlayerPos.h
│       ├── ServiceContext.h
│       └── legend/PetInstance.h
├── src/
│   ├── core/        (EventLoop.cpp)
│   ├── core_asio/   (Asio*.cpp)
│   ├── rpc/         (rpc_impl.cpp, lua/*.cpp)
│   ├── serial/      (fbs/, pb/)
│   ├── discovery/   (ServiceRegistry.cpp)
│   ├── monitor/     (Monitor.cpp)
│   ├── db/          (StubDatabase.cpp)
│   ├── http/        (HttpServer.cpp)
│   ├── auth/        (AuthService.cpp)
│   ├── admin/       (AdminService.cpp)
│   └── gateway/     (ClusterGateway.cpp)
├── libs/
│   ├── snc_base/        → 保留
│   ├── snc_core/        → 保留
│   ├── snc_core_asio/   → 保留（asio_standalone 改 submodule）
│   ├── snc_rpc/         → 保留
│   ├── snc_rpc_lua/     → 保留
│   ├── snc_serial_pb/   → 保留
│   ├── snc_serial_fbs/  → 保留
│   ├── snc_discovery/   → 保留
│   ├── snc_monitor/     → 保留
│   ├── snc_gateway/     → 保留
│   ├── snc_db/          → 保留
│   ├── snc_http/        → 保留
│   ├── snc_auth/        → 保留
│   ├── snc_admin/       → 保留
│   ├── snc_redis/       → 保留
│   └── snc_mmorpg/      → 保留（通用 MMORPG 服务层）
├── asio_standalone/     # ← 改为 git submodule（原 libs/asio_standalone/）
│   └── asio/
├── tests/               # 保留核心测试（不含 legend 测试）
├── examples/            # 保留 example_01~02, 04~06（不含依赖 mmo 的 example_03）
└── docs/
```

### 仓库二：`snc-mmo-legend`（传奇 MMO 游戏逻辑）

```
snc-mmo-legend/
├── CMakeLists.txt               # find_package(snc REQUIRED)
├── LICENSE
├── README.md                    # 中文版，展示如何基于 snc-framework 构建游戏服务器
├── include/snc/mmorpg/legend/  # 保留（Entity.h, Scene.h, CombatSystem.h 等）
├── src/                         # 保留 14 个 .cpp 源文件
├── tests/                       # 保留 7 个 legend 测试文件
├── CMakePresets.json            # 推荐构建配置
└── vcpkg.json
```

---

## 三、CMake 改造清单

### 3.1 根 CMakeLists.txt（snc-framework）

**变更点**：引入 `SNC_ROOT_DIR` 变量，消除所有 `${CMAKE_SOURCE_DIR}/include` 引用。

```cmake
# 在 project() 之后立即设置
set(SNC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# 将 asio_standalone 改为 submodule 管理
# 用户需先执行：git submodule update --init --recursive
```

**根 CMakeLists.txt 改动对比**：

```diff
  project(snc VERSION 1.0.1 DESCRIPTION "..." LANGUAGES CXX)

+ set(SNC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
+
+ # asio_standalone: 通过 submodule 或 vcpkg (asio) 提供
+ if(NOT TARGET asio_standalone::asio)
+   if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asio_standalone")
+     # 内置 submodule 模式
+     set(_ASIO_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/asio_standalone")
+   elseif(vcpkg_FOUND)
+     find_package(asio REQUIRED)
+     set(_ASIO_INCLUDE "")
+   endif()
+ endif()
```

---

### 3.2 各库 CMakeLists.txt 路径修正

**核心原则**：所有 `${CMAKE_SOURCE_DIR}/include` → `${SNC_ROOT_DIR}/include`；所有 `${CMAKE_SOURCE_DIR}/src/...` → 使用相对路径。

#### snc_base（无需改路径，已是 INTERFACE）

```diff
  target_include_directories(snc_base
      INTERFACE
-         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
+         $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
          $<INSTALL_INTERFACE:include>
  )
```

#### snc_core

```diff
  add_library(snc_core STATIC
-     ${CMAKE_SOURCE_DIR}/src/core/EventLoop.cpp
+     src/core/EventLoop.cpp
  )
  target_include_directories(snc_core
      PUBLIC
-         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
+         $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
          $<INSTALL_INTERFACE:include>
  )
```

#### snc_core_asio

```diff
  add_library(snc_core_asio STATIC
-     file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/core_asio/*.cpp)
+     file(GLOB SOURCES src/core_asio/*.cpp)
  )
  target_include_directories(snc_core_asio
      PUBLIC
-         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
+         $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
-         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libs/asio_standalone/asio>
+         $<BUILD_INTERFACE:${SNC_ROOT_DIR}/asio_standalone>
  )
```

#### snc_discovery / snc_monitor / snc_db / snc_rpc / snc_rpc_lua 等

同理，将 `${CMAKE_SOURCE_DIR}/src/...` 改为相对路径，`${CMAKE_SOURCE_DIR}/include` 改为 `${SNC_ROOT_DIR}/include`。

#### snc_mmorpg（保留在框架仓库中）

```diff
  target_include_directories(snc_mmorpg
      PUBLIC
-         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
+         $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
          $<INSTALL_INTERFACE:include>
  )
```

---

### 3.3 测试 CMakeLists.txt 改造

**移除 legend 测试**（移到 snc-mmo-legend 仓库），保留核心测试：

```diff
- # Legend MMO tests (lines 103–180) — 整个 if(SNC_BUILD_MMO_LEGEND) 块删除
+ # 仅保留 test_master_service ~ test_event_loop
```

**doctest 改为 FetchContent**：

```diff
- # thirdparty/doctest/doctest/doctest.h 直接引用
+ FetchContent_Declare(
+     doctest
+     GIT_REPOSITORY https://github.com/doctest/doctest.git
+     GIT_TAG        v2.4.11
+ )
+ FetchContent_MakeAvailable(doctest)
```

同时删除 `thirdparty/doctest/` 目录。

---

### 3.4 示例 CMakeLists.txt 改造

**移除 example_03_game**（依赖 mmo）：

```diff
- # example_03_game: 依赖 snc_mmo_legend — 移至 snc-mmo-legend 仓库作为示例
```

---

## 四、snc-mmo-legend 仓库设计

### 4.1 CMakeLists.txt（snc-mmo-legend 根）

```cmake
cmake_minimum_required(VERSION 3.20)
project(snc_mmo_legend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 依赖 snc-framework ─────────────────────────────────────────
find_package(snc REQUIRED)

# ── 库目标 ─────────────────────────────────────────────────────
add_library(snc_mmo_legend STATIC
    src/PlayerActor.cpp
    src/NPCManager.cpp
    src/MonsterManager.cpp
    src/PetInstance.cpp
    src/CombatSystem.cpp
    src/DropManager.cpp
    src/GuildManager.cpp
    src/WorldChannel.cpp
    src/GameService.cpp
    src/EntityMgr.cpp
    src/Scene.cpp
    src/SceneMgr.cpp
    src/ItemMgr.cpp
    src/LuaBridge.cpp
)

target_include_directories(snc_mmo_legend
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_mmo_legend PUBLIC
    snc::core
    snc::rpc
    snc::discovery
    snc::auth
    $<$<BOOL:${SNC_ENABLE_FLATBUFFERS}>:snc::serial_fbs>
)

# ── Lua（可选，策略与框架内一致）────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LUA QUIET lua5.4 lua5.3 lua52 luajit)
endif()
if(NOT LUA_INCLUDE_DIR OR NOT LUA_LIBRARY)
    find_path(LUA_INCLUDE_DIR NAMES lua.h
        PATHS "C:/Lua/include" "/usr/include" "/usr/local/include")
    find_library(LUA_LIBRARY NAMES lua lua-5.4 lua-5.3 luajit
        PATHS "C:/Lua/lib" "/usr/lib" "/usr/local/lib")
endif()
if(LUA_INCLUDE_DIR AND LUA_LIBRARY)
    target_compile_definitions(snc_mmo_legend PRIVATE SNC_LUA_AVAILABLE)
    target_include_directories(snc_mmo_legend PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(snc_mmo_legend PRIVATE ${LUA_LIBRARY})
    if(MSVC)
        target_compile_definitions(snc_mmo_legend PRIVATE LUA_LIB LUA_CORE)
    endif()
else()
    message(WARNING "Lua not found. LuaBridge compiles in stub mode.")
endif()

# ── 测试 ─────────────────────────────────────────────────────────
option(SNC_MMO_BUILD_TESTS "Build legend MMO tests" ON)
if(SNC_MMO_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)

    macro(add_legend_test NAME SRC)
        add_executable(test_legend_${NAME} ${SRC} tests/main.cpp)
        target_include_directories(test_legend_${NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${doctest_SOURCE_DIR}/scripts/)
        target_link_libraries(test_legend_${Name} PRIVATE snc_mmo_legend doctest::doctest)
        if(WIN32)
            target_link_libraries(test_legend_${NAME} PRIVATE ws2_32)
        endif()
        add_test(NAME legend_${NAME} COMMAND test_legend_${NAME})
    endmacro()

    add_legend_test(player_actor tests/test_player_actor.cpp)
    add_legend_test(monster     tests/test_monster.cpp)
    add_legend_test(combat      tests/test_combat.cpp)
    add_legend_test(drop        tests/test_drop.cpp)
    add_legend_test(guild       tests/test_guild.cpp)
    add_legend_test(world_channel tests/test_world_channel.cpp)
endif()
```

---

## 五、关键改造操作清单

### Phase 1：snc-framework 仓库（约 2h 工作量）

| # | 操作 | 文件 |
|---|------|------|
| 1 | 新增 `set(SNC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")` | `CMakeLists.txt` |
| 2 | 将 `libs/asio_standalone/` 改为 git submodule | 根目录 |
| 3 | 替换 `thirdparty/doctest/` 为 FetchContent | `tests/CMakeLists.txt` |
| 4 | 将所有 libs/\*/CMakeLists.txt 中的 `${CMAKE_SOURCE_DIR}/include` 改为 `${SNC_ROOT_DIR}/include` | 16 个文件 |
| 5 | 将所有 libs/\*/CMakeLists.txt 中的 `${CMAKE_SOURCE_DIR}/src/...` 改为相对路径 | 7 个文件 |
| 6 | 删除 `tests/CMakeLists.txt` 中的 legend 测试块（行 103-180） | `tests/CMakeLists.txt` |
| 7 | 删除 `examples/CMakeLists.txt` 中的 `example_03_game` | `examples/CMakeLists.txt` |
| 8 | 更新 `vcpkg.json`，移除 mmo 专属说明，增加 asio 说明 | `vcpkg.json` |
| 9 | 验证：clean build 全量编译通过 | — |

### Phase 2：snc-mmo-legend 仓库（约 1h 工作量）

| # | 操作 |
|---|------|
| 1 | 新建仓库，复制 `mmo/` 目录内容 |
| 2 | 编写新的根 CMakeLists.txt（使用 `find_package(snc)`） |
| 3 | 复制 `mmo/tests/` 中的 7 个测试文件 |
| 4 | 编写 README（中文版，展示架构和用法） |
| 5 | 添加 `.gitignore`、`CMakePresets.json` |
| 6 | 验证：依赖 snc-framework 后可独立编译和测试 |

### Phase 3：发布准备（约 1h 工作量）

| # | 操作 |
|---|------|
| 1 | snc-framework：英文 README，贡献指南（CONTRIBUTING.md），GitHub Actions CI |
| 2 | snc-mmo-legend：中文 README，架构图，快速开始 |
| 3 | 两个仓库均添加 `CODE_OF_CONDUCT.md` 和 `SECURITY.md` |
| 4 | 在 GitHub 创建两个 repository，推送代码 |
| 5 | 发布首个 tag v1.0.0 |

---

## 六、依赖关系变更图

### 拆分前（单仓库）

```
snc (monorepo)
├── snc_base
├── snc_core ──► snc_base
├── snc_rpc  ──► snc_core
├── snc_mmorpg ──► snc_core + snc_rpc + ...
├── snc_mmo_legend ──► snc_core + snc_rpc + ... + snc_mmorpg(间接)
└── tests/examples ──► 全部
```

### 拆分后（双仓库）

```
snc-framework（独立可安装）          snc-mmo-legend（依赖框架）
├── snc_base                        ├── snc_mmo_legend ──► snc::core
├── snc_core ──► snc_base              ├── snc::rpc
├── snc_rpc ──► snc_core               ├── snc::discovery
├── snc_mmorpg ──► snc_core + ...      ├── snc::auth
├── snc_discovery ──► snc_base         └── [optional] snc::serial_fbs
├── snc_auth ──► snc_base
├── snc_db ──► snc_base
├── snc_http ──► snc_base
└── ...（其余框架库）
```

`snc-mmo-legend` 完全独立于 `snc-framework` 源码，通过 `find_package(snc)` 消费框架。

---

## 七、风险与注意事项

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| `include/snc/mmorpg/legend/PetInstance.h` 在框架中但被 mmo 使用 | 低 | 已在框架中保留；mmo 通过 `find_package` 获取 |
| vcpkg 用户 vs 手动构建用户的体验差异 | 中 | 提供 CMakePresets.json，支持 `--toolchain vcpkg` |
| asio_standalone submodule 初始化 | 低 | README 中明确说明 `git submodule update --init` |
| 历史 git 记录拆分 | 高 | 建议在 GitHub 用 `git filter-repo` 或直接从当前状态建仓库（不继承历史） |
| `snc::` 命名空间前缀（CMake install） | 低 | 框架 CMakeLists 已有 `NAMESPACE snc::` 配置 |

---

## 八、建议的 GitHub 仓库命名

| 仓库 | 建议名称 | 定位 |
|------|---------|------|
| 框架 | `snc-framework` / `snc-network` | C++17 游戏服务器网络框架 |
| MMO | `snc-mmo-legend` | 基于 snc-framework 的传奇 MMO 服务端示例 |

---

*方案制定依据：当前 snc v1.0.1 代码结构分析（2026-10-19）*
