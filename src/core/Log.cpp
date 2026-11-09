#include "core/Log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace sidera {

namespace {
std::mutex g_logMutex;

constexpr std::string_view levelToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKN ";
}
} // namespace

void Log::write(LogLevel level, std::string_view category, std::string_view message) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t nowTime = system_clock::to_time_t(now);

    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &nowTime);
#else
    localtime_r(&nowTime, &localTm);
#endif

    std::ostringstream timeStream;
    timeStream << std::put_time(&localTm, "%H:%M:%S")
               << '.' << std::setfill('0') << std::setw(3) << ms.count();

    std::scoped_lock lock(g_logMutex);
    std::ostream& out = (level == LogLevel::Error || level == LogLevel::Warn) ? std::cerr : std::cout;
    out << "[" << timeStream.str() << "] [" << levelToString(level) << "] [" << category << "] "
        << message << '\n';
    out.flush();
}

} // namespace sidera
