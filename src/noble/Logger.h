#include <format>
#include <iostream>
#include <mutex>
#include <chrono>

#ifndef LOG_LEVEL
#define LOG_LEVEL LogLevel::Trace
#endif


constexpr const char* COL_RESET = "\033[0m";
constexpr const char* COL_RED = "\033[31m";
constexpr const char* COL_YELLOW = "\033[33m";
constexpr const char* COL_BLUE = "\033[34m";
constexpr const char* COL_GREEN = "\033[32m";
constexpr const char* COL_GRAY = "\033[90m";
constexpr const char* COL_MAGENTA = "\033[35m";

enum class LogLevel : uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

constexpr bool enabled(LogLevel level) {
    return static_cast<uint8_t>(level) >=
        static_cast<uint8_t>(LOG_LEVEL);
}

class Logger {
public:
    template<LogLevel Level, typename... Args>
    static void log(std::format_string<Args...> fmt, Args&&... args) {
        if constexpr(!enabled(Level)) return;

        std::lock_guard lock(mutex_);

        auto msg = std::format(fmt, std::forward<Args>(args)...);
        std::cout << colorPrefix(Level) << msg << COL_RESET << "\n";
    }

private:

    static std::string colorPrefix(LogLevel level) {
        switch(level) {
            case LogLevel::Trace: return COL_GREEN;
            case LogLevel::Debug: return COL_BLUE;
            case LogLevel::Info:  return COL_RESET;
            case LogLevel::Warn:  return COL_YELLOW;
            case LogLevel::Error: return COL_RED;
            case LogLevel::Fatal: return COL_MAGENTA;
        }
        return COL_RESET;
    }

    static inline std::mutex mutex_;
};

#define LOG_TRACE(...) Logger::log<LogLevel::Trace>(__VA_ARGS__)
#define LOG_DEBUG(...) Logger::log<LogLevel::Debug>(__VA_ARGS__)
#define LOG_INFO(...)  Logger::log<LogLevel::Info>(__VA_ARGS__)
#define LOG_WARN(...)  Logger::log<LogLevel::Warn>(__VA_ARGS__)
#define LOG_ERROR(...) Logger::log<LogLevel::Error>(__VA_ARGS__)
#define LOG_FATAL(...) Logger::log<LogLevel::Fatal>(__VA_ARGS__)
