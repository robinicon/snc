cmake_minimum_required(VERSION 3.20)
project(snc_admin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(snc_admin STATIC
    src/AdminService.cpp
)

target_include_directories(snc_admin
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/snc_admin/src
)

# snc_admin depends on snc_http and snc_auth (JWT middleware only, no business logic)
target_link_libraries(snc_admin PUBLIC snc_http snc_auth snc_base)

if(MSVC)
    target_compile_options(snc_admin PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_admin PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(snc_admin PRIVATE ws2_32)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_admin PRIVATE -Wall -Wextra -Wpedantic)
endif()
