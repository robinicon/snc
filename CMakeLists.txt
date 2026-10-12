cmake_minimum_required(VERSION 3.20)
project(snc_cpp VERSION 1.0.0 LANGUAGES CXX)

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

# ── Tutorials ───────────────────────────────────────────────────
# Tutorial documentation lives in docs/tutorials/
