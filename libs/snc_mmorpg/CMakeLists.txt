cmake_minimum_required(VERSION 3.20)
project(snc_mmorpg VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(snc_mmorpg
    src/MasterService.cpp
    src/GameService.cpp
    src/WorldService.cpp
    src/AccountService.cpp
    src/GateService.cpp
    src/MapManager.cpp
    # ── 传奇层源码 ──────────────────────────────────────────────
    ${CMAKE_SOURCE_DIR}/mmo/src/GameService.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/PlayerActor.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/Scene.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/SceneMgr.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/EntityMgr.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/NPCManager.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/MonsterManager.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/CombatSystem.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/DropManager.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/ItemMgr.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/GuildManager.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/WorldChannel.cpp
    ${CMAKE_SOURCE_DIR}/mmo/src/LuaBridge.cpp
)

target_include_directories(snc_mmorpg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/mmo/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_mmorpg PUBLIC
    snc_core
    snc_rpc
    snc_discovery
    snc_auth
    snc_db
    snc_http
)
