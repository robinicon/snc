# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_gateway VERSION 1.0.0 LANGUAGES CXX)

# Static library: ClusterGateway with CircuitBreaker (moved from snc_circuit)
add_library(snc_gateway
    src/ClusterGateway.cpp
)

target_include_directories(snc_gateway
    PUBLIC
        $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_gateway
    PRIVATE snc_discovery snc_rpc snc_base
)
