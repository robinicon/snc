// SPDX-License-Identifier: MIT
// snc/base/Common.h
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cassert>
#include <stdexcept>
#include <system_error>

// Platform detection
#ifdef _WIN32
    // 预先定义 WIN32_LEAN_AND_MEAN 防止 windows.h 包含旧 winsock.h
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

#ifdef _WIN32
    // 清除 windows.h 定义的宏（winsock2.h 已在 include 前解除 far/near）
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif
#endif

// Windows-specific macros
#ifdef _WIN32
    #define SNC_PLATFORM_WINDOWS 1
    #define SNC_PLATFORM_UNIX 0
    #define SNC_FORCE_INLINE __forceinline
    #define SNC_DEPRECATED __declspec(deprecated)
    using ssize_t = ptrdiff_t;
#else
    #define SNC_PLATFORM_WINDOWS 0
    #define SNC_PLATFORM_UNIX 1
    #define SNC_FORCE_INLINE inline __attribute__((always_inline))
    #define SNC_DEPRECATED __attribute__((deprecated))
#endif

// Logging macros (basic implementation; Log.h provides the full logger with __FUNCTION__ tag)
#ifndef SNC_LOG_IMPL_DEFINED
#define SNC_LOG_IMPL_DEFINED
#define SNC_LOG_INFO(...)   snc::log_impl(__VA_ARGS__)
#define SNC_LOG_WARN(...)   snc::log_warn_impl(__VA_ARGS__)
#define SNC_LOG_ERROR(...)  snc::log_error_impl(__VA_ARGS__)
#define SNC_LOG_DEBUG(...)  snc::log_debug_impl(__VA_ARGS__)
#endif

namespace snc {

inline void log_impl(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fputc('\n', stdout);
}

inline void log_warn_impl(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[WARN] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

inline void log_error_impl(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

inline void log_debug_impl(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "[DEBUG] ");
    vfprintf(stdout, fmt, args);
    va_end(args);
    fputc('\n', stdout);
}

// UUID generation
inline std::string generate_uuid() {
    // Simple UUID v4 implementation
    static thread_local unsigned int seed = 0;
    seed ^= static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    seed ^= seed >> 16;
    seed *= 0x7feb352dU;
    seed ^= seed >> 15;
    seed *= 0x846ca68bU;
    seed ^= seed >> 16;

    char buf[37];
    snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<unsigned>(seed ^ static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id()))),
        static_cast<unsigned short>(seed & 0xffff),
        static_cast<unsigned short>((seed >> 16) & 0xffff),
        static_cast<unsigned short>((seed >> 32) & 0xffff),
        static_cast<long long>(seed) & 0xffffffffffffLL);
    return std::string(buf);
}

} // namespace snc
