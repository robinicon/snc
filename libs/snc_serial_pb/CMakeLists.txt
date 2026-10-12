cmake_minimum_required(VERSION 3.20)
project(snc_serial_pb VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find Protobuf ──────────────────────────────────────────────────────────────
option(SNC_ENABLE_PROTOBUF "Enable Protobuf serialization support" OFF)

if(SNC_ENABLE_PROTOBUF)
    find_package(Protobuf REQUIRED)
    message(STATUS "Protobuf found: ${PROTOBUF_VERSION}")
endif()

# ── Library ────────────────────────────────────────────────────────────────────
add_library(snc_serial_pb STATIC
    ${CMAKE_SOURCE_DIR}/src/serial/pb/PbSerializer.cpp
)

target_include_directories(snc_serial_pb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(snc_serial_pb PUBLIC
    $<$<BOOL:${SNC_ENABLE_PROTOBUF}>:SNC_ENABLE_PROTOBUF>
)

if(SNC_ENABLE_PROTOBUF)
    target_link_libraries(snc_serial_pb
        PUBLIC protobuf::libprotobuf
    )
endif()

# Depends on snc_base (MsgBuffer)
if(TARGET snc_base)
    target_link_libraries(snc_serial_pb PRIVATE snc_base)
endif()

