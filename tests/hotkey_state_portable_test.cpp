#include <iostream>

#include "dk/hotkey_state.hpp"

int main() {
    bool valid = true;

    dk::HotkeyState state;
    state.toggle_processing();
    valid &= state.desired_processing_enabled();
    valid &= state.processing_enabled();

    state.request_calibration();
    valid &= state.desired_processing_enabled();
    valid &= !state.processing_enabled();
    valid &= state.take_calibration_request();
    valid &= !state.processing_enabled();

    state.toggle_processing();
    valid &= !state.desired_processing_enabled();
    state.finish_calibration();
    valid &= !state.processing_enabled();

    state.request_calibration();
    valid &= state.take_calibration_request();
    state.toggle_processing();
    valid &= state.desired_processing_enabled();
    valid &= !state.processing_enabled();
    state.finish_calibration();
    valid &= state.processing_enabled();

    state.request_calibration();
    valid &= state.take_calibration_request();
    state.finish_calibration();
    valid &= state.processing_enabled();

    state.request_quit();
    valid &= !state.processing_enabled();

    if (!valid) {
        std::cerr
            << "hotkey desired state and temporary calibration pause diverged\n";
    }
    return valid ? 0 : 1;
}
