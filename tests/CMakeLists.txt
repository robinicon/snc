cmake_minimum_required(VERSION 3.20)
project(snc_mmorpg_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# ── 测试框架（自包含头文件）───────────────────────────────────────
# tests/test_framework.h 提供 Catch2-like 接口，无需外部依赖

# ── 测试可执行文件 ─────────────────────────────────────────────────

add_executable(test_master_service
    test_master_service.cpp
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
target_link_libraries(test_gate_service PRIVATE
    snc_mmorpg
    snc_core
    snc_auth
    snc_base
)

# ── MSVC 编译器选项 ─────────────────────────────────────────────
if(MSVC)
    foreach(_target
            test_master_service test_world_service test_game_service
            test_account_service test_gate_service)
        target_compile_options(${_target} PRIVATE /W4 /WX- /utf-8)
        target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endforeach()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(_target
            test_master_service test_world_service test_game_service
            test_account_service test_gate_service)
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

# ── 注册测试 ──────────────────────────────────────────────────────
add_test(NAME master_service      COMMAND test_master_service)
add_test(NAME world_service       COMMAND test_world_service)
add_test(NAME game_service        COMMAND test_game_service)
add_test(NAME account_service     COMMAND test_account_service)
add_test(NAME gate_service        COMMAND test_gate_service)

# ── Windows 链接库 ──────────────────────────────────────────────
if(WIN32)
    foreach(_target
            test_master_service test_world_service test_game_service
            test_account_service test_gate_service)
        target_link_libraries(${_target} PRIVATE ws2_32)
    endforeach()
endif()
