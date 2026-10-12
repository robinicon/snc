cmake_minimum_required(VERSION 3.20)
project(snc_discovery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Library (header-only, but we create a static lib for linking consistency) ──
add_library(snc_discovery STATIC
    ${CMAKE_SOURCE_DIR}/src/discovery/ServiceRegistry.cpp
)

target_include_directories(snc_discovery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_discovery PUBLIC snc_base)

if(MSVC)
    target_compile_options(snc_discovery PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_discovery PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_discovery PRIVATE -Wall -Wextra)
endif()

