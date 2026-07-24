#pragma once
#include <string>
#include <string_view>
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <format>
#include <source_location>

namespace agent {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
    Off   = 6
};

inline constexpr std::string_view level_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "?????";
    }
}

inline constexpr std::string_view level_color(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "\033[90m";    // Gray
        case LogLevel::Debug: return "\033[36m";    // Cyan
        case LogLevel::Info:  return "\033[32m";    // Green
        case LogLevel::Warn:  return "\033[33m";    // Yellow
        case LogLevel::Error: return "\033[31m";    // Red
        case LogLevel::Fatal: return "\033[35m";    // Magenta
        default:              return "\033[0m";
    }
}

inline constexpr const char* level_color_reset = "\033[0m";

class Logger {
public:
    static Logger& instance() {
        static Logger s_instance;
        return s_instance;
    }

    void set_level(LogLevel level) { m_level = level; }
    LogLevel level() const { return m_level; }

    void set_module_filter(std::string_view module) { m_module_filter = module; }

    void log(LogLevel level,
             std::string_view module,
             std::string_view message,
             std::string_view file = "",
             int line = 0) {
        if (level < m_level) return;
        if (!m_module_filter.empty() && module != m_module_filter) return;

        std::lock_guard lock(m_mutex);

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif

        auto& os = (level >= LogLevel::Error) ? std::cerr : std::cout;

        os << level_color(level)
           << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "]"
           << " [" << level_string(level) << "]"
           << " [" << module << "]"
           << " " << message;

        if (!file.empty()) {
            os << " (" << file << ":" << line << ")";
        }

        os << level_color_reset << std::endl;
    }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel m_level = LogLevel::Info;
    std::string m_module_filter;
    std::mutex m_mutex;
};

// ── Convenience macros ─────────────────────────────────────────────
#define LOG_TRACE(module, message) \
    agent::Logger::instance().log(agent::LogLevel::Trace, module, message, __FILE__, __LINE__)

#define LOG_DEBUG(module, message) \
    agent::Logger::instance().log(agent::LogLevel::Debug, module, message, __FILE__, __LINE__)

#define LOG_INFO(module, message) \
    agent::Logger::instance().log(agent::LogLevel::Info, module, message, __FILE__, __LINE__)

#define LOG_WARN(module, message) \
    agent::Logger::instance().log(agent::LogLevel::Warn, module, message, __FILE__, __LINE__)

#define LOG_ERROR(module, message) \
    agent::Logger::instance().log(agent::LogLevel::Error, module, message, __FILE__, __LINE__)

#define LOG_FATAL(module, message) \
    agent::Logger::instance().log(agent::LogLevel::Fatal, module, message, __FILE__, __LINE__)

} // namespace agent
