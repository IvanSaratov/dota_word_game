#ifndef _WIN32
#error "main_win32.cpp is only available on Windows"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "dk/app.hpp"
#include "dk/config.hpp"
#include "dk/dxgi_capture.hpp"
#include "dk/hotkeys.hpp"
#include "dk/hotkey_state.hpp"
#include "dk/ocr_recognizer.hpp"
#include "dk/region_selector.hpp"
#include "dk/session_log.hpp"
#include "dk/win32_input_sink.hpp"
#include "dk/window_locator.hpp"

#ifndef DK_APP_VERSION
#error "DK_APP_VERSION must contain the CMake project version"
#endif

namespace {

std::atomic_bool keep_running{true};
constexpr int maximum_consecutive_frame_errors = 5;

BOOL WINAPI console_control(DWORD) {
    keep_running = false;
    return TRUE;
}

std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    constexpr std::size_t maximum_path_characters = 32768;
    while (buffer.size() <= maximum_path_characters) {
        const DWORD copied = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "resolve executable path");
        }
        if (copied < buffer.size()) {
            return std::filesystem::path{
                std::wstring{buffer.data(), copied}}.parent_path();
        }
        if (buffer.size() == maximum_path_characters) {
            break;
        }
        buffer.resize(
            std::min(buffer.size() * 2, maximum_path_characters));
    }
    throw std::runtime_error("Executable path exceeds the Windows path limit.");
}

void write_bootstrap_failure(
    const std::string& message,
    std::ostream* file) {
    const std::string record = "Fatal error: " + message + '\n';
    std::cerr << record;
    if (file != nullptr) {
        *file << record;
        file->flush();
    }
}

std::optional<dk::Box> screen_region(HWND target, const dk::AppConfig& config) {
    const auto client = dk::WindowLocator::client_screen_bounds(target);
    if (!client || !config.region_configured || config.region.x < 0 ||
        config.region.y < 0 || config.region.width <= 0 ||
        config.region.height <= 0 ||
        config.region.right() > client->width ||
        config.region.bottom() > client->height) {
        return std::nullopt;
    }
    return dk::Box{
        client->x + config.region.x,
        client->y + config.region.y,
        config.region.width,
        config.region.height,
    };
}

class HotkeyController {
public:
    explicit HotkeyController(const dk::HotkeyConfig& config) {
        std::promise<void> startup;
        auto startup_result = startup.get_future();
        worker_ = std::jthread(
            [this, config, startup = std::move(startup)](
                std::stop_token stop_token) mutable {
                bool startup_reported = false;
                try {
                    dk::Hotkeys hotkeys{config.calibrate, config.toggle};
                    startup.set_value();
                    startup_reported = true;
                    while (!stop_token.stop_requested() && !quit_requested()) {
                        switch (hotkeys.poll(std::chrono::milliseconds{50})) {
                            case dk::HotkeyEvent::none:
                                break;
                            case dk::HotkeyEvent::calibrate:
                                request_calibration();
                                break;
                            case dk::HotkeyEvent::toggle:
                                toggle_processing();
                                break;
                            case dk::HotkeyEvent::quit:
                                request_quit();
                                break;
                        }
                    }
                } catch (...) {
                    if (!startup_reported) {
                        startup.set_exception(std::current_exception());
                        return;
                    }
                    {
                        std::lock_guard lock{failure_mutex_};
                        thread_failure_ = std::current_exception();
                    }
                    request_quit();
                }
            });
        startup_result.get();
    }

    [[nodiscard]] bool processing_enabled() const noexcept {
        return state_.processing_enabled();
    }

    [[nodiscard]] bool quit_requested() const noexcept {
        return state_.quit_requested();
    }

    [[nodiscard]] bool take_calibration_request() {
        return state_.take_calibration_request();
    }

    void finish_calibration() {
        state_.finish_calibration();
    }

    void stop_processing() {
        state_.stop_processing();
    }

    void rethrow_if_failed() const {
        std::exception_ptr failure;
        {
            std::lock_guard lock{failure_mutex_};
            failure = thread_failure_;
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

private:
    void request_calibration() {
        state_.request_calibration();
    }

    void toggle_processing() {
        state_.toggle_processing();
    }

    void request_quit() {
        state_.request_quit();
    }

    mutable std::mutex failure_mutex_;
    dk::HotkeyState state_;
    std::exception_ptr thread_failure_;
    std::jthread worker_;
};

struct Pipeline {
    Pipeline(const dk::AppConfig& config, HWND target, dk::Box capture_region,
             dk::LineRecognizer& recognizer,
             dk::Logger& logger,
             const dk::CancellationPredicate& cancellation)
        : capture_region(std::move(capture_region)),
          detector(config.detector),
          capture(capture_region),
          input(target, config.inter_key_delay_us, cancellation),
          app(config, capture, detector, recognizer, input, logger, cancellation) {}

    dk::Box capture_region;
    dk::CandidateDetector detector;
    dk::DxgiCapture capture;
    dk::Win32InputSink input;
    dk::App app;
};

std::unique_ptr<Pipeline> build_pipeline(
    const dk::AppConfig& config, HWND target, dk::LineRecognizer& recognizer,
    dk::Logger& logger,
    const dk::CancellationPredicate& cancellation) {
    const auto region = screen_region(target, config);
    if (!region) {
        throw std::runtime_error(
            "The configured region is invalid for the current game client bounds.");
    }
    return std::make_unique<Pipeline>(
        config, target, *region, recognizer, logger, cancellation);
}

dk::AppConfig load_startup_config() {
    const std::filesystem::path user_config{"config.json"};
    if (std::filesystem::exists(user_config)) {
        return dk::load_config(user_config);
    }
    return dk::load_config("config/default.json");
}

bool is_install_check(const int argc, wchar_t* argv[]) {
    return argc == 2 && std::wstring_view{argv[1]} == L"--check-install";
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleCtrlHandler(console_control, TRUE);
    try {
        const auto log_path =
            executable_directory() / L"dota-keyboard.log";
        dk::SessionLog session_log{log_path};
        std::unique_ptr<dk::Logger> logger;
        try {
            const bool install_check = is_install_check(argc, argv);
            auto config = install_check ? dk::load_config("config.json")
                                        : load_startup_config();
            logger = std::make_unique<dk::Logger>(
                config.log_level,
                std::cout,
                std::cerr,
                session_log.sink());
            auto& runtime_logger = *logger;
            runtime_logger.write(
                dk::LogLevel::info,
                "Dota Keyboard v" + std::string{DK_APP_VERSION} +
                    " started; log_level=" +
                    std::string{
                        dk::configured_log_level_name(config.log_level)} +
                    "; log_path=" + log_path.string());
            if (!session_log.is_open()) {
                runtime_logger.write(
                    dk::LogLevel::warning,
                    "Unable to open session log at " + log_path.string() +
                        "; continuing with console logging.");
            }
            runtime_logger.write(
                dk::LogLevel::info,
                std::string{config.live_input ? "LIVE INPUT ENABLED"
                                              : "DRY RUN"} +
                    "; F7 calibrates; F8 starts or stops processing.");

            dk::OcrRecognizer recognizer{
                "assets/models/en_PP-OCRv5_rec_mobile_infer.onnx",
                "assets/models/ppocrv5_en_dict.txt",
            };
            if (is_install_check(argc, argv)) {
                runtime_logger.write(
                    dk::LogLevel::info, "Install check succeeded.");
                return 0;
            }
            HotkeyController control{config.hotkeys};
            dk::CancellationPredicate cancellation = [&control] {
                return !keep_running.load(std::memory_order_acquire) ||
                       control.quit_requested() ||
                       !control.processing_enabled();
            };

            HWND target{};
            if (config.region_configured && !config.window_title.empty()) {
                const auto foreground = dk::WindowLocator::foreground();
                if (foreground &&
                    foreground->title == config.window_title) {
                    target = foreground->handle;
                } else {
                    runtime_logger.write(
                        dk::LogLevel::info,
                        "Saved game window is not foreground; press F7 to bind it.");
                }
            } else if (config.region_configured) {
                runtime_logger.write(
                    dk::LogLevel::info,
                    "Saved window title is empty; press F7 to bind the game.");
            } else {
                runtime_logger.write(
                    dk::LogLevel::info,
                    "No calibrated region; focus the game and press F7.");
            }

            bool announced_processing{};
            int consecutive_frame_errors{};
            std::unique_ptr<Pipeline> pipeline;
            dk::MetricsSchedule metrics_schedule{
                std::chrono::steady_clock::now()};

            while (keep_running.load(std::memory_order_acquire) &&
                   !control.quit_requested()) {
                control.rethrow_if_failed();

                if (control.take_calibration_request()) {
                    announced_processing = false;
                    consecutive_frame_errors = 0;
                    pipeline.reset();

                    const auto binding = dk::WindowLocator::foreground();
                    if (!binding) {
                        runtime_logger.write(
                            dk::LogLevel::warning,
                            "F7: no usable foreground game window.");
                        control.finish_calibration();
                        continue;
                    }
                    const auto region = dk::RegionSelector::select(
                        binding->handle, binding->client_bounds);
                    if (!region) {
                        control.finish_calibration();
                        runtime_logger.write(
                            dk::LogLevel::info,
                            std::string{"Calibration cancelled"} +
                                (control.processing_enabled()
                                     ? "; honoring the newer F8 request."
                                     : "; processing remains stopped."));
                        continue;
                    }

                    auto calibrated = config;
                    calibrated.window_title = binding->title;
                    calibrated.region = *region;
                    calibrated.region_configured = true;
                    dk::save_config("config.json", calibrated);
                    config = calibrated;
                    target = binding->handle;
                    control.finish_calibration();
                    metrics_schedule.reset(
                        std::chrono::steady_clock::now());
                    runtime_logger.write(
                        dk::LogLevel::info,
                        std::string{"Calibration saved"} +
                            (control.processing_enabled()
                                 ? "; processing will resume with rebuilt capture."
                                 : "."));
                    continue;
                }

                if (control.processing_enabled() != announced_processing) {
                    if (!control.processing_enabled()) {
                        announced_processing = false;
                        runtime_logger.write(
                            dk::LogLevel::info,
                            "STOPPED (" +
                                std::string{config.live_input ? "live mode"
                                                              : "dry run"} +
                                " unchanged)");
                    } else if (!target || !IsWindow(target)) {
                        runtime_logger.write(
                            dk::LogLevel::warning,
                            "Cannot start: calibrate a live game window with F7.");
                        control.stop_processing();
                    } else {
                        pipeline.reset();
                        pipeline = build_pipeline(
                            config,
                            target,
                            recognizer,
                            runtime_logger,
                            cancellation);
                        announced_processing = true;
                        consecutive_frame_errors = 0;
                        metrics_schedule.reset(
                            std::chrono::steady_clock::now());
                        runtime_logger.write(
                            dk::LogLevel::info,
                            "RUNNING (" +
                                std::string{config.live_input ? "LIVE INPUT"
                                                              : "DRY RUN"} +
                                ")");
                    }
                }

                const auto now = std::chrono::steady_clock::now();
                if (pipeline &&
                    metrics_schedule.take_if_due(
                        now, control.processing_enabled())) {
                    runtime_logger.write(
                        dk::LogLevel::info,
                        dk::format_latency_summary(
                            pipeline->app.metrics().summary()));
                }
                if (target && !IsWindow(target)) {
                    runtime_logger.write(
                        dk::LogLevel::error,
                        "Game window was destroyed; stopping.");
                    break;
                }
                if (!target) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds{10});
                    continue;
                }
                if (!control.processing_enabled()) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds{10});
                    continue;
                }
                const auto current_region = screen_region(target, config);
                if (!current_region) {
                    runtime_logger.write(
                        dk::LogLevel::warning,
                        "Game client region became invalid; processing paused. "
                        "Restore the window geometry or recalibrate with F7, "
                        "then press F8.");
                    control.stop_processing();
                    announced_processing = false;
                    consecutive_frame_errors = 0;
                    pipeline.reset();
                    continue;
                }
                if (!pipeline ||
                    pipeline->capture_region != *current_region) {
                    pipeline.reset();
                    pipeline = build_pipeline(
                        config,
                        target,
                        recognizer,
                        runtime_logger,
                        cancellation);
                    metrics_schedule.reset(now);
                    runtime_logger.write(
                        dk::LogLevel::info,
                        "Game window moved; capture rebuilt before processing.");
                }

                try {
                    if (!pipeline->app.process_one_frame()) {
                        runtime_logger.write(
                            dk::LogLevel::warning,
                            "Input was blocked or partial; stopping.");
                        control.stop_processing();
                        break;
                    }
                    consecutive_frame_errors = 0;
                } catch (const std::exception& error) {
                    ++consecutive_frame_errors;
                    std::ostringstream message;
                    message << "Frame processing error "
                            << consecutive_frame_errors << '/'
                            << maximum_consecutive_frame_errors << ": "
                            << error.what()
                            << "; discarding the frame and rebuilding capture.";
                    runtime_logger.write(
                        dk::LogLevel::warning, message.str());
                    pipeline.reset();
                    if (consecutive_frame_errors >=
                        maximum_consecutive_frame_errors) {
                        runtime_logger.write(
                            dk::LogLevel::error,
                            "Too many consecutive frame errors; processing "
                            "paused. Press F8 to retry or F7 to recalibrate.");
                        control.stop_processing();
                        announced_processing = false;
                    }
                    continue;
                }
            }

            control.rethrow_if_failed();
            return 0;
        } catch (const std::exception& error) {
            if (logger) {
                logger->write(
                    dk::LogLevel::error,
                    "Fatal error: " + std::string{error.what()});
            } else {
                write_bootstrap_failure(
                    error.what(), session_log.sink());
            }
            return 1;
        }
    } catch (const std::exception& error) {
        write_bootstrap_failure(error.what(), nullptr);
        return 1;
    }
}
