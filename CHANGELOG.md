# CHANGELOG

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] — 2026-10-13

### Fixed
- `example_04_gate.cpp` 编译错误：`std::signal` → `<csignal>`，`Shutdown` → `Stop`，移除不存在的 `OnPlayerDisconnect` 调用，修复 lambda 捕获
- `IConnection.h` WorkerDispatchFn MSVC 模板参数可见性：将 using 声明移至 class IConnection 定义之后

### Changed
- 单元测试：移除 doctest 第三方头文件，替换为自包含 Catch2-like 测试框架（`test_framework.h`）
- README：更新文档结构描述，反映自包含测试框架

---

## [Unreleased]

### Added
- `snc_http` — lightweight HTTP server with routing, middleware chain, JSON responses
- `snc_auth` — JWT (HS256) signing, verification, and middleware factory; self-contained, no OpenSSL dependency
- `snc_admin` — Admin API scaffolding with callback-driven business logic injection; ban/unban, resource modification, operation logs
- `snc_websocket` — WebSocket gateway implementing RFC 6455; `IGatewayBridge` pure virtual callback interface
- `snc_redis` — Redis client adapter; in-memory stub by default, hiredis backend optional via `SNC_REDIS_CLIENT=ON`
- `snc_db` — Database adapter layer with `StubDatabase` for testing
- Tutorials: HTTP Server, JWT Auth, Admin API, WebSocket Gateway
- `SNC_API` export macro in `Common.h` for future shared-library builds

### Fixed
- `NetBuffer` missing move constructor and move assignment operator
- `BackpressureStats::depthSamples_` data race — now protected by `mtx_`
- `MessageQueue::Size()` missing `const` qualifier
- `JwtConfig` field name mismatch (`expiry_seconds` → `expireSecs`)
- `StubDatabase` UPDATE column matching bug
- `WsGateway` buffer limit enforcement for received frames
- `AdminService` operation log truncation after callback invocation

### Changed
- `snc_auth` refactored: removed WeChat mini-program login and other hardcoded business logic; only generic `JwtService` remains
- `snc_auth` SHA-256 / HMAC-SHA256 replaced with placeholder implementations (not cryptographically correct); see CRYPTO_NOTICE.md
- Removed all debug test programs (`debug_jwt*`, `debug_crash*`, `debug_hmac*`, `debug_sha256*`, etc.)
- Removed `Dbg*` debug accessors from `JwtService` public API
- Test suite cleaned up: only 5 production test targets remain in `tests/CMakeLists.txt`
- `snc_admin` refactored: removed in-memory player store; all business logic is now injected via callbacks
- Removed unit tests and test fixtures (now tutorial/documentation-focused)
- Removed `examples/` directory from build system

---

## [1.0.0] — 2026-10-12

### Added
- `snc_base` — Logging, threading primitives, byte-order utilities
- `snc_core` — Event loop (IOCP/select on Windows, epoll on Linux, kqueue on macOS), TcpConnection, message router, worker pool, timer manager
- `snc_rpc` — Async RPC client/server with Entity handle management
- `snc_rpc_lua` — Lua scripting bridge for hot-reloadable game logic (stub when Lua unavailable)
- `snc_serial_pb` — Protobuf serialization adapter (stub mode by default)
- `snc_serial_fbs` — FlatBuffers serialization adapter (stub mode by default)
- `snc_discovery` — In-process service registry with `ServiceRegistry::Pick()` (ROUND_ROBIN / WEIGHTED / RANDOM)
- `snc_circuit` — Circuit breaker (CLOSED/OPEN/HALF_OPEN) + token bucket rate limiter
- `snc_gateway` — Cluster gateway with load-balanced forwarding and circuit-breaker integration
- `snc_monitor` — Runtime metrics: I/O idle stats, backpressure detection, Lua block-time monitoring
- `CMakePresets.json` — Presets: minimal, normal, full, relwithdebinfo, msvc-minimal, msvc-full
- `cmake/sncConfig.cmake.in` — `find_package(snc)` support with exported targets (`snc::snc_base`, etc.)
- `LICENSE` (MIT), `README.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `KNOWN_ISSUES.md`
- Getting-started tutorials: Quick Start, RPC Service, Connection Lifecycle
