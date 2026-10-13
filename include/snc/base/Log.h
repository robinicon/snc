#pragma once

#include <cstdio>
#include <cstdarg>
#include <string>
#include <mutex>
#include "snc/base/Common.h"

namespace snc {
namespace base {

/// 日志级别枚举，数值越大优先级越高。
/// TRACE < DEBUG < INFO < WARN < ERR < FATAL
enum class LogLevel : int {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERR   = 4,
    FATAL = 5,
};

/// 将 LogLevel 转换为可读字符串（供日志行前缀使用）。
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

/// 日志写入器（单例，线程安全）。
/// 使用 pthread/CRITICAL_SECTION 风格的简单互斥锁保证写入不交错。
/// 日志格式：[TIME] [LEVEL] [TAG] message\n
class LogWriter {
public:
    LogWriter() : file_(nullptr), level_(LogLevel::INFO) {}
    ~LogWriter() {
        if (file_) fclose(file_);
    }

    /// 打开日志文件（追加模式）。返回 true 表示成功。
    bool Start(const char* filename) {
        file_ = fopen(filename, "a");
        return file_ != nullptr;
    }

    /// 写入一条日志（可变参数，格式串与 printf 一致）。
    /// 当 level < level_ 时静默丢弃；file_ 为 null 时同样丢弃。
    void Write(LogLevel level, const char* tag, const char* fmt, ...) {
        if (level < level_) return;
        if (!file_) return;

        static std::mutex logMutex;
        std::lock_guard<std::mutex> lock(logMutex);

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
    FILE*     file_;
    LogLevel  level_;
};

/// 获取全局日志写入器（Meyers' singleton，线程安全）。
/// 注意：此函数返回的是引用，调用方不应存储其地址。
inline LogWriter& GetLogger() {
    static LogWriter logger;
    return logger;
}

/// 初始化日志：设置最低记录级别，可选指定日志文件路径。
/// 在应用启动早期调用一次即可（例如 main() 入口处）。
/// 示例：base::InitLogger(base::LogLevel::INFO, "server.log");
inline void InitLogger(LogLevel level, const char* filename = nullptr) {
    auto& logger = GetLogger();
    logger.SetLevel(level);
    if (filename) logger.Start(filename);
}

// ── 日志宏 ──────────────────────────────────────────────────────
// 每个宏自动注入当前函数名作为 tag，便于定位日志来源。
#define SNC_LOG_TRACE(...) snc::base::GetLogger().Write(snc::base::LogLevel::TRACE, __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_DEBUG(...) snc::base::GetLogger().Write(snc::base::LogLevel::DEBUG, __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_INFO(...)  snc::base::GetLogger().Write(snc::base::LogLevel::INFO,  __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_WARN(...)  snc::base::GetLogger().Write(snc::base::LogLevel::WARN,  __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_ERROR(...) snc::base::GetLogger().Write(snc::base::LogLevel::ERR,   __FUNCTION__, __VA_ARGS__)
#define SNC_LOG_FATAL(...) snc::base::GetLogger().Write(snc::base::LogLevel::FATAL, __FUNCTION__, __VA_ARGS__)

} // namespace base
} // namespace snc
