#pragma once

// 平台相关头文件必须在 namespace 之外包含，
// 否则 Winsock/cctype 等符号会被导入 snc 命名空间，导致编译错误。
#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <windows.h>
    // MSVC 不直接提供 ssize_t，此处手动定义（long 类型，与 POSIX 兼容）
    #ifndef _SSIZE_T_DEFINED
        typedef long ssize_t;
        #define _SSIZE_T_DEFINED
    #endif
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace snc {

// ── 平台检测宏 ──────────────────────────────────────────────────
#ifdef _WIN32
    #define SNC_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define SNC_PLATFORM_LINUX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#elif defined(__APPLE__)
    #define SNC_PLATFORM_MACOS
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

// ── 编译器检测宏 ────────────────────────────────────────────────
#ifdef _MSC_VER
    #define SNC_COMPILER_MSVC
#elif defined(__GNUC__)
    #define SNC_COMPILER_GCC
#elif defined(__clang__)
    #define SNC_COMPILER_CLANG
#endif

/// 内联提示：MSVC 用 __forceinline，GCC/Clang 用 always_inline
#ifdef SNC_COMPILER_MSVC
    #define SNC_FORCE_INLINE __forceinline
#else
    #define SNC_FORCE_INLINE __attribute__((always_inline)) inline
#endif

/// DLL 导出/导入宏。
/// - 构建共享库时（SNC_BUILD_SHARED），Windows 下使用 __declspec(dllexport)
/// - 静态库或跨平台场景下为空宏，不影响 ABI
#ifdef _WIN32
    #ifdef SNC_BUILD_SHARED
        #define SNC_API __declspec(dllexport)
    #else
        #define SNC_API
    #endif
#else
    #define SNC_API __attribute__((visibility("default")))
#endif

// ── 字节序工具函数 ──────────────────────────────────────────────
/// 检测当前系统是否为小端序（x86/x64 均为小端）。
inline bool IsLittleEndian() {
    uint16_t x = 1;
    return *reinterpret_cast<uint8_t*>(&x) == 1;
}

/// 主机字节序 → 小端 16 位（网络字节序为大端，游戏协议常用小端）
inline uint16_t HostToLE16(uint16_t v) {
    return IsLittleEndian() ? v : ((v >> 8) | (v << 8));
}

/// 主机字节序 → 小端 32 位
inline uint32_t HostToLE32(uint32_t v) {
    if (IsLittleEndian()) return v;
    return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}

// ── 时间工具 ────────────────────────────────────────────────────
/// 返回单调时钟的毫秒时间戳（跨平台，不受系统时间调整影响）。
/// Windows 使用 QueryPerformanceCounter，POSIX 使用 std::chrono::high_resolution_clock。
inline int64_t TimeMs() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return static_cast<int64_t>(counter.QuadPart * 1000.0 / freq.QuadPart);
#else
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<milliseconds>(
        high_resolution_clock::now().time_since_epoch()).count());
#endif
}

} // namespace snc
