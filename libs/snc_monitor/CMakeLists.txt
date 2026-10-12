cmake_minimum_required(VERSION 3.20)
project(snc_monitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(snc_monitor STATIC
    ${CMAKE_SOURCE_DIR}/src/monitor/Monitor.cpp
)

target_include_directories(snc_monitor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_monitor PUBLIC snc_base snc_core)

if(MSVC)
    target_compile_options(snc_monitor PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_monitor PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_monitor PRIVATE -Wall -Wextra)
endif()

