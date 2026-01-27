#pragma once

#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <ostream>

#include "LogLevel.h"

namespace logging {

class AsyncLogger;

/* ================= NullStream ================= */

// 空 buffer：丢弃所有输出
class NullBuffer : public std::streambuf {
public:
    int overflow(int c) override {
        return c;
    }
};

// 空 ostream：用于日志关闭时
class NullStream : public std::ostream {
public:
    NullStream() : std::ostream(&buffer_) {}
private:
    NullBuffer buffer_;
};

// 返回一个全局唯一的 NullStream 引用
inline std::ostream& nullStream()
{
    static NullStream ns;
    return ns;
}

/* ================= Logger ================= */

class Logger {
public:
    Logger(LogLevel level,
           const char* file,
           int line);

    ~Logger();

    // 返回真正的输出流
    std::ostream& stream() { return stream_; }

    static void setAsyncLogger(AsyncLogger* logger);
    static void clearAsyncLogger();

    static void setLogLevel(LogLevel level);
    static LogLevel logLevel();

private:
    void formatTime();

    LogLevel level_;
    const char* file_;
    int line_;
    std::ostringstream stream_;

    static std::atomic<AsyncLogger*> asyncLogger_;
    static std::atomic<LogLevel> globalLogLevel_;
};

} // namespace logging


/* ================= Logging Macros ================= */

#define LOG_TRACE \
    (logging::Logger::logLevel() <= logging::LogLevel::TRACE ? \
        logging::Logger(logging::LogLevel::TRACE, __FILE__, __LINE__).stream() : \
        logging::nullStream())

#define LOG_DEBUG \
    (logging::Logger::logLevel() <= logging::LogLevel::DEBUG ? \
        logging::Logger(logging::LogLevel::DEBUG, __FILE__, __LINE__).stream() : \
        logging::nullStream())

#define LOG_INFO \
    (logging::Logger::logLevel() <= logging::LogLevel::INFO ? \
        logging::Logger(logging::LogLevel::INFO, __FILE__, __LINE__).stream() : \
        logging::nullStream())

#define LOG_WARN \
    (logging::Logger::logLevel() <= logging::LogLevel::WARN ? \
        logging::Logger(logging::LogLevel::WARN, __FILE__, __LINE__).stream() : \
        logging::nullStream())

#define LOG_ERROR \
    (logging::Logger::logLevel() <= logging::LogLevel::ERROR ? \
        logging::Logger(logging::LogLevel::ERROR, __FILE__, __LINE__).stream() : \
        logging::nullStream())

// FATAL 永远输出，析构中 abort
#define LOG_FATAL \
    logging::Logger(logging::LogLevel::FATAL, __FILE__, __LINE__).stream()
