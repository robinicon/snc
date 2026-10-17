cmake_minimum_required(VERSION 3.20)
project(snc_core_asio VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Asio standalone: no external dependency, headers are in libs/asio_standalone
file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/core_asio/*.cpp)
add_library(snc_core_asio STATIC ${SOURCES})
target_include_directories(snc_core_asio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libs/asio_standalone/asio>
)
target_link_libraries(snc_core_asio PUBLIC snc_base)
