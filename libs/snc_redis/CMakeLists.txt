cmake_minimum_required(VERSION 3.20)
project(snc_redis VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Optional hiredis dependency ────────────────────────────────────
option(SNC_REDIS_CLIENT "Enable real hiredis Redis client" OFF)

add_library(snc_redis STATIC
    src/RedisClient.cpp
)

target_include_directories(snc_redis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/snc_redis/src
)

target_link_libraries(snc_redis PUBLIC snc_base)

if(SNC_REDIS_CLIENT)
    find_package(hiredis REQUIRED)
    target_link_libraries(snc_redis PRIVATE hiredis::hiredis)
    target_compile_definitions(snc_redis PUBLIC SNC_REDIS_CLIENT)
    message(STATUS "snc_redis: hiredis enabled")
else()
    message(STATUS "snc_redis: stub mode (no hiredis)")
endif()

if(MSVC)
    target_compile_options(snc_redis PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_redis PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_redis PRIVATE -Wall -Wextra -Wpedantic)
endif()
