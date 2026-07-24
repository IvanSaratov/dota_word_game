#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_MAIN_WIN32_SOURCE_PATH
#error "DK_MAIN_WIN32_SOURCE_PATH must name main_win32.cpp"
#endif

namespace {

bool require_text(
    const std::string& source, const std::string& expected, const char* requirement) {
    if (source.find(expected) != std::string::npos) {
        return true;
    }
    std::cerr << "missing main control-flow guard: " << requirement << '\n';
    return false;
}

}  // namespace

int main() {
    std::ifstream input{DK_MAIN_WIN32_SOURCE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read main_win32.cpp\n";
        return 1;
    }

    bool valid = true;
    valid &= require_text(
        source,
        "if (config.region_configured && !config.window_title.empty())",
        "only a non-empty saved title may auto-bind");
    valid &= source.find("config.window_title.empty() ||") == std::string::npos;
    if (!valid) {
        std::cerr << "empty saved title must remain unbound\n";
    }
    valid &= require_text(
        source,
        "if (target && !IsWindow(target))",
        "only a previously bound HWND can be classified as destroyed");
    valid &= require_text(
        source,
        "if (!target) {",
        "an unbound startup must stay in the hotkey loop");

    valid &= require_text(
        source,
        "class HotkeyController",
        "hotkeys must be owned by a dedicated controller");
    valid &= require_text(
        source,
        "std::jthread worker_",
        "hotkey polling must run independently of frame processing");
    valid &= require_text(
        source,
        "dk::HotkeyState state_;",
        "desired processing state must be separate from calibration pause");
    valid &= require_text(
        source,
        "dk::Hotkeys hotkeys",
        "the dedicated thread must own the Win32 hotkey message loop");
    valid &= require_text(
        source,
        "take_calibration_request",
        "the main thread must consume calibration requests");
    valid &= require_text(
        source,
        "rethrow_if_failed",
        "hotkey-thread errors must propagate to the main thread");
    valid &= require_text(
        source,
        "dk::CancellationPredicate cancellation",
        "App and InputSink must share the immediate-stop predicate");

    valid &= require_text(
        source,
        "dk::Box capture_region;",
        "Pipeline must remember the exact screen region captured");
    const auto current_region =
        source.find("const auto current_region = screen_region(target, config);");
    const auto moved_region =
        source.find("pipeline->capture_region != *current_region", current_region);
    const auto rebuild =
        source.find("pipeline = build_pipeline(", moved_region);
    const auto process = source.find("pipeline->app.process_one_frame()", current_region);
    if (current_region == std::string::npos || moved_region == std::string::npos ||
        rebuild == std::string::npos || process == std::string::npos ||
        current_region > moved_region || moved_region > rebuild || rebuild > process) {
        std::cerr
            << "screen ROI changes must rebuild capture before any OCR/input processing\n";
        valid = false;
    }

    const auto frame_try = source.rfind("try {", process);
    const auto frame_catch =
        source.find("catch (const std::exception& error)", process);
    const auto bounded_errors =
        source.find("maximum_consecutive_frame_errors", process);
    const auto reset_pipeline = source.find("pipeline.reset();", process);
    if (frame_try == std::string::npos || frame_catch == std::string::npos ||
        bounded_errors == std::string::npos ||
        reset_pipeline == std::string::npos ||
        frame_try > process || process > frame_catch ||
        frame_catch > bounded_errors || frame_catch > reset_pipeline) {
        std::cerr
            << "per-frame exceptions must be caught, rebuild capture, and stop at a bounded threshold\n";
        valid = false;
    }

    valid &= require_text(
        source,
        "int wmain(int argc, wchar_t* argv[])",
        "the executable must receive command-line arguments");
    valid &= require_text(
        source,
        "std::wstring_view{argv[1]} == L\"--check-install\"",
        "the package smoke-check command must be recognized");
    valid &= require_text(
        source,
        "Install check succeeded.",
        "the package smoke-check must report success");
    valid &= require_text(
        source,
        "dk::load_config(\"config.json\")",
        "the package smoke-check must require the packaged config.json");

    const auto install_mode = source.find("const bool install_check = is_install_check(argc, argv);");
    const auto package_config = source.find("dk::load_config(\"config.json\")", install_mode);
    const auto config_load = source.find("auto config = install_check ?");
    const auto recognizer = source.find("dk::OcrRecognizer recognizer");
    const auto install_check = source.find("if (is_install_check(argc, argv))");
    const auto hotkeys = source.find("HotkeyController control", recognizer);
    if (install_mode == std::string::npos || package_config == std::string::npos ||
        config_load == std::string::npos || recognizer == std::string::npos ||
        install_check == std::string::npos || hotkeys == std::string::npos ||
        install_mode > package_config || package_config > recognizer ||
        config_load > recognizer || recognizer > install_check ||
        install_check > hotkeys) {
        std::cerr << "install check must load config and OCR dependencies before hotkeys\n";
        valid = false;
    }

    const auto summary = source.find("if (pipeline && now >= next_metrics)");
    const auto unbound_continue = source.find("if (!target) {", summary);
    const auto stopped_continue =
        source.find("if (!control.processing_enabled()) {", summary);
    if (summary == std::string::npos || unbound_continue == std::string::npos ||
        stopped_continue == std::string::npos || summary > unbound_continue ||
        summary > stopped_continue) {
        std::cerr << "periodic metrics must run before stopped/unbound continues\n";
        valid = false;
    }
    return valid ? 0 : 1;
}
