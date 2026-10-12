#pragma once

#include <cstdint>
#include <cstddef>

namespace snc {

// Platform detection
#ifdef _WIN32
    #define SNC_PLATFORM_WINDOWS
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

// Prevent windows.h from defining min/max macros
#ifndef NOMINMAX
    #define NOMINMAX
#endif

} // namespace snc
