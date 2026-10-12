cmake_minimum_required(VERSION 3.20)
project(snc_core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Core library (STATIC — includes EventLoop.cpp) ──────
add_library(snc_core STATIC
    ${CMAKE_SOURCE_DIR}/src/core/EventLoop.cpp
)

target_include_directories(snc_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_core PUBLIC snc_base)

if(MSVC)
    target_compile_options(snc_core PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_core PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_core PRIVATE -Wall -Wextra)
endif()

