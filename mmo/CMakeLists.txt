cmake_minimum_required(VERSION 3.20)
project(snc_mmo_legend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Library: snc_mmo_legend ─────────────────────────────────────
add_library(snc_mmo_legend
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
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_mmo_legend PUBLIC
    snc_core
    snc_rpc
    snc_discovery
    snc_auth
    $<$<BOOL:${SNC_ENABLE_FLATBUFFERS}>:snc_serial_fbs>
)

# ── Find Lua（与 snc_rpc_lua 策略一致：未找到时降级为 stub）──────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LUA QUIET lua5.4 lua5.3 lua52 luajit)
endif()

if(NOT LUA_INCLUDE_DIR OR NOT LUA_LIBRARY)
    find_path(LUA_INCLUDE_DIR NAMES lua.h
        PATHS "C:/Lua/include" "C:/Program Files/Lua" "E:/Lua/luajit/include"
        "/usr/include" "/usr/local/include"
    )
    find_library(LUA_LIBRARY NAMES lua lua-5.4 lua-5.3 luajit
        PATHS "C:/Lua/lib" "C:/Program Files/Lua" "E:/Lua/luajit/lib"
        "/usr/lib" "/usr/local/lib"
    )
endif()

if(LUA_INCLUDE_DIR AND LUA_LIBRARY)
    message(STATUS "MMO Legend: Lua found — include=${LUA_INCLUDE_DIR}, lib=${LUA_LIBRARY}")
    target_compile_definitions(snc_mmo_legend PRIVATE SNC_LUA_AVAILABLE)
    target_include_directories(snc_mmo_legend PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(snc_mmo_legend PRIVATE ${LUA_LIBRARY})
    if(MSVC)
        target_compile_definitions(snc_mmo_legend PRIVATE LUA_LIB LUA_CORE)
    endif()
else()
    message(WARNING "MMO Legend: Lua not found. LuaBridge will compile in stub mode.")
    message(STATUS "  To enable Lua support, install Lua 5.4 and set -DLUA_ROOT=/path/to/lua")
endif()
