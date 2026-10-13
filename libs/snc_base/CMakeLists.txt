cmake_minimum_required(VERSION 3.20)
project(snc_base VERSION 1.0.0 LANGUAGES CXX)

# ── Library target ──────────────────────────────────────────────
# snc_base is header-only (Log.h, ThreadTools.h, ByteOrder.h, etc.)
add_library(snc_base INTERFACE)

target_include_directories(snc_base
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Platform-specific compile flags propagate to consumers via INTERFACE
if(MSVC)
    target_compile_options(snc_base INTERFACE /W4 /utf-8)
    target_link_libraries(snc_base INTERFACE ws2_32)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_base INTERFACE -Wall -Wextra -Wpedantic)
endif()
