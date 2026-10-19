# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
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
target_include_directories(example_01_account PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

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
target_include_directories(example_02_master PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

# ── 示例 03 — Gate 服（TCP 接入 + JWT 验签 + 消息路由）────────────
add_executable(example_03_gate
    example_04_gate.cpp
)
target_link_libraries(example_03_gate PRIVATE
    snc_rpc
    snc_discovery
    snc_auth
    snc_base
    snc_mmorpg
)
target_include_directories(example_03_gate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

# ── 示例 04 — World 服（跨服场景管理 + 状态机）──────────────────
add_executable(example_04_world
    example_05_world.cpp
)
target_link_libraries(example_04_world PRIVATE
    snc_rpc
    snc_discovery
    snc_base
    snc_mmorpg
)

# ── 示例 05 — 完整 MMORPG 多进程启动演示────────────────────────
add_executable(example_05_mmorpg
    example_06_mmorpg.cpp
)
target_link_libraries(example_05_mmorpg PRIVATE
    snc_http
    snc_auth
    snc_db
    snc_rpc
    snc_discovery
    snc_admin
    snc_base
    snc_mmorpg
)
target_include_directories(example_05_mmorpg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

# ── Windows 链接库 ──────────────────────────────────────────────
if(WIN32)
    foreach(_target
            example_01_account example_02_master
            example_03_gate example_04_world example_05_mmorpg)
        target_link_libraries(${_target} PRIVATE ws2_32)
    endforeach()
endif()
