#include "dk/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace dk {
namespace {

std::tm local_time(const std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

std::string format_record(
    const std::chrono::system_clock::time_point timestamp,
    const LogLevel level,
    std::string_view message) {
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch());
    const auto fractional =
        milliseconds % std::chrono::milliseconds{1000};
    const auto time = std::chrono::system_clock::to_time_t(timestamp);
    const auto parts = local_time(time);

    std::ostringstream record;
    record << '[' << std::put_time(&parts, "%Y-%m-%d %H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << fractional.count() << "] ["
           << log_level_name(level) << "] " << message << '\n';
    return record.str();
}

}  // namespace

Logger::Logger(
    const LogLevel minimum,
    std::ostream& out,
    std::ostream& err,
    std::ostream* file,
    LogClock clock)
    : minimum_(minimum),
      out_(out),
      err_(err),
      file_(file),
      clock_(std::move(clock)) {}

void Logger::write(const LogLevel level, const std::string_view message) {
    if (!enabled(level)) {
        return;
    }

    std::lock_guard lock{mutex_};
    const auto record = format_record(clock_(), level, message);
    auto& console =
        level >= LogLevel::warning ? err_ : out_;
    console << record;
    if (level >= LogLevel::warning) {
        console.flush();
    }
    if (file_ != nullptr) {
        *file_ << record;
        file_->flush();
    }
}

bool Logger::enabled(const LogLevel level) const noexcept {
    return level >= minimum_;
}

std::string_view log_level_name(const LogLevel level) noexcept {
    switch (level) {
        case LogLevel::debug:
            return "DEBUG";
        case LogLevel::info:
            return "INFO";
        case LogLevel::warning:
            return "WARNING";
        case LogLevel::error:
            return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace dk
