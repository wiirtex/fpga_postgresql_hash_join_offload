#include "logger.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>   // GetLocalTime for sub-second precision
#  ifdef ERROR
#    undef ERROR   // windows.h defines ERROR as a macro — conflicts with LogLevel::ERROR
#  endif
#endif

// ─── Time helpers ─────────────────────────────────────────────────────────────

// Fill buf (len ≥ 16) with "HH:MM:SS.mmm".
static void format_timestamp(char* buf, size_t len) {
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buf, len, "%02d:%02d:%02d.%03d",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_local;
    localtime_r(&ts.tv_sec, &tm_local);
    int ms = static_cast<int>(ts.tv_nsec / 1'000'000);
    snprintf(buf, len, "%02d:%02d:%02d.%03d",
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec, ms);
#endif
}

// ─── Level helpers ────────────────────────────────────────────────────────────

static const char* level_tag(LogLevel l) {
    switch (l) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO ";
    case LogLevel::WARN:  return "WARN ";
    case LogLevel::ERROR: return "ERROR";
    default:              return "?????";
    }
}

static LogLevel parse_level_env() {
    const char* env = std::getenv("FPGA_LOG_LEVEL");
    if (!env) return LogLevel::INFO;

    if (strcmp(env, "DEBUG") == 0) return LogLevel::DEBUG;
    if (strcmp(env, "INFO")  == 0) return LogLevel::INFO;
    if (strcmp(env, "WARN")  == 0) return LogLevel::WARN;
    if (strcmp(env, "ERROR") == 0) return LogLevel::ERROR;
    if (strcmp(env, "NONE")  == 0) return LogLevel::NONE;

    // Unknown value — default INFO.
    return LogLevel::INFO;
}

// ─── Logger implementation ────────────────────────────────────────────────────

Logger::Logger() : min_level_(parse_level_env()), out_(&std::cerr) {}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(LogLevel min_level) {
    min_level_ = min_level;
}

void Logger::log(LogLevel lvl, const char* comp, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(lvl, comp, fmt, ap);
    va_end(ap);
}

void Logger::vlog(LogLevel lvl, const char* comp, const char* fmt, va_list ap) {
    if (lvl < min_level_) return;

    char ts[20];
    format_timestamp(ts, sizeof(ts));

    // Format the user message.
    char msg[512];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    // "[HH:MM:SS.mmm] [LEVEL] Component: message\n"
    fprintf(stderr, "[%s] [%s] %s: %s\n", ts, level_tag(lvl), comp, msg);
}
