# CHANGELOG

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] — 2026-10-19

### Fixed
- `example_04_gate.cpp` 编译错误：`std::signal` → `<csignal>`，`Shutdown` → `Stop`，移除不存在的 `OnPlayerDisconnect` 调用，修复 lambda 捕获
- `IConnection.h` WorkerDispatchFn MSVC 模板参数可见性：将 using 声明移至 class IConnection 定义之后

### Changed
- 单元测试：移除 doctest 第三方头文件，替换为自包含 Catch2-like 测试框架（`test_framework.h`）
- README：更新文档结构描述，反映自包含测试框架
- 所有 CMakeLists.txt 添加 `SPDX-License-Identifier: MIT` 许可证头
- `thirdparty/doctest/` 添加独立 LICENSE 文件
- `libs/asio_standalone/` 各头文件添加 SPDX 许可证头及独立实现声明
- 示例代码移除硬编码 JWT fallback secret，改为强制要求环境变量 `SNC_JWT_SECRET`
- `.gitignore` 补充 `build_log*.txt`、`build_output.txt`、`build-verbose.txt`
- `mmo/CMakeLists.txt` 移除硬编码 Windows Lua 路径（`E:/Lua/luajit/*`），更新为 vcpkg 安装提示
- `mmo/include/` 全部头文件添加 `SPDX-License-Identifier: MIT` 许可证头
- 新增 `AUTHORS.md`、`CODE_OF_CONDUCT.md`、`SECURITY.md`、`KNOWN_ISSUES.md`、`CRYPTO_NOTICE.md`
- 新增 `vcpkg.json` 可选依赖声明

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
