#include "Logger.h"
#include "AsyncLogger.h"
#include <iomanip>
#include <ctime>
#include <unistd.h>

namespace logging {

std::atomic<AsyncLogger*> Logger::asyncLogger_{nullptr};
std::atomic<LogLevel> Logger::globalLogLevel_{LogLevel::INFO};

const char* toString(LogLevel level)
{
    switch (level) {
    case LogLevel::TRACE: return "TRACE";
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO ";
    case LogLevel::WARN:  return "WARN ";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::FATAL: return "FATAL";
    default:              return "UNKNOWN";
    }
}

Logger::Logger(LogLevel level,
               const char* file,
               int line)
    : level_(level),
      file_(file),
      line_(line)
{
    formatTime();

    stream_ << "[" << toString(level_) << "] "
            << "[tid " << std::this_thread::get_id() << "] ";
}

Logger::~Logger()
{
    stream_ << " (" << file_ << ":" << line_ << ")\n";

    AsyncLogger* logger =
        asyncLogger_.load(std::memory_order_acquire);

    if (logger) {
        logger->append(stream_.str().c_str(),
                       stream_.str().size());
    } else {
        ::fputs(stream_.str().c_str(), stderr);
        ::fflush(stderr);
    }

    if (level_ == LogLevel::FATAL) {
        // 确保异步日志尽量写出
        if (logger) {
            // 直接同步写 stderr，保证一定能看到
            ::fputs(stream_.str().c_str(), stderr);
            ::fflush(stderr);

            // 再尽力通知异步线程
            logger->notify();
        }
        ::abort();  // 直接终止进程
    }
}

void Logger::setAsyncLogger(AsyncLogger* logger)
{
    asyncLogger_.store(logger, std::memory_order_release);
}

void Logger::clearAsyncLogger()
{
    asyncLogger_.store(nullptr, std::memory_order_release);
}

void Logger::formatTime()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto seconds = time_point_cast<std::chrono::seconds>(now);
    auto microseconds =
        duration_cast<std::chrono::microseconds>(now - seconds).count();

    std::time_t t = system_clock::to_time_t(seconds);
    std::tm tm;
    localtime_r(&t, &tm);

    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%4d-%02d-%02d %02d:%02d:%02d.%06ld ",
                  tm.tm_year + 1900,
                  tm.tm_mon + 1,
                  tm.tm_mday,
                  tm.tm_hour,
                  tm.tm_min,
                  tm.tm_sec,
                  microseconds);

    stream_ << buf;
}

void Logger::setLogLevel(LogLevel level)
{
    globalLogLevel_.store(level, std::memory_order_release);
}

LogLevel Logger::logLevel()
{
    return globalLogLevel_.load(std::memory_order_acquire);
}
} // namespace log
