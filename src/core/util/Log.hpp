#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace vd::log {

enum class Level { Info, Warn, Error };

namespace detail {

inline std::mutex& mutex() {
    static std::mutex m;
    return m;
}

inline void print(Level lvl, const char* fmt, va_list ap) {
    const char* tag = "INFO ";
    const char* color = "\x1b[90m";
    switch (lvl) {
        case Level::Warn:  tag = "WARN "; color = "\x1b[33m"; break;
        case Level::Error: tag = "ERROR"; color = "\x1b[31m"; break;
        default: break;
    }
    std::lock_guard<std::mutex> lk(mutex());
    std::fputs(color, stdout);
    std::fputs(tag, stdout);
    std::fputs("  ", stdout);
    std::vprintf(fmt, ap);
    std::fputs("\x1b[0m\n", stdout);
    std::fflush(stdout);
}

} // namespace detail

inline void info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    detail::print(Level::Info, fmt, ap);
    va_end(ap);
}

inline void warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    detail::print(Level::Warn, fmt, ap);
    va_end(ap);
}

inline void error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    detail::print(Level::Error, fmt, ap);
    va_end(ap);
}

} // namespace vd::log
