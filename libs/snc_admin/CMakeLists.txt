# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_admin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(snc_admin STATIC
    src/AdminService.cpp
)

target_include_directories(snc_admin
    PUBLIC
        $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# snc_admin depends on snc_http and snc_auth (JWT middleware only, no business logic)
target_link_libraries(snc_admin PUBLIC snc_http snc_auth snc_base)
