# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_rpc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── RPC library (STATIC) ───────────────────────────────────
add_library(snc_rpc STATIC
    ${SNC_ROOT_DIR}/src/rpc/rpc_impl.cpp
)

target_include_directories(snc_rpc
    PUBLIC
        $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_rpc PUBLIC snc_core)

