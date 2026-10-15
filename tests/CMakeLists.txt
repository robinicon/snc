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
target_include_directories(test_master_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
target_include_directories(test_world_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
target_include_directories(test_game_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
target_include_directories(test_account_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
target_include_directories(test_gate_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
target_include_directories(test_crypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_crypto PRIVATE snc_auth snc_base)

add_executable(test_event_loop
    test_event_loop.cpp
)
target_include_directories(test_event_loop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_event_loop PRIVATE snc_core snc_base)

# ── 注册测试 ──────────────────────────────────────────────────────
add_test(NAME master_service      COMMAND test_master_service)
add_test(NAME world_service       COMMAND test_world_service)
add_test(NAME game_service        COMMAND test_game_service)
add_test(NAME account_service     COMMAND test_account_service)
add_test(NAME gate_service        COMMAND test_gate_service)
add_test(NAME crypto              COMMAND test_crypto)
add_test(NAME event_loop          COMMAND test_event_loop)

# ── Legend MMO 子模块单元测试（可选，需 SNC_BUILD_MMO_LEGEND=ON）─────────
if(SNC_BUILD_MMO_LEGEND AND TARGET snc_mmo_legend)
    add_executable(test_legend_player_actor
        ../mmo/tests/main.cpp
        ../mmo/tests/test_player_actor.cpp
    )
    target_include_directories(test_legend_player_actor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/mmo/include
    )
    target_link_libraries(test_legend_player_actor PRIVATE snc_mmo_legend snc_base)

    add_executable(test_legend_monster
        ../mmo/tests/main.cpp
        ../mmo/tests/test_monster.cpp
    )
    target_include_directories(test_legend_monster PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/mmo/include
    )
    target_link_libraries(test_legend_monster PRIVATE snc_mmo_legend snc_base)

    add_executable(test_legend_combat
        ../mmo/tests/main.cpp
        ../mmo/tests/test_combat.cpp
    )
    target_include_directories(test_legend_combat PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/mmo/include
    )
    target_link_libraries(test_legend_combat PRIVATE snc_mmo_legend snc_base)

    add_executable(test_legend_drop
        ../mmo/tests/main.cpp
        ../mmo/tests/test_drop.cpp
    )
    target_include_directories(test_legend_drop PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/mmo/include
    )
    target_link_libraries(test_legend_drop PRIVATE snc_mmo_legend snc_base)

    add_executable(test_legend_guild
        ../mmo/tests/main.cpp
        ../mmo/tests/test_guild.cpp
    )
    target_include_directories(test_legend_guild PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/mmo/include
    )
    target_link_libraries(test_legend_guild PRIVATE snc_mmo_legend snc_base)

    add_executable(test_legend_world_channel
        ../mmo/tests/main.cpp
        ../mmo/tests/test_world_channel.cpp
    )
    target_include_directories(test_legend_world_channel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/mmo/include
    )
    target_link_libraries(test_legend_world_channel PRIVATE snc_mmo_legend snc_base)

    add_test(NAME legend_player_actor COMMAND test_legend_player_actor)
    add_test(NAME legend_monster      COMMAND test_legend_monster)
    add_test(NAME legend_combat       COMMAND test_legend_combat)
    add_test(NAME legend_drop         COMMAND test_legend_drop)
    add_test(NAME legend_guild        COMMAND test_legend_guild)
    add_test(NAME legend_world_channel COMMAND test_legend_world_channel)

    if(WIN32)
        foreach(_target
                test_legend_player_actor test_legend_monster
                test_legend_combat test_legend_drop
                test_legend_guild test_legend_world_channel)
            target_link_libraries(${_target} PRIVATE ws2_32)
        endforeach()
    endif()
endif()

# ── Windows 链接库 ──────────────────────────────────────────────
if(WIN32)
    foreach(_target
            test_master_service test_world_service test_game_service
            test_account_service test_gate_service
            test_crypto test_event_loop)
        target_link_libraries(${_target} PRIVATE ws2_32)
    endforeach()
endif()
