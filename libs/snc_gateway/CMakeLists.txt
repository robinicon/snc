cmake_minimum_required(VERSION 3.20)
project(snc_gateway VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only library
add_library(snc_gateway STATIC
    ${CMAKE_SOURCE_DIR}/libs/snc_gateway/dummy.cpp
)

target_include_directories(snc_gateway
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_gateway PUBLIC snc_discovery snc_circuit snc_rpc snc_base)

if(MSVC)
    target_compile_options(snc_gateway PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_gateway PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_gateway PRIVATE -Wall -Wextra)
endif()

