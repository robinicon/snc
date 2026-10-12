# SNC — Server Network Component

A C++17 microservice networking framework for game servers.

## Overview

SNC provides a layered set of libraries covering the full stack from
low-level TCP I/O to high-level RPC, service discovery, and circuit breaking.

```
snc_base     — Logging, data structures, threading primitives
snc_core     — Event loop (IOCP/epoll), TcpConnection, message routing
snc_rpc      — Async RPC client & server, Entity handle management
snc_circuit  — Circuit breaker (CLOSED/OPEN/HALF_OPEN) + token bucket rate limiter
snc_discovery — In-memory / Redis-backed service registry
snc_gateway  — Cluster gateway with load-balanced forwarding and circuit breaker integration
snc_monitor  — Runtime metrics (I/O idle stats, backpressure, Lua block time)
snc_rpc_lua  — Lua scripting bridge for hot-reloadable game logic
snc_serial_pb — Protobuf serialization adapter (stub by default)
snc_serial_fbs — FlatBuffers serialization adapter (stub by default)
```

### Supported platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Windows (MSVC) | ✅ Fully supported | IOCP event loop |
| Linux (GCC/Clang) | ✅ Fully supported | epoll event loop |
| macOS | ⚠️ Limited | No kqueue backend; epoll code will not compile on macOS |

### Compiler requirements

- C++17 compliant compiler (MSVC 2019+, GCC 8+, Clang 7+)
- CMake ≥ 3.20

## Tutorials

Get started with the tutorials in [docs/tutorials/](docs/tutorials/):

| Tutorial | Description |
|----------|-------------|
| [01 - Quick Start](docs/tutorials/01-quickstart.md) | Build setup and first echo server |
| [02 - RPC Service](docs/tutorials/02-rpc-service.md) | RPC client/server patterns |
| [03 - Connection Lifecycle](docs/tutorials/03-connection-lifecycle.md) | EventLoop, backpressure, entity handling |

## Building

### Prerequisites

- **CMake 3.20+**
- Optional dependencies (disabled by default, library still builds without them):
  - **Lua 5.4 / LuaJIT** — `SNC_RPC_ENABLE_LUA=ON`
  - **Protobuf** — `SNC_ENABLE_PROTOBUF=ON`
  - **FlatBuffers** — `SNC_ENABLE_FLATBUFFERS=ON`
  - **hiredis** — Redis-backed service discovery

### Quick start (Windows, MSVC)

```bash
cd snc_cpp
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### Quick start (Linux)

```bash
cd snc_cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### CMake presets

The project ships with `CMakePresets.json` containing four preset configurations:

| Preset | Description |
|--------|-------------|
| `minimal` | `snc_base` + `snc_core` + `snc_rpc` only |
| `normal` | Minimal + `snc_rpc_lua` + `snc_monitor` |
| `full` | All features (Lua, Protobuf, FlatBuffers, Monitor) |
| `relwithdebinfo` | Release with debug info (default) |

```bash
cmake --preset=full
cmake --build --preset=full
```

### Feature flags

| Flag | Default | Description |
|------|---------|-------------|
| `SNC_BUILD_RPC` | ON | Build the RPC library |
| `SNC_RPC_ENABLE_LUA` | ON | Enable Lua scripting bridge |
| `SNC_ENABLE_PROTOBUF` | OFF | Enable Protobuf serialization |
| `SNC_ENABLE_FLATBUFFERS` | OFF | Enable FlatBuffers serialization |
| `SNC_ENABLE_MONITOR` | ON | Enable runtime monitoring |
| `SNC_ENABLE_SANITIZER` | OFF | Enable AddressSanitizer (Debug builds) |

## Architecture

### Layered design

```
┌─────────────────────────────────────────────────┐
│  Application / Game Logic (Lua or C++)          │
├─────────────────────────────────────────────────┤
│  snc_gateway  — Cluster gateway, load balancing │
│  snc_discovery — Service registry               │
│  snc_circuit  — Circuit breaker                 │
├─────────────────────────────────────────────────┤
│  snc_rpc      — RPC client / server             │
├─────────────────────────────────────────────────┤
│  snc_core     — Event loop, TCP, message router │
├─────────────────────────────────────────────────┤
│  snc_base     — Log, threading, data structs    │
└─────────────────────────────────────────────────┘
```

### Key patterns

- **Entity / EntityHandle**: Each logical game entity owns an `Entity` (shared_ptr with
  embedded connection). `EntityHandle<Entity>` is a weak-ptr wrapper safe to pass
  across threads.
- **RpcSession**: Maintains a pending-call map keyed by sequence number. Requests
  are serialized into `Msg` objects with `kRpcMsgId=0xFEED`.
- **ClusterGateway**: Thread-safe, lock-free `Forward()` path. Statistics and
  circuit-breaker state are protected by mutex.
- **CircuitBreaker**: Three-state machine (CLOSED → OPEN → HALF_OPEN) with
  configurable failure threshold and recovery timeout.
- **MsgRouter**: Routes incoming messages by `msgId` to handler functions.
  Supports both C++ and Lua handlers.

## Public API Highlights

### Logging

```cpp
#include "snc/base/Log.h"

SNC_LOG_INFO("tag", "Hello %s", name);
SNC_LOG_WARN("tag", "Something went wrong: %d", err);
SNC_LOG_ERROR("tag", "Fatal: %s", msg);
```

### Creating an RPC server

```cpp
#include "snc/rpc/RpcServer.h"

auto server = std::make_shared<rpc::RpcServer>(eventLoop);
server->RegisterHandler(kLoginMsgId, [](const uint8_t* data, size_t len) {
    // handle login request
});
server->Listen(8001);
```

### Creating an RPC client

```cpp
#include "snc/rpc/RpcClient.h"

auto client = rpc::RpcClient::Create(eventLoop, []() {
    return std::make_shared<core::TcpConnection>(/* ... */);
});
client->Connect("127.0.0.1", 8001);

client->Call(kLoginMsgId, payload, len, [](RpcError err, const uint8_t* resp, size_t rlen) {
    if (err == RpcError::OK) { /* handle response */ }
});
```

### Circuit breaker

```cpp
#include "snc/circuit/CircuitBreaker.h"

auto breaker = std::make_shared<circuit::CircuitBreaker>(
    /* failureThreshold */ 5,
    /* recoveryTimeoutMs */ 30000
);

if (breaker->Allow()) {
    // proceed with call
} else {
    // request rejected — service is open
}
```

### Service discovery

```cpp
#include "snc/discovery/ServiceRegistry.h"

auto reg = std::make_shared<discovery::ServiceRegistry>();
reg->Register("login", "10.0.0.1", 8001, /* weight */ 1);
reg->Register("map",   "10.0.0.2", 8002, /* weight */ 2);

auto endpoints = reg->List("login"); // returns vector of Endpoint
```

## Directory layout

```
snc_cpp/
├── CMakeLists.txt          # Root build configuration
├── CMakePresets.json       # Predefined build presets
├── include/snc/            # Public headers (install target)
│   ├── base/               # Logging, threading, data structures
│   ├── core/               # Event loop, TCP, messaging
│   ├── rpc/                # RPC client, server, entity
│   ├── circuit/            # Circuit breaker
│   ├── discovery/          # Service registry
│   ├── gateway/            # Cluster gateway
│   └── monitor/            # Runtime metrics
├── src/core/               # Core library sources
├── libs/                   # Library sub-projects
│   ├── snc_base/
│   ├── snc_core/
│   ├── snc_rpc/
│   ├── snc_circuit/
│   ├── snc_discovery/
│   ├── snc_gateway/
│   ├── snc_monitor/
│   ├── snc_rpc_lua/
│   ├── snc_serial_pb/
│   ├── snc_serial_fbs/
│   └── snc_db/
├── docs/tutorials/         # Getting-started tutorials
├── proto/                  # Protobuf schema (reference only)
└── thirdparty/             # Bundled third-party headers (doctest)
```

## License

MIT License — see [LICENSE](../LICENSE).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Known Issues

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for platform limitations and known bugs.
