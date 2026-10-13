cmake_minimum_required(VERSION 3.20)
project(snc_examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 示例 01 — 账号服（HTTP 注册/登录 + JWT）──────────────────────
add_executable(example_01_account
    example_01_account.cpp
)
target_link_libraries(example_01_account PRIVATE
    snc_http
    snc_auth
    snc_db
    snc_admin
    snc_base
)

# ── 示例 02 — Master 服（服务状态管理 + GM 命令分发）──────────────
add_executable(example_02_master
    example_02_master.cpp
)
target_link_libraries(example_02_master PRIVATE
    snc_rpc
    snc_discovery
    snc_admin
    snc_auth
    snc_http
    snc_base
    snc_mmorpg
)

# ── 示例 03 — Game 服（游戏逻辑 + Actor + 地图 + 优雅停机）──────
add_executable(example_03_game
    example_03_game.cpp
)
target_link_libraries(example_03_game PRIVATE
    snc_rpc
    snc_discovery
    snc_auth
    snc_base
    snc_mmorpg
)

# ── 示例 04 — Gate 服（TCP 接入 + JWT 本地验签 + 消息路由）──────
add_executable(example_04_gate
    example_04_gate.cpp
)
target_link_libraries(example_04_gate PRIVATE
    snc_core
    snc_rpc
    snc_discovery
    snc_auth
    snc_base
    snc_mmorpg
)

# ── 示例 05 — World 服（跨服场景管理 + 状态机）──────────────────
add_executable(example_05_world
    example_05_world.cpp
)
target_link_libraries(example_05_world PRIVATE
    snc_rpc
    snc_discovery
    snc_base
    snc_mmorpg
)

# ── 示例 06 — 完整 MMORPG 多进程启动演示────────────────────────
add_executable(example_06_mmorpg
    example_06_mmorpg.cpp
)
target_link_libraries(example_06_mmorpg PRIVATE
    snc_http
    snc_auth
    snc_db
    snc_rpc
    snc_discovery
    snc_admin
    snc_base
    snc_mmorpg
)

# ── MSVC 编译器选项 ─────────────────────────────────────────────
if(MSVC)
    foreach(_target
            example_01_account example_02_master example_03_game
            example_04_gate example_05_world example_06_mmorpg)
        target_compile_options(${_target} PRIVATE /W4 /WX- /utf-8)
        target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endforeach()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(_target
            example_01_account example_02_master example_03_game
            example_04_gate example_05_world example_06_mmorpg)
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

# ── Windows 链接库 ──────────────────────────────────────────────
if(WIN32)
    foreach(_target
            example_01_account example_02_master example_03_game
            example_04_gate example_05_world example_06_mmorpg)
        target_link_libraries(${_target} PRIVATE ws2_32)
    endforeach()
endif()
