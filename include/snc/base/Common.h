#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace snc {

// Platform detection
#ifdef _WIN32
    #define SNC_PLATFORM_WINDOWS
    // Prevent windows.h from defining min/max macros
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // Must include winsock2.h before windows.h on Windows
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <windows.h>
#elif defined(__linux__)
    #define SNC_PLATFORM_LINUX
#elif defined(__APPLE__)
    #define SNC_PLATFORM_MACOS
#endif

// Compiler detection
#ifdef _MSC_VER
    #define SNC_COMPILER_MSVC
#elif defined(__GNUC__)
    #define SNC_COMPILER_GCC
#elif defined(__clang__)
    #define SNC_COMPILER_CLANG
#endif

// Inline hint
#ifdef SNC_COMPILER_MSVC
    #define SNC_FORCE_INLINE __forceinline
#else
    #define SNC_FORCE_INLINE __attribute__((always_inline)) inline
#endif

// Host byte-order helpers
inline bool IsLittleEndian() {
    uint16_t x = 1;
    return *reinterpret_cast<uint8_t*>(&x) == 1;
}

inline uint16_t HostToLE16(uint16_t v) {
    return IsLittleEndian() ? v : ((v >> 8) | (v << 8));
}

inline uint32_t HostToLE32(uint32_t v) {
    if (IsLittleEndian()) return v;
    return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}

// Millisecond timestamp (monotonic, cross-platform)
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
