cmake_minimum_required(VERSION 3.20)
project(snc_circuit VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only library — create a dummy static lib for linking consistency
add_library(snc_circuit STATIC
    ${CMAKE_SOURCE_DIR}/libs/snc_circuit/dummy.cpp
)

target_include_directories(snc_circuit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(snc_circuit PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_circuit PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_circuit PRIVATE -Wall -Wextra)
endif()

