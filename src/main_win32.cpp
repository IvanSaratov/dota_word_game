#ifndef _WIN32
#error "main_win32.cpp is only available on Windows"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

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

struct Pipeline {
    Pipeline(const dk::AppConfig& config, HWND target, dk::Box capture_region,
             dk::LineRecognizer& recognizer)
        : detector(config.detector),
          capture(capture_region),
          input(target, config.inter_key_delay_us),
          app(config, capture, detector, recognizer, input) {}

    dk::CandidateDetector detector;
    dk::DxgiCapture capture;
    dk::Win32InputSink input;
    dk::App app;
};

std::unique_ptr<Pipeline> build_pipeline(
    const dk::AppConfig& config, HWND target, dk::LineRecognizer& recognizer) {
    const auto region = screen_region(target, config);
    if (!region) {
        throw std::runtime_error(
            "The configured region is invalid for the current game client bounds.");
    }
    return std::make_unique<Pipeline>(config, target, *region, recognizer);
}

dk::AppConfig load_startup_config() {
    const std::filesystem::path user_config{"config.json"};
    if (std::filesystem::exists(user_config)) {
        return dk::load_config(user_config);
    }
    return dk::load_config("config/default.json");
}

}  // namespace

int wmain() {
    SetConsoleCtrlHandler(console_control, TRUE);
    try {
        auto config = load_startup_config();
        std::cout << "\n========================================\n"
                  << (config.live_input ? "       LIVE INPUT ENABLED\n"
                                        : "             DRY RUN\n")
                  << "========================================\n"
                  << "F7 calibrates. F8 starts or stops processing.\n";

        dk::OcrRecognizer recognizer{
            "assets/models/en_PP-OCRv5_rec_mobile_infer.onnx",
            "assets/models/ppocrv5_en_dict.txt",
        };
        dk::Hotkeys hotkeys{config.hotkeys.calibrate, config.hotkeys.toggle};

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

        bool running = false;
        std::unique_ptr<Pipeline> pipeline;
        auto next_metrics = std::chrono::steady_clock::now() +
                            std::chrono::seconds{5};

        while (keep_running) {
            const auto event = hotkeys.poll(
                running ? std::chrono::milliseconds{0}
                        : std::chrono::milliseconds{250});
            if (event == dk::HotkeyEvent::quit) {
                break;
            }
            if (event == dk::HotkeyEvent::calibrate) {
                const bool resume = running;
                running = false;
                if (pipeline) {
                    print_metrics(pipeline->app.metrics());
                    pipeline.reset();
                }

                const auto binding = dk::WindowLocator::foreground();
                if (!binding) {
                    std::cerr << "F7: no usable foreground game window.\n";
                    continue;
                }
                const auto region =
                    dk::RegionSelector::select(binding->handle, binding->client_bounds);
                if (!region) {
                    std::cout << "Calibration cancelled; processing remains stopped.\n";
                    continue;
                }

                auto calibrated = config;
                calibrated.window_title = binding->title;
                calibrated.region = *region;
                calibrated.region_configured = true;
                dk::save_config("config.json", calibrated);
                config = calibrated;
                target = binding->handle;
                pipeline = build_pipeline(config, target, recognizer);
                running = resume;
                next_metrics = std::chrono::steady_clock::now() +
                               std::chrono::seconds{5};
                std::cout << "Calibration saved and capture rebuilt"
                          << (running ? "; processing resumed.\n" : ".\n");
                continue;
            }
            if (event == dk::HotkeyEvent::toggle) {
                if (running) {
                    running = false;
                    std::cout << "STOPPED ("
                              << (config.live_input ? "live mode" : "dry run")
                              << " unchanged)\n";
                } else {
                    if (!target || !IsWindow(target)) {
                        std::cerr << "Cannot start: calibrate a live game window with F7.\n";
                        continue;
                    }
                    pipeline = build_pipeline(config, target, recognizer);
                    running = true;
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
                continue;
            }
            if (!running) {
                continue;
            }
            if (!screen_region(target, config)) {
                std::cerr << "Game client region became invalid; stopping.\n";
                break;
            }
            if (!pipeline->app.process_one_frame()) {
                std::cerr << "Input was blocked or partial; stopping.\n";
                break;
            }

        }

        if (pipeline) {
            print_metrics(pipeline->app.metrics());
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
