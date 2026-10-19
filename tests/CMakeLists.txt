# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_mmorpg_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# ── 测试框架：使用内置 doctest（thirdparty/doctest/doctest/doctest.h）──────────
set(DOCTEST_INCLUDE_DIR "${SNC_ROOT_DIR}/thirdparty/doctest")

# ── 测试可执行文件 ─────────────────────────────────────────────────

add_executable(test_master_service
    test_master_service.cpp
)
target_include_directories(test_master_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_master_service PRIVATE
    snc_mmorpg
    snc_core
    snc_rpc
    snc_discovery
    snc_auth
    snc_db
    snc_http
    snc_admin
    snc_base
)

add_executable(test_world_service
    test_world_service.cpp
)
target_include_directories(test_world_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_world_service PRIVATE
    snc_mmorpg
    snc_core
    snc_rpc
    snc_discovery
    snc_base
)

add_executable(test_game_service
    test_game_service.cpp
)
target_include_directories(test_game_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_game_service PRIVATE
    snc_mmorpg
    snc_core
    snc_rpc
    snc_discovery
    snc_auth
    snc_base
)

add_executable(test_account_service
    test_account_service.cpp
)
target_include_directories(test_account_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_account_service PRIVATE
    snc_mmorpg
    snc_core
    snc_auth
    snc_db
    snc_http
    snc_base
)

add_executable(test_gate_service
    test_gate_service.cpp
)
target_include_directories(test_gate_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_gate_service PRIVATE
    snc_mmorpg
    snc_core
    snc_auth
    snc_base
)

# ── 核心单元测试 ───────────────────────────────────────────────────

add_executable(test_crypto
    test_crypto.cpp
)
target_include_directories(test_crypto PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_crypto PRIVATE snc_auth snc_base)

add_executable(test_event_loop
    test_event_loop.cpp
)
target_include_directories(test_event_loop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOCTEST_INCLUDE_DIR}
)
target_link_libraries(test_event_loop PRIVATE snc_core snc_base)

# ── 注册测试 ──────────────────────────────────────────────────────
add_test(NAME master_service      COMMAND test_master_service)
add_test(NAME world_service       COMMAND test_world_service)
add_test(NAME game_service        COMMAND test_game_service)
add_test(NAME account_service     COMMAND test_account_service)
add_test(NAME gate_service        COMMAND test_gate_service)
add_test(NAME crypto              COMMAND test_crypto)
add_test(NAME event_loop          COMMAND test_event_loop)

# ── Windows 链接库 ──────────────────────────────────────────────
if(WIN32)
    foreach(_target
            test_master_service test_world_service test_game_service
            test_account_service test_gate_service
            test_crypto test_event_loop)
        target_link_libraries(${_target} PRIVATE ws2_32)
    endforeach()
endif()
