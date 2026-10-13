cmake_minimum_required(VERSION 3.20)
project(snc_rpc_lua VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find Lua ─────────────────────────────────────────────────
# Supports Lua 5.4, 5.3, 5.2, and LuaJIT.
#
# To override automatic detection:
#   cmake -DLUA_ROOT=/path/to/lua -B build
#
# On Windows, set LUA_ROOT to the Lua installation root (e.g. C:/Lua).
# On Linux/macOS, the default search paths are sufficient for most installations.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LUA QUIET lua5.4 lua5.3 lua52 luajit)
endif()

if(NOT LUA_INCLUDE_DIR OR NOT LUA_LIBRARY)
    find_path(LUA_INCLUDE_DIR
        NAMES lua.h
        PATHS
            /usr/include
            /usr/local/include
            /usr/local/opt/lua/include
            ${LUA_INCLUDE_DIRS}
    )
endif()

if(NOT LUA_LIBRARY)
    find_library(LUA_LIBRARY
        NAMES lua lua-5.4 lua-5.3 lua54 lua53 luajit
        PATHS
            /usr/lib
            /usr/local/lib
            /usr/local/opt/lua/lib
            ${LUA_LIBRARY_DIRS}
    )
endif()

# Fallback: common Windows paths
if(NOT LUA_INCLUDE_DIR OR NOT LUA_LIBRARY)
    find_path(LUA_INCLUDE_DIR NAMES lua.h
        PATHS "C:/Lua/include" "C:/Program Files/Lua" "E:/Lua/luajit/include" "D:/Lua/include"
    )
    find_library(LUA_LIBRARY NAMES lua lua-5.4 lua-5.3 luajit
        PATHS "C:/Lua/lib" "C:/Program Files/Lua" "E:/Lua/luajit/lib" "D:/Lua/lib"
    )
endif()

if(LUA_LIBRARY AND LUA_INCLUDE_DIR)
    message(STATUS "snc_rpc_lua: Lua found — include=${LUA_INCLUDE_DIR}, lib=${LUA_LIBRARY}")
    set(SNC_LUA_AVAILABLE TRUE)
    add_library(snc_rpc_lua STATIC
        src/rpc/lua/LuaBinding.cpp
        src/rpc/lua/LuaRouterApi.cpp
        src/rpc/lua/LuaRpcApi.cpp
    )
    target_include_directories(snc_rpc_lua
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${LUA_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${LUA_INCLUDE_DIR}
    )
    target_link_libraries(snc_rpc_lua PUBLIC snc_rpc)
    target_compile_definitions(snc_rpc_lua PUBLIC SNC_LUA_AVAILABLE)
    target_compile_definitions(snc_rpc_lua PRIVATE
        LUA_LIB
        LUA_CORE
    )
    if(MSVC)
        target_compile_options(snc_rpc_lua PRIVATE /W4 /WX- /utf-8)
        target_compile_definitions(snc_rpc_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(snc_rpc_lua PRIVATE -Wall -Wextra)
    endif()
    # Link the Lua library
    target_link_libraries(snc_rpc_lua PRIVATE ${LUA_LIBRARY})
    if(LUA_LIBRARY MATCHES "luajit")
        target_compile_definitions(snc_rpc_lua PUBLIC SNC_LUA_JIT)
    endif()
else()
    message(WARNING "snc_rpc_lua: Lua not found. Building stub-only library.")
    message(STATUS "  To enable Lua support, set -DLUA_ROOT=/path/to/lua")
    add_library(snc_rpc_lua STATIC
        src/rpc/lua/LuaBinding.cpp
        src/rpc/lua/LuaRouterApi.cpp
        src/rpc/lua/LuaRpcApi.cpp
    )
    target_include_directories(snc_rpc_lua
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(snc_rpc_lua PUBLIC snc_rpc)
    if(MSVC)
        target_compile_options(snc_rpc_lua PRIVATE /W4 /WX- /utf-8)
        target_compile_definitions(snc_rpc_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(snc_rpc_lua PRIVATE -Wall -Wextra)
    endif()
endif()

