cmake_minimum_required(VERSION 3.20)
project(snc_base VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Library target ──────────────────────────────────────────────
add_library(snc_base STATIC
    ${CMAKE_SOURCE_DIR}/src/base/Log.cpp
)

target_include_directories(snc_base
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(snc_base PUBLIC SNC_BUILD_SNC_BASE_DLL)

# Platform-specific settings
if(MSVC)
    target_compile_options(snc_base PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_base PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_base PRIVATE -Wall -Wextra -Wpedantic)
endif()

