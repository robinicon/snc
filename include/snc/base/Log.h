#pragma once

#include <cstdio>
#include <cstdarg>
#include <string>
#include "snc/base/Common.h"

namespace snc {
namespace base {

enum class LogLevel : int {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERR   = 4,
    FATAL = 5,
};

inline const char* LogLevelToStr(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

class LogWriter {
public:
    LogWriter() : file_(nullptr), level_(LogLevel::INFO) {}
    ~LogWriter() {
        if (file_) fclose(file_);
    }

    bool Start(const char* filename) {
        file_ = fopen(filename, "a");
        return file_ != nullptr;
    }

    void Write(LogLevel level, const char* tag, const char* fmt, ...) {
        if (level < level_) return;
        if (!file_) return;

        va_list args;
        va_start(args, fmt);
        fprintf(file_, "[%s] [%s] ", LogLevelToStr(level), tag);
        vfprintf(file_, fmt, args);
        fprintf(file_, "\n");
        fflush(file_);
        va_end(args);
    }

    void SetLevel(LogLevel level) { level_ = level; }
    LogLevel GetLevel() const { return level_; }

private:
    FILE* file_;
    LogLevel level_;
};

inline LogWriter& GetLogger() {
    static LogWriter logger;
    return logger;
}

inline void InitLogger(LogLevel level, const char* filename = nullptr) {
    auto& logger = GetLogger();
    logger.SetLevel(level);
    if (filename) logger.Start(filename);
}

#define SNC_LOG_TRACE(...) snc::base::GetLogger().Write(snc::base::LogLevel::TRACE, __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_DEBUG(...) snc::base::GetLogger().Write(snc::base::LogLevel::DEBUG, __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_INFO(...)  snc::base::GetLogger().Write(snc::base::LogLevel::INFO,  __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_WARN(...)  snc::base::GetLogger().Write(snc::base::LogLevel::WARN,  __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_ERROR(...) snc::base::GetLogger().Write(snc::base::LogLevel::ERR,   __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_FATAL(...) snc::base::GetLogger().Write(snc::base::LogLevel::FATAL, __FUNCTION__, __VA_ARGS__)

} // namespace base
} // namespace snc
