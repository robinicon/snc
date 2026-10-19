# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_core_asio VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Asio standalone: prefer submodule at root/asio_standalone; falls back to vcpkg asio
if(NOT DEFINED _ASIO_INCLUDE_DIR)
    set(_ASIO_INCLUDE_DIR "")
endif()
file(GLOB SOURCES ${SNC_ROOT_DIR}/src/core_asio/*.cpp)
add_library(snc_core_asio STATIC ${SOURCES})
target_include_directories(snc_core_asio
    PUBLIC
        $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
        $<BUILD_INTERFACE:${_ASIO_INCLUDE_DIR}>
)
target_link_libraries(snc_core_asio PUBLIC snc_base)
