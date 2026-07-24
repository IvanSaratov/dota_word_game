#pragma once

#include <atomic>
#include <mutex>

namespace dk {

class HotkeyState {
public:
    [[nodiscard]] bool processing_enabled() const noexcept {
        return effective_processing_enabled_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool desired_processing_enabled() const {
        std::lock_guard lock{mutex_};
        return desired_processing_enabled_;
    }

    [[nodiscard]] bool quit_requested() const noexcept {
        return quit_requested_.load(std::memory_order_acquire);
    }

    void toggle_processing() {
        std::lock_guard lock{mutex_};
        desired_processing_enabled_ = !desired_processing_enabled_;
        refresh_effective_state();
    }

    void stop_processing() {
        std::lock_guard lock{mutex_};
        desired_processing_enabled_ = false;
        refresh_effective_state();
    }

    void request_calibration() {
        std::lock_guard lock{mutex_};
        calibration_requested_ = true;
        calibration_paused_ = true;
        refresh_effective_state();
    }

    [[nodiscard]] bool take_calibration_request() {
        std::lock_guard lock{mutex_};
        const bool requested = calibration_requested_;
        calibration_requested_ = false;
        return requested;
    }

    void finish_calibration() {
        std::lock_guard lock{mutex_};
        calibration_paused_ = calibration_requested_;
        refresh_effective_state();
    }

    void request_quit() {
        std::lock_guard lock{mutex_};
        quit_requested_.store(true, std::memory_order_release);
        refresh_effective_state();
    }

private:
    void refresh_effective_state() noexcept {
        effective_processing_enabled_.store(
            desired_processing_enabled_ && !calibration_paused_ &&
                !quit_requested_.load(std::memory_order_relaxed),
            std::memory_order_release);
    }

    mutable std::mutex mutex_;
    bool desired_processing_enabled_{};
    bool calibration_requested_{};
    bool calibration_paused_{};
    std::atomic_bool quit_requested_{false};
    std::atomic_bool effective_processing_enabled_{false};
};

}  // namespace dk
