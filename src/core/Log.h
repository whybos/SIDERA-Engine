#pragma once

#include <format>
#include <string_view>

namespace sidera {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Log {
public:
    static void write(LogLevel level, std::string_view category, std::string_view message);

    template <typename... Args>
    static void debug(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Debug, category, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void info(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Info, category, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void warn(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Warn, category, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void error(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        write(LogLevel::Error, category, std::format(fmt, std::forward<Args>(args)...));
    }
};

} // namespace sidera

#define SIDERA_LOG_DEBUG(cat, ...) ::sidera::Log::debug(cat, __VA_ARGS__)
#define SIDERA_LOG_INFO(cat, ...)  ::sidera::Log::info(cat, __VA_ARGS__)
#define SIDERA_LOG_WARN(cat, ...)  ::sidera::Log::warn(cat, __VA_ARGS__)
#define SIDERA_LOG_ERROR(cat, ...) ::sidera::Log::error(cat, __VA_ARGS__)
