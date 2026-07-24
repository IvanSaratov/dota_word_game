#ifndef _WIN32
#error "main_win32.cpp is only available on Windows"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include "dk/app.hpp"
#include "dk/config.hpp"
#include "dk/dxgi_capture.hpp"
#include "dk/hotkeys.hpp"
#include "dk/ocr_recognizer.hpp"
#include "dk/region_selector.hpp"
#include "dk/win32_input_sink.hpp"
#include "dk/window_locator.hpp"

namespace {

std::atomic_bool keep_running{true};
constexpr int maximum_consecutive_frame_errors = 5;

BOOL WINAPI console_control(DWORD) {
    keep_running = false;
    return TRUE;
}

void print_stage(const char* name, const dk::StageSummary& stage) {
    std::cout << name << ": n=" << stage.count << " mean=" << std::fixed
              << std::setprecision(2) << stage.mean_ms << "ms median="
              << stage.median_ms << "ms p95=" << stage.p95_ms << "ms\n";
}

void print_metrics(const dk::LatencyMetrics& metrics) {
    const auto summary = metrics.summary();
    std::cout << "\nLatency summary\n";
    print_stage("capture", summary.capture);
    print_stage("detect ", summary.detect);
    print_stage("ocr    ", summary.ocr);
    print_stage("total  ", summary.total);
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

struct CalibrationRequest {
    bool resume_processing{};
    std::uint64_t toggle_generation{};
};

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
        return processing_enabled_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool quit_requested() const noexcept {
        return quit_requested_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<CalibrationRequest> take_calibration_request() {
        std::lock_guard lock{state_mutex_};
        if (!calibration_requested_.exchange(false, std::memory_order_acq_rel)) {
            return std::nullopt;
        }
        return CalibrationRequest{
            calibration_resume_processing_,
            calibration_toggle_generation_,
        };
    }

    void finish_calibration(
        const CalibrationRequest& request, bool completed) {
        std::lock_guard lock{state_mutex_};
        if (toggle_generation_ == request.toggle_generation) {
            processing_enabled_.store(
                completed && request.resume_processing,
                std::memory_order_release);
        }
    }

    void stop_processing() {
        std::lock_guard lock{state_mutex_};
        processing_enabled_.store(false, std::memory_order_release);
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
        std::lock_guard lock{state_mutex_};
        if (calibration_requested_.load(std::memory_order_acquire)) {
            processing_enabled_.store(false, std::memory_order_release);
            return;
        }
        calibration_resume_processing_ =
            processing_enabled_.exchange(false, std::memory_order_acq_rel);
        calibration_toggle_generation_ = toggle_generation_;
        calibration_requested_.store(true, std::memory_order_release);
    }

    void toggle_processing() {
        std::lock_guard lock{state_mutex_};
        ++toggle_generation_;
        processing_enabled_.store(
            !processing_enabled_.load(std::memory_order_acquire),
            std::memory_order_release);
    }

    void request_quit() {
        std::lock_guard lock{state_mutex_};
        processing_enabled_.store(false, std::memory_order_release);
        quit_requested_.store(true, std::memory_order_release);
    }

    mutable std::mutex state_mutex_;
    mutable std::mutex failure_mutex_;
    std::atomic_bool processing_enabled_{false};
    std::atomic_bool calibration_requested_{false};
    std::atomic_bool quit_requested_{false};
    bool calibration_resume_processing_{};
    std::uint64_t toggle_generation_{};
    std::uint64_t calibration_toggle_generation_{};
    std::exception_ptr thread_failure_;
    std::jthread worker_;
};

struct Pipeline {
    Pipeline(const dk::AppConfig& config, HWND target, dk::Box capture_region,
             dk::LineRecognizer& recognizer,
             const dk::CancellationPredicate& cancellation)
        : capture_region(std::move(capture_region)),
          detector(config.detector),
          capture(capture_region),
          input(target, config.inter_key_delay_us, cancellation),
          app(config, capture, detector, recognizer, input, cancellation) {}

    dk::Box capture_region;
    dk::CandidateDetector detector;
    dk::DxgiCapture capture;
    dk::Win32InputSink input;
    dk::App app;
};

std::unique_ptr<Pipeline> build_pipeline(
    const dk::AppConfig& config, HWND target, dk::LineRecognizer& recognizer,
    const dk::CancellationPredicate& cancellation) {
    const auto region = screen_region(target, config);
    if (!region) {
        throw std::runtime_error(
            "The configured region is invalid for the current game client bounds.");
    }
    return std::make_unique<Pipeline>(
        config, target, *region, recognizer, cancellation);
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
        const bool install_check = is_install_check(argc, argv);
        auto config = install_check ? dk::load_config("config.json") : load_startup_config();
        std::cout << "\n========================================\n"
                  << (config.live_input ? "       LIVE INPUT ENABLED\n"
                                        : "             DRY RUN\n")
                  << "========================================\n"
                  << "F7 calibrates. F8 starts or stops processing.\n";

        dk::OcrRecognizer recognizer{
            "assets/models/en_PP-OCRv5_rec_mobile_infer.onnx",
            "assets/models/ppocrv5_en_dict.txt",
        };
        if (is_install_check(argc, argv)) {
            std::cout << "Install check succeeded.\n";
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
            if (foreground && foreground->title == config.window_title) {
                target = foreground->handle;
            } else {
                std::cout << "Saved game window is not foreground; press F7 to bind it.\n";
            }
        } else if (config.region_configured) {
            std::cout << "Saved window title is empty; press F7 to bind the game.\n";
        } else {
            std::cout << "No calibrated region; focus the game and press F7.\n";
        }

        bool announced_processing{};
        int consecutive_frame_errors{};
        std::unique_ptr<Pipeline> pipeline;
        auto next_metrics = std::chrono::steady_clock::now() +
                            std::chrono::seconds{5};

        while (keep_running.load(std::memory_order_acquire) &&
               !control.quit_requested()) {
            control.rethrow_if_failed();

            if (const auto request = control.take_calibration_request()) {
                announced_processing = false;
                consecutive_frame_errors = 0;
                if (pipeline) {
                    print_metrics(pipeline->app.metrics());
                    pipeline.reset();
                }

                const auto binding = dk::WindowLocator::foreground();
                if (!binding) {
                    std::cerr << "F7: no usable foreground game window.\n";
                    control.finish_calibration(*request, false);
                    continue;
                }
                const auto region =
                    dk::RegionSelector::select(binding->handle, binding->client_bounds);
                if (!region) {
                    control.finish_calibration(*request, false);
                    std::cout << "Calibration cancelled"
                              << (control.processing_enabled()
                                      ? "; honoring the newer F8 request.\n"
                                      : "; processing remains stopped.\n");
                    continue;
                }

                auto calibrated = config;
                calibrated.window_title = binding->title;
                calibrated.region = *region;
                calibrated.region_configured = true;
                dk::save_config("config.json", calibrated);
                config = calibrated;
                target = binding->handle;
                control.finish_calibration(*request, true);
                next_metrics = std::chrono::steady_clock::now() +
                               std::chrono::seconds{5};
                std::cout << "Calibration saved"
                          << (control.processing_enabled()
                                  ? "; processing will resume with rebuilt capture.\n"
                                  : ".\n");
                continue;
            }

            if (control.processing_enabled() != announced_processing) {
                if (!control.processing_enabled()) {
                    announced_processing = false;
                    std::cout << "STOPPED ("
                              << (config.live_input ? "live mode" : "dry run")
                              << " unchanged)\n";
                } else if (!target || !IsWindow(target)) {
                    std::cerr
                        << "Cannot start: calibrate a live game window with F7.\n";
                    control.stop_processing();
                } else {
                    pipeline.reset();
                    pipeline = build_pipeline(
                        config, target, recognizer, cancellation);
                    announced_processing = true;
                    consecutive_frame_errors = 0;
                    next_metrics = std::chrono::steady_clock::now() +
                                   std::chrono::seconds{5};
                    std::cout << "RUNNING ("
                              << (config.live_input ? "LIVE INPUT" : "DRY RUN")
                              << ")\n";
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (pipeline && now >= next_metrics) {
                print_metrics(pipeline->app.metrics());
                next_metrics = now + std::chrono::seconds{5};
            }
            if (target && !IsWindow(target)) {
                std::cerr << "Game window was destroyed; stopping.\n";
                break;
            }
            if (!target) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continue;
            }
            if (!control.processing_enabled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continue;
            }
            const auto current_region = screen_region(target, config);
            if (!current_region) {
                std::cerr
                    << "Game client region became invalid; processing paused. "
                       "Restore the window geometry or recalibrate with F7, then press F8.\n";
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
                    config, target, recognizer, cancellation);
                std::cout
                    << "Game window moved; capture rebuilt before processing.\n";
            }

            try {
                if (!pipeline->app.process_one_frame()) {
                    std::cerr << "Input was blocked or partial; stopping.\n";
                    control.stop_processing();
                    break;
                }
                consecutive_frame_errors = 0;
            } catch (const std::exception& error) {
                ++consecutive_frame_errors;
                std::cerr << "Frame processing error "
                          << consecutive_frame_errors << '/'
                          << maximum_consecutive_frame_errors << ": "
                          << error.what()
                          << "; discarding the frame and rebuilding capture.\n";
                pipeline.reset();
                if (consecutive_frame_errors >=
                    maximum_consecutive_frame_errors) {
                    std::cerr
                        << "Too many consecutive frame errors; processing paused. "
                           "Press F8 to retry or F7 to recalibrate.\n";
                    control.stop_processing();
                    announced_processing = false;
                }
                continue;
            }
        }

        control.rethrow_if_failed();
        if (pipeline) {
            print_metrics(pipeline->app.metrics());
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
