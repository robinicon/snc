#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include "snc/base/Common.h"

namespace snc {
namespace base {

inline uint64_t NowMilliSeconds() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000.0 / freq.QuadPart);
#else
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
#endif
}

inline uint64_t NowMicroSeconds() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000000.0 / freq.QuadPart);
#else
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
#endif
}

inline std::string FormatDuration(long long us) {
    char buf[64];
    if (us >= 1000000) {
        snprintf(buf, sizeof(buf), "%llds", us / 1000000);
    } else if (us >= 1000) {
        snprintf(buf, sizeof(buf), "%lldms", us / 1000);
    } else {
        snprintf(buf, sizeof(buf), "%lldus", us);
    }
    return std::string(buf);
}

class Timer {
public:
    Timer() : start_(NowMicroSeconds()) {}
    void Reset() { start_ = NowMicroSeconds(); }
    long long ElapsedMs() const {
        return (NowMicroSeconds() - start_) / 1000;
    }
private:
    long long start_;
};

class StopWatch {
public:
    StopWatch() : running_(false), start_(0), elapsed_(0) {}
    void Start() {
        start_ = NowMicroSeconds();
        running_ = true;
    }
    void Stop() {
        if (running_) {
            elapsed_ += NowMicroSeconds() - start_;
            running_ = false;
        }
    }
    long long ElapsedMs() const {
        long long now = running_ ? NowMicroSeconds() : start_ + elapsed_;
        return (now - start_) / 1000;
    }
    void Reset() { start_ = NowMicroSeconds(); elapsed_ = 0; running_ = false; }
private:
    bool running_;
    long long start_;
    long long elapsed_;
};

} // namespace base
} // namespace snc
