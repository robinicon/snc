# SPDX-License-Identifier: MIT
# Licensed under the MIT License — see LICENSE for details.
cmake_minimum_required(VERSION 3.20)
project(snc_serial_fbs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find FlatBuffers ───────────────────────────────────────────
option(SNC_ENABLE_FLATBUFFERS "Enable FlatBuffers serialization support" OFF)

if(SNC_ENABLE_FLATBUFFERS)
    find_package(flatbuffers REQUIRED)
    message(STATUS "FlatBuffers found: ${FLATBUFFERS_VERSION}")
endif()

# ── Library ────────────────────────────────────────────────────
add_library(snc_serial_fbs STATIC
    ${SNC_ROOT_DIR}/src/serial/fbs/FbsSerializer.cpp
)

target_include_directories(snc_serial_fbs
    PUBLIC
        $<BUILD_INTERFACE:${SNC_ROOT_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(snc_serial_fbs PUBLIC
    $<$<BOOL:${SNC_ENABLE_FLATBUFFERS}>:SNC_ENABLE_FLATBUFFERS>
)

if(SNC_ENABLE_FLATBUFFERS)
    target_link_libraries(snc_serial_fbs
        PUBLIC flatbuffers::flatbuffers
    )
endif()

# Depends on snc_base
if(TARGET snc_base)
    target_link_libraries(snc_serial_fbs PRIVATE snc_base)
endif()

