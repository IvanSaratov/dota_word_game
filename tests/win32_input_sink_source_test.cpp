#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_WIN32_INPUT_SINK_SOURCE_PATH
#error "DK_WIN32_INPUT_SINK_SOURCE_PATH must name win32_input_sink.cpp"
#endif

int main() {
    std::ifstream input{DK_WIN32_INPUT_SINK_SOURCE_PATH};
    if (!input) {
        std::cerr << "could not open Win32 input sink source\n";
        return 1;
    }
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        std::cerr << "could not read Win32 input sink source\n";
        return 1;
    }

    bool valid = true;

    const auto zero_delay = source.find("if (inter_key_delay_us_ <= 0)");
    const auto zero_delay_send = source.find("SendInput(requested", zero_delay);
    const auto zero_delay_cancel = source.find("if (cancellation())", zero_delay);
    const auto zero_delay_foreground =
        source.find("if (GetForegroundWindow() != target_)", zero_delay);
    if (zero_delay == std::string::npos || zero_delay_send == std::string::npos ||
        zero_delay_cancel == std::string::npos ||
        zero_delay_foreground == std::string::npos ||
        zero_delay_cancel > zero_delay_send ||
        zero_delay_foreground > zero_delay_send) {
        std::cerr
            << "zero-delay SendInput must recheck cancellation and foreground at the final boundary\n";
        valid = false;
    }

    const auto delayed_loop =
        source.find("for (std::size_t index = 0; index < letters.size(); ++index)");
    const auto cancellation_guard = source.find("if (cancellation())", delayed_loop);
    const auto foreground_guard = source.find("if (GetForegroundWindow() != target_)", delayed_loop);
    const auto delayed_send = source.find("SendInput(2, events", delayed_loop);
    if (delayed_loop == std::string::npos ||
        cancellation_guard == std::string::npos ||
        foreground_guard == std::string::npos ||
        delayed_send == std::string::npos ||
        cancellation_guard > delayed_send || foreground_guard > delayed_send) {
        std::cerr
            << "delayed SendInput must recheck cancellation and foreground inside the per-letter loop\n";
        valid = false;
    }

    const auto sent_pairs = source.find("std::size_t sent_pairs{};");
    const auto partial_cancel = source.find(
        "interrupted_send_status(sent_pairs, SendStatus::blocked)",
        delayed_loop);
    const auto partial_focus = source.find(
        "sent_pairs, SendStatus::not_foreground",
        delayed_loop);
    const auto partial_timer = source.find(
        "interrupted_send_status(sent_pairs, SendStatus::blocked)",
        partial_cancel == std::string::npos ? delayed_loop : partial_cancel + 1);
    if (sent_pairs == std::string::npos || sent_pairs > delayed_loop ||
        partial_cancel == std::string::npos ||
        partial_focus == std::string::npos ||
        partial_timer == std::string::npos) {
        std::cerr
            << "delayed input must turn cancellation, focus, and timer failures into partial after a sent prefix\n";
        valid = false;
    }

    return valid ? 0 : 1;
}
