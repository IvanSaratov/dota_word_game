#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <ostream>
#include <string_view>

namespace dk {

enum class LogLevel {
    debug,
    info,
    warning,
    error,
};

using LogClock =
    std::function<std::chrono::system_clock::time_point()>;

class Logger {
public:
    Logger(
        LogLevel minimum,
        std::ostream& out,
        std::ostream& err,
        std::ostream* file = nullptr,
        LogClock clock = std::chrono::system_clock::now);

    void write(LogLevel level, std::string_view message);
    [[nodiscard]] bool enabled(LogLevel level) const noexcept;

private:
    LogLevel minimum_;
    std::ostream& out_;
    std::ostream& err_;
    std::ostream* file_;
    LogClock clock_;
    std::mutex mutex_;
};

[[nodiscard]] std::string_view log_level_name(LogLevel level) noexcept;

}  // namespace dk
