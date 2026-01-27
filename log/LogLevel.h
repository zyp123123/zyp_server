#pragma once

namespace logging {

enum class LogLevel {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

const char* toString(LogLevel level);

} // namespace log
