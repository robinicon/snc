# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_db VERSION 1.0.0 LANGUAGES CXX)

# Static library: abstract IDatabase + StubDatabase in-memory implementation
add_library(snc_db STATIC
    src/StubDatabase.cpp
)

target_include_directories(snc_db
    PUBLIC
        $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_db
    PUBLIC snc_base
)
