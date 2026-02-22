#include "core/logger.hpp"
#include <chrono>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <cstdlib>

namespace wowee {
namespace core {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::ensureFile() {
    if (fileReady) return;
    fileReady = true;
    if (const char* flushMs = std::getenv("WOWEE_LOG_FLUSH_MS")) {
        char* end = nullptr;
        unsigned long parsed = std::strtoul(flushMs, &end, 10);
        if (end != flushMs && parsed <= 10000ul) {
            flushIntervalMs_ = static_cast<uint32_t>(parsed);
        }
    }
    std::error_code ec;
    std::filesystem::create_directories("logs", ec);
    fileStream.open("logs/wowee.log", std::ios::out | std::ios::trunc);
    lastFlushTime_ = std::chrono::steady_clock::now();
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < minLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);
    ensureFile();

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    // Format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message
    std::ostringstream line;
    line << "["
         << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
         << "." << std::setfill('0') << std::setw(3) << ms.count()
         << "] [";

    switch (level) {
        case LogLevel::DEBUG:   line << "DEBUG"; break;
        case LogLevel::INFO:    line << "INFO "; break;
        case LogLevel::WARNING: line << "WARN "; break;
        case LogLevel::ERROR:   line << "ERROR"; break;
        case LogLevel::FATAL:   line << "FATAL"; break;
    }

    line << "] " << message;

    std::cout << line.str() << '\n';
    if (fileStream.is_open()) {
        fileStream << line.str() << '\n';
        bool shouldFlush = (level >= LogLevel::WARNING);
        if (!shouldFlush) {
            auto nowSteady = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(nowSteady - lastFlushTime_).count();
            shouldFlush = (elapsedMs >= static_cast<long long>(flushIntervalMs_));
            if (shouldFlush) {
                lastFlushTime_ = nowSteady;
            }
        }
        if (shouldFlush) {
            fileStream.flush();
        }
    }
}

void Logger::setLogLevel(LogLevel level) {
    minLevel = level;
}

} // namespace core
} // namespace wowee
