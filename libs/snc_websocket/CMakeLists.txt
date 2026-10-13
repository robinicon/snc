cmake_minimum_required(VERSION 3.20)
project(snc_websocket VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(snc_websocket STATIC
    src/WsGateway.cpp
)

target_include_directories(snc_websocket
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/snc_websocket/src
)

target_link_libraries(snc_websocket PUBLIC snc_base)

if(MSVC)
    target_compile_options(snc_websocket PRIVATE /W4 /WX- /utf-8)
    target_compile_definitions(snc_websocket PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_websocket PRIVATE -Wall -Wextra -Wpedantic)
endif()
