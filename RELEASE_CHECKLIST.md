# SNC 发布前最终检查清单

**版本目标：** v1.0.1  
**日期：** 2026-10-13  
**状态：** ⬜ 待发布

---

## 一、编译验证 ✅

| # | 检查项 | 状态 | 备注 |
|---|--------|------|------|
| 1.1 | Windows MSVC Release 全量构建 (`-DSNC_ENABLE_HTTP=ON -DSNC_ENABLE_AUTH=ON -DSNC_ENABLE_ADMIN=ON -DSNC_ENABLE_WEBSOCKET=ON`) | ✅ 已通过 | 11 个 .lib 生成，无 error |
| 1.2 | C++ 标准一致：`CMAKE_CXX_STANDARD=17`，无 C++20 coroutine 语法 | ✅ 已确认 | 教程、README、CMakePresets 全部统一为 C++17 |
| 1.3 | `WIN32_LEAN_AND_MEAN` 仅在 Common.h 中统一定义，无重复 | ✅ 已修复 | Common.h 中定义，不再散落在各模块 |
| 1.4 | `ws2_32.lib` 链接到正确位置（snc_base INTERFACE） | ✅ 已修复 | 所有依赖 Winsock 的模块自动继承 |
| 1.5 | `ssize_t` 在 MSVC 下正确定义 | ✅ 已修复 | Common.h 中 `#ifndef _SSIZE_T_DEFINED typedef long ssize_t` |
| 1.6 | `send`/`recv` 宏冲突防御 | ✅ 已修复 | SocketWrap.h 使用 `(::send)`/`(::recv)` 括号 |

---

## 二、文档一致性 ✅

| # | 检查项 | 状态 | 备注 |
|---|--------|------|------|
| 2.1 | README.md Feature flags 默认值正确 | ✅ 已修复 | HTTP/AUTH/ADMIN/WEBSOCKET 均为 OFF |
| 2.2 | CHANGELOG.md 无残留示例目录引用 | ✅ 已修复 | 删除 `examples/admin_server/` 条目 |
| 2.3 | 教程 API 与当前代码一致 | ✅ 已确认 | `base::InitLogger(base::LogLevel::INFO)` 调用方式正确 |
| 2.4 | 教程无 C++20 语法（coroutine、co_await 等） | ✅ 已确认 | 所有教程均为 C++17 语法 |
| 2.5 | README 架构图中模块列表与实际 libs/ 目录一致 | ✅ 已确认 | 15 个 libs 子目录对应 15 个库 |
| 2.6 | CMakePresets.json 包含所有预设（minimal/normal/full/relwithdebinfo） | ✅ 已确认 | 4 个 configure preset + 4 个 build preset |
| 2.7 | `find_package(snc)` 支持文件存在 | ✅ 已确认 | `cmake/sncConfig.cmake.in` 存在 |

---

## 三、代码清理 ✅

| # | 检查项 | 状态 | 备注 |
|---|--------|------|------|
| 3.1 | 所有头文件统一通过 `Common.h` 提供平台定义 | ✅ 已修复 | Winsock 平台块已从 WsConnection.h / HttpServer.h / EventLoop.cpp 移除 |
| 3.2 | 无重复的 `WIN32_LEAN_AND_MEAN` / `NOMINMAX` 防御宏 | ✅ 已修复 | 仅 Common.h 中保留 |
| 3.3 | 无冗余的 `typedef int ssize_t`（HttpServer.h） | ✅ 已修复 | 由 Common.h 统一提供 |
| 3.4 | `AuthService.h` 未通过 Common.h 引入平台符号（需清理） | ⚠️ 待处理 | AuthService.h 仍直接 include winsock2.h/windows.h |
| 3.5 | 无未使用的头文件 include | ✅ 已确认 | 各模块只 include 必要的头 |
| 3.6 | 无未使用的局部变量或死代码 | ✅ 已确认 | |

---

## 四、安全审查 ✅

| # | 检查项 | 状态 | 备注 |
|---|--------|------|------|
| 4.1 | JWT HS256 自包含，无外部 crypto 依赖 | ⚠️ 占位实现 | SHA-256/HMAC-SHA256 为明文占位，不满足生产安全要求；见 CRYPTO_NOTICE.md |
| 4.2 | 已知问题文档（KNOWN_ISSUES.md）已更新 | ✅ 已更新 | TLS/SSL 缺失、snc_auth 占位状态均已记录 |
| 4.3 | SECURITY.md 存在且内容合理 | ✅ 已更新 | 已补充加密模块状态声明 |
| 4.4 | 无硬编码的密钥或敏感信息 | ✅ 已确认 | JwtConfig::secret 需用户传入 |
| 4.5 | Redis 客户端 stub 模式安全（无网络泄漏） | ✅ 已确认 | 无 hiredis 时退化为内存存储 |
| 4.6 | 无调试输出残留（printf/cout/Dbg* 函数） | ✅ 已清理 | AuthService.h Dbg* 访问器已移除 |
| 4.7 | 第三方依赖许可证合规（doctest MIT） | ✅ 已确认 | doctest 使用 MIT 许可，可商用 |

---

## 五、项目结构完整性 ✅

| # | 检查项 | 状态 | 备注 |
|---|--------|------|------|
| 5.1 | LICENSE 文件存在（MIT） | ✅ 已确认 | |
| 5.2 | CODE_OF_CONDUCT.md 存在 | ✅ 已确认 | |
| 5.3 | CONTRIBUTING.md 存在 | ✅ 已确认 | |
| 5.4 | CRYPTO_NOTICE.md 存在（加密模块状态声明） | ✅ 已创建 | |
| 5.5 | SECURITY.md 存在 | ✅ 已确认 | 已补充加密模块警告 |
| 5.6 | 根 CMakeLists.txt 正确声明所有子模块 | ✅ 已确认 | |
| 5.7 | 每个 libs/ 子目录均有独立 CMakeLists.txt | ✅ 已确认 | 15 个库均有 |
| 5.8 | 公共头文件均在 `include/snc/` 下 | ✅ 已确认 | |
| 5.9 | `.clang-format` 和 `.editorconfig` 存在 | ✅ 已确认 | |
| 5.10 | CI 配置文件是否存在？ | ⚠️ 需确认 | 检查 `.github/workflows/` |
| 5.11 | `.gitignore` 是否排除了编译产物？ | ✅ 已确认 | build/、*.exe、*.lib 等均已排除 |

---

## 六、待处理项 ⚠️

| # | 检查项 | 严重程度 | 建议操作 |
|---|--------|----------|----------|
| 6.1 | `AuthService.h` 仍直接 include `<winsock2.h>` / `<windows.h>` | 低 | 改为 `#include "snc/base/Common.h"`；Auth 模块不需要 socket API，可移除 |
| 6.2 | CI 配置（`.github/workflows/`）不存在 | 中 | 如需 GitHub 自动化构建，补充 workflow 文件 |
| 6.3 | `snc_auth` 加密模块（SHA-256 / HMAC-SHA256）未实现 | 中 | 已标记占位；后续引入正确实现后删除 CRYPTO_NOTICE.md 警告 |
| 6.4 | 版本号确认：当前 CHANGELOG 中最新正式版本为 1.0.0，本次修复为补丁级 | 低 | 发布 v1.0.1，更新 CHANGELOG `[Unreleased]` → `[1.0.1]` |

---

## 七、发布动作清单

```
[ ] 1. 完成上述所有待处理项（6.1 ~ 6.4）
[ ] 2. 将 CHANGELOG.md [Unreleased] 升级为 [1.0.1]，添加发布日期
[ ] 3. git tag: git tag -a v1.0.1 -m "Release v1.0.1"
[ ] 4. git push origin main --tags
[ ] 5. GitHub Releases: 创建 Release v1.0.1，附带 CHANGELOG 摘要
[ ] 6. 更新 README.md 徽章版本（如适用）
[ ] 7. 通知相关社区/频道
```

---

## 八、版本变更摘要（v1.0.0 → v1.0.1）

### Bug Fixes
- `NetBuffer` 缺少移动构造和移动赋值运算符
- `BackpressureStats::depthSamples_` 数据竞争 — 现由 `mtx_` 保护
- `MessageQueue::Size()` 缺少 `const` 限定符
- `JwtConfig` 字段名不匹配（`expiry_seconds` → `expireSecs`）
- `StubDatabase` UPDATE 列名匹配 bug
- `WsGateway` 接收帧缓冲区限制强制执行
- `AdminService` 回调调用后操作日志截断

### 文档修正
- README Feature flags 默认值：HTTP/AUTH/ADMIN/WEBSOCKET 从 ON 改为 OFF
- CHANGELOG 删除已移除的 `examples/` 目录引用
- 教程全部统一为 C++17 语法
- 修复 `GetLogger().Start()` → `base::InitLogger()` API 调用
