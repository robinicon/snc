cmake_minimum_required(VERSION 3.20)
project(snc_rpc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── RPC library (STATIC) ───────────────────────────────────
add_library(snc_rpc STATIC
    ${CMAKE_SOURCE_DIR}/src/rpc/rpc_impl.cpp
)

target_include_directories(snc_rpc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_rpc PUBLIC snc_core)

if(MSVC)
    target_compile_options(snc_rpc PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_rpc PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_rpc PRIVATE -Wall -Wextra)
endif()

