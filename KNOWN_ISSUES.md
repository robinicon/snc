# Known Issues & Limitations

This document tracks known issues and limitations in the SNC framework.

## Platform Limitations

### macOS
- ~~kqueue backend not implemented~~ → **Fixed**: kqueue backend is now implemented and fully supported.

## Library Limitations

### snc_rpc_lua (stub mode)
- When Lua is not installed, all Lua API functions are no-ops.
- `LuaVmManager`, `LuaRpcApi`, `LuaRouterApi` throw or return empty results.
- The test suite passes but no actual Lua scripting is available.
- Enable with `SNC_RPC_ENABLE_LUA=ON` and ensure Lua 5.4 or LuaJIT is installed.

### snc_serial_pb / snc_serial_fbs
- Protobuf and FlatBuffers serialization adapters are stubs by default.
- They compile and link but do nothing (return empty bytes).
- Enable with `SNC_ENABLE_PROTOBUF=ON` or `SNC_ENABLE_FLATBUFFERS=ON` and install the respective libraries.

### snc_db
- Redis-backed service discovery requires `hiredis`. Without it, the in-memory registry is used.

### snc_auth (cryptographic module — placeholder)
- **SHA-256 and HMAC-SHA256 are NOT implemented.** The current implementations are non-functional placeholders:
  - `Sha256()` returns the input unchanged.
  - `HmacSha256()` returns `data + key`.
- `JwtService::Sign()` and `Verify()` are therefore non-functional — signatures cannot be verified.
- **Do not use `snc_auth` for production authentication.**
- A correct implementation (RFC 6234 / RFC 2104) is planned for a future release.
- See [CRYPTO_NOTICE.md](CRYPTO_NOTICE.md) for the full status and migration guidance.

## Performance Notes

### TcpConnection flush behavior
- `Send()` buffers data internally; `FlushSendBuffer()` performs a single `::send()` call.
- For high-throughput scenarios, batch messages before sending to reduce syscall overhead.
- The ring buffer compacts when more than half is consumed (see `CompactSendBuffer()`).

### Heartbeat
- Default interval: 30s ping, 10s pong timeout, 3 max missed pongs.
- Heartbeat runs on the EventLoop thread — do not block it in callbacks.

## Security Notes

- SNC does **not** implement TLS/SSL. Use a reverse proxy (e.g., Nginx, Envoy) for encrypted traffic.
- No authentication is built in — applications should implement their own auth layer.
- RPC messages are transmitted in plaintext; do not send sensitive data without an external encryption layer.

## Debug vs Release

- ASan (`SNC_ENABLE_SANITIZER=ON`) is Linux-only.
- Gateway tests may crash in Debug mode on Windows due to ASan instrumentation; Release mode is stable.
- Use `-DSNC_ENABLE_SANITIZER=OFF` for Windows Debug builds if crashes occur.
