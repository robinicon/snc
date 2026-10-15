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

