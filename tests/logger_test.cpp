#include "dk/logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "logger_test failure: " << message << '\n';
        std::exit(1);
    }
}

void use_utc_timezone() {
#ifdef _WIN32
    _putenv_s("TZ", "UTC");
    _tzset();
#else
    setenv("TZ", "UTC", 1);
    tzset();
#endif
}

dk::LogClock fixed_clock() {
    const auto timestamp =
        std::chrono::sys_days{std::chrono::year{2026} / 7 / 26} +
        23h + 24min + 12s + 123ms;
    return [timestamp] { return timestamp; };
}

class SyncCountingBuffer final : public std::stringbuf {
public:
    int sync() override {
        ++sync_count;
        return std::stringbuf::sync();
    }

    int sync_count{};
};

void test_names_filtering_and_routing() {
    require(dk::log_level_name(dk::LogLevel::debug) == "DEBUG",
            "debug name must be uppercase");
    require(dk::log_level_name(dk::LogLevel::info) == "INFO",
            "info name must be uppercase");
    require(dk::log_level_name(dk::LogLevel::warning) == "WARNING",
            "warning name must be uppercase");
    require(dk::log_level_name(dk::LogLevel::error) == "ERROR",
            "error name must be uppercase");

    std::ostringstream out;
    std::ostringstream err;
    std::ostringstream file;
    dk::Logger logger{
        dk::LogLevel::info, out, err, &file, fixed_clock()};

    require(!logger.enabled(dk::LogLevel::debug),
            "info logger must filter debug");
    require(logger.enabled(dk::LogLevel::info),
            "info logger must enable info");
    logger.write(dk::LogLevel::debug, "hidden");
    logger.write(dk::LogLevel::info, "ready");
    logger.write(dk::LogLevel::warning, "careful");
    logger.write(dk::LogLevel::error, "failed");

    const std::string prefix{"[2026-07-26 23:24:12.123] "};
    require(out.str() == prefix + "[INFO] ready\n",
            "info must use stdout with exact format");
    require(err.str() ==
                prefix + "[WARNING] careful\n" +
                prefix + "[ERROR] failed\n",
            "warning and error must use stderr with exact format");
    require(file.str() ==
                prefix + "[INFO] ready\n" +
                prefix + "[WARNING] careful\n" +
                prefix + "[ERROR] failed\n",
            "file must receive every enabled record exactly once");
}

void test_file_is_flushed_after_each_record() {
    std::ostringstream out;
    std::ostringstream err;
    SyncCountingBuffer buffer;
    std::ostream file{&buffer};
    dk::Logger logger{
        dk::LogLevel::info, out, err, &file, fixed_clock()};

    logger.write(dk::LogLevel::info, "first");
    require(buffer.sync_count == 1, "first file record must flush immediately");
    logger.write(dk::LogLevel::warning, "second");
    require(buffer.sync_count == 2, "second file record must flush immediately");
}

void test_concurrent_records_remain_complete() {
    std::ostringstream out;
    std::ostringstream err;
    std::ostringstream file;
    dk::Logger logger{
        dk::LogLevel::debug, out, err, &file, fixed_clock()};

    std::vector<std::thread> writers;
    for (int index = 0; index < 16; ++index) {
        writers.emplace_back([index, &logger] {
            logger.write(
                dk::LogLevel::debug, "worker-" + std::to_string(index));
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    const auto count_lines = [](const std::string& text) {
        return static_cast<int>(
            std::count(text.begin(), text.end(), '\n'));
    };
    require(count_lines(out.str()) == 16,
            "console must contain 16 complete records");
    require(count_lines(file.str()) == 16,
            "file must contain 16 complete records");
    for (int index = 0; index < 16; ++index) {
        const auto token = "[DEBUG] worker-" + std::to_string(index) + "\n";
        require(out.str().find(token) != std::string::npos,
                "console record must not be split");
        require(file.str().find(token) != std::string::npos,
                "file record must not be split");
    }
}

void test_clock_and_record_serialization_share_the_write_lock() {
    std::ostringstream out;
    std::ostringstream err;
    std::atomic<int> active_calls{};
    std::atomic<int> maximum_active_calls{};
    std::atomic<int> timestamp_index{};
    const auto epoch = std::chrono::system_clock::time_point{};
    const dk::LogClock probing_clock = [&] {
        const auto active = ++active_calls;
        auto maximum = maximum_active_calls.load();
        while (active > maximum &&
               !maximum_active_calls.compare_exchange_weak(maximum, active)) {
        }
        std::this_thread::sleep_for(5ms);
        const auto index = timestamp_index++;
        --active_calls;
        return epoch + std::chrono::milliseconds{index};
    };
    dk::Logger logger{
        dk::LogLevel::debug, out, err, nullptr, probing_clock};

    constexpr int writer_count = 16;
    std::latch start{writer_count};
    std::vector<std::thread> writers;
    for (int index = 0; index < writer_count; ++index) {
        writers.emplace_back([index, &logger, &start] {
            start.count_down();
            start.wait();
            logger.write(
                dk::LogLevel::debug, "serialized-" + std::to_string(index));
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    require(maximum_active_calls == 1,
            "stateful clock calls must be serialized with record writes");
}

}  // namespace

int main() {
    use_utc_timezone();
    test_names_filtering_and_routing();
    test_file_is_flushed_after_each_record();
    test_concurrent_records_remain_complete();
    test_clock_and_record_serialization_share_the_write_lock();
}
