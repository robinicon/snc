cmake_minimum_required(VERSION 3.20)
project(snc_circuit VERSION 1.0.0 LANGUAGES CXX)

# Header-only library
add_library(snc_circuit INTERFACE)

target_include_directories(snc_circuit
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(snc_circuit INTERFACE /W4 /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_circuit INTERFACE -Wall -Wextra)
endif()
