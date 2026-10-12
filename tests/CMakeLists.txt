cmake_minimum_required(VERSION 3.20)
project(snc_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(snc_test
    test_main.cpp
    test_base.cpp
    test_message_queue.cpp
    test_timer.cpp
)

target_include_directories(snc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/thirdparty/doctest
)

target_link_libraries(snc_test PRIVATE
    snc_base
    snc_core
)

# Platform-specific libraries
if(MSVC)
    target_link_libraries(snc_test PRIVATE ws2_32)
else()
    target_link_libraries(snc_test PRIVATE pthread)
endif()

add_test(NAME snc_test COMMAND snc_test)

# Enable warnings
if(MSVC)
    target_compile_options(snc_test PRIVATE /W3)
else()
    target_compile_options(snc_test PRIVATE -Wall -Wextra)
endif()
