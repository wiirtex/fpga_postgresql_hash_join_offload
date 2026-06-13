#pragma once
// host/include/logger.hpp — Singleton logger with level filtering.
//
// NOTE: windows.h defines ERROR as a macro (= 0).  We undef it here so that
// LogLevel::ERROR compiles when this header is included after <windows.h>.
//
// Usage:
//   LOG_INFO("FpgaClient", "CONFIGURE sent inner=%u outer=%u", ic, oc);
//   LOG_DEBUG("UartTransport", "send %zu bytes  %s", len, hex);
//
// Default level: INFO.  Override at startup via env var FPGA_LOG_LEVEL=DEBUG|INFO|WARN|ERROR.

#include <cstdarg>
#include <iosfwd>

#ifdef ERROR
#  undef ERROR  // windows.h defines ERROR as a macro
#endif

enum class LogLevel : int { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel min_level);
    LogLevel level() const { return min_level_; }

    // Printf-style log call.  comp is a short component tag, e.g. "UartTransport".
    void log(LogLevel lvl, const char* comp, const char* fmt, ...);
    void vlog(LogLevel lvl, const char* comp, const char* fmt, va_list ap);

private:
    Logger();
    LogLevel     min_level_ = LogLevel::INFO;
    std::ostream* out_      = nullptr;  // set in constructor
};

// ── Macros ────────────────────────────────────────────────────────────────────

#define LOG_DEBUG(comp, ...) \
    do { if (Logger::instance().level() <= LogLevel::DEBUG) \
             Logger::instance().log(LogLevel::DEBUG, comp, __VA_ARGS__); } while(0)

#define LOG_INFO(comp, ...) \
    do { if (Logger::instance().level() <= LogLevel::INFO)  \
             Logger::instance().log(LogLevel::INFO,  comp, __VA_ARGS__); } while(0)

#define LOG_WARN(comp, ...) \
    do { if (Logger::instance().level() <= LogLevel::WARN)  \
             Logger::instance().log(LogLevel::WARN,  comp, __VA_ARGS__); } while(0)

#define LOG_ERROR(comp, ...) \
    do { if (Logger::instance().level() <= LogLevel::ERROR) \
             Logger::instance().log(LogLevel::ERROR, comp, __VA_ARGS__); } while(0)
