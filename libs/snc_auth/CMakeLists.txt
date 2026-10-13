cmake_minimum_required(VERSION 3.20)
project(snc_auth VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(snc_auth STATIC
    src/AuthService.cpp
)

target_include_directories(snc_auth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/snc_auth/src
)

# snc_auth depends only on snc_base (no snc_http, no snc_redis)
target_link_libraries(snc_auth PUBLIC snc_base)

if(MSVC)
    target_compile_options(snc_auth PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_auth PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(snc_auth PRIVATE ws2_32)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_auth PRIVATE -Wall -Wextra -Wpedantic)
endif()
