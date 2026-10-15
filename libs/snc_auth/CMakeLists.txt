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
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# snc_auth depends only on snc_base (no snc_http, no snc_redis)
target_link_libraries(snc_auth PUBLIC snc_base)
