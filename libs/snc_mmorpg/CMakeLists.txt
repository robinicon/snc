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
)

target_include_directories(snc_mmorpg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_mmorpg PUBLIC
    snc_core
    snc_rpc
    snc_discovery
    snc_auth
    snc_http
    snc_db
    snc_admin
)

if(MSVC)
    target_compile_options(snc_mmorpg PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_mmorpg PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_mmorpg PRIVATE -Wall -Wextra -Wpedantic)
endif()
