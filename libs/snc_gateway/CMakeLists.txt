cmake_minimum_required(VERSION 3.20)
project(snc_gateway VERSION 1.0.0 LANGUAGES CXX)

# Header-only library
add_library(snc_gateway INTERFACE)

target_include_directories(snc_gateway
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(snc_gateway INTERFACE snc_discovery snc_circuit snc_rpc snc_base)

if(MSVC)
    target_compile_options(snc_gateway INTERFACE /W4 /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(snc_gateway INTERFACE -Wall -Wextra)
endif()
