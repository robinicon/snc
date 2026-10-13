cmake_minimum_required(VERSION 3.20)
project(snc
    VERSION 1.0.0
    DESCRIPTION "Lightweight C++17 game server networking framework for microservices"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Global compile options ─────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── AddressSanitizer (global toggle, Debug only) ───────────────
option(SNC_ENABLE_SANITIZER "Enable AddressSanitizer (Debug builds only)" OFF)
if(SNC_ENABLE_SANITIZER AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# ── Sub-projects ───────────────────────────────────────────────
add_subdirectory(libs/snc_base)
add_subdirectory(libs/snc_core)

# Optional extensions (controlled by user)
option(SNC_BUILD_RPC        "Build snc_rpc"       ON)
option(SNC_RPC_ENABLE_LUA   "Build snc_rpc_lua"   ON)  # needs Lua (falls back to stubs)
option(SNC_ENABLE_PROTOBUF  "Build snc_serial_pb" OFF)  # needs protobuf
option(SNC_ENABLE_FLATBUFFERS "Build snc_serial_fbs" OFF)  # needs flatbuffers
option(SNC_ENABLE_MONITOR   "Build snc_monitor"   ON)

if(SNC_BUILD_RPC)
    add_subdirectory(libs/snc_rpc)
endif()
if(SNC_RPC_ENABLE_LUA)
    add_subdirectory(libs/snc_rpc_lua)
endif()
# Always build snc_serial_pb (stub mode when protobuf unavailable)
add_subdirectory(libs/snc_serial_pb)
# Always build snc_serial_fbs (stub mode when flatbuffers unavailable)
add_subdirectory(libs/snc_serial_fbs)
# P2: Service discovery (lightweight in-process registry)
add_subdirectory(libs/snc_discovery)
if(SNC_ENABLE_MONITOR)
    add_subdirectory(libs/snc_monitor)
endif()
# ── Benchmark tool (optional, removed for open-source release) ──
# # P2: Benchmark tool (PB vs FBS performance comparison)
# add_subdirectory(libs/snc_bench)
# P3: Circuit breaker & rate limiter
add_subdirectory(libs/snc_circuit)
# P3: Cluster gateway (load-balanced proxy with circuit breaking)
add_subdirectory(libs/snc_gateway)
# P3: Database adapter layer (abstract IDatabase + in-memory StubDatabase)
add_subdirectory(libs/snc_db)

# New modules for WebSocket, HTTP, Redis (enabled individually)
option(SNC_ENABLE_WEBSOCKET "Build snc_websocket" OFF)
option(SNC_ENABLE_HTTP      "Build snc_http"      OFF)
option(SNC_ENABLE_REDIS     "Build snc_redis"     OFF)
option(SNC_ENABLE_AUTH      "Build snc_auth"      OFF)
option(SNC_ENABLE_ADMIN     "Build snc_admin"     OFF)

if(SNC_ENABLE_REDIS)
    add_subdirectory(libs/snc_redis)
endif()
if(SNC_ENABLE_WEBSOCKET)
    add_subdirectory(libs/snc_websocket)
endif()
if(SNC_ENABLE_HTTP)
    add_subdirectory(libs/snc_http)
endif()
if(SNC_ENABLE_AUTH)
    add_subdirectory(libs/snc_auth)
endif()
if(SNC_ENABLE_ADMIN)
    add_subdirectory(libs/snc_admin)
endif()

# ── MMORPG 模块（纯头文件，依赖上述可选模块）────────────────────
option(SNC_BUILD_MMORPG "Build snc_mmorpg" ON)
if(SNC_BUILD_MMORPG)
    add_subdirectory(libs/snc_mmorpg)
endif()

# ── Examples ────────────────────────────────────────────────────
option(SNC_BUILD_EXAMPLES "Build example programs" ON)
if(SNC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Tests ───────────────────────────────────────────────────────
option(SNC_BUILD_TESTS "Build unit tests" ON)
if(SNC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ── Tutorials ───────────────────────────────────────────────────
# Tutorial documentation lives in docs/tutorials/

# ── Install rules ───────────────────────────────────────────────
include(GNUInstallDirs)

# Collect all available targets for installation
set(_snc_install_targets snc_base snc_core snc_serial_pb snc_serial_fbs snc_discovery snc_circuit snc_gateway snc_db)
if(SNC_BUILD_RPC)
    list(APPEND _snc_install_targets snc_rpc)
endif()
if(SNC_RPC_ENABLE_LUA)
    list(APPEND _snc_install_targets snc_rpc_lua)
endif()
if(SNC_ENABLE_MONITOR)
    list(APPEND _snc_install_targets snc_monitor)
endif()
if(SNC_ENABLE_WEBSOCKET)
    list(APPEND _snc_install_targets snc_websocket)
endif()
if(SNC_ENABLE_HTTP)
    list(APPEND _snc_install_targets snc_http)
endif()
if(SNC_ENABLE_AUTH)
    list(APPEND _snc_install_targets snc_auth)
endif()
if(SNC_ENABLE_ADMIN)
    list(APPEND _snc_install_targets snc_admin)
endif()

install(TARGETS ${_snc_install_targets}
    EXPORT sncTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public headers
install(DIRECTORY include/snc
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Install CMake config for downstream find_package(snc) usage
install(EXPORT sncTargets
    FILE sncTargets.cmake
    NAMESPACE snc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/snc
)

# Generate and install version file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sncConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sncConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sncConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/snc
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/sncConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/sncConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/snc
)
