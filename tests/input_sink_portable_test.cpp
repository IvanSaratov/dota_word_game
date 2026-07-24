#include <iostream>

#include "dk/input_sink.hpp"

int main() {
    bool valid = true;
    valid &= dk::interrupted_send_status(0, dk::SendStatus::not_foreground) ==
             dk::SendStatus::not_foreground;
    valid &= dk::interrupted_send_status(0, dk::SendStatus::blocked) ==
             dk::SendStatus::blocked;
    valid &= dk::interrupted_send_status(0, dk::SendStatus::cancelled) ==
             dk::SendStatus::cancelled;
    valid &= dk::interrupted_send_status(1, dk::SendStatus::not_foreground) ==
             dk::SendStatus::partial;
    valid &= dk::interrupted_send_status(2, dk::SendStatus::blocked) ==
             dk::SendStatus::partial;
    valid &= dk::interrupted_send_status(3, dk::SendStatus::cancelled) ==
             dk::SendStatus::partial;
    if (!valid) {
        std::cerr << "interrupted send status did not preserve partial-prefix safety\n";
    }
    return valid ? 0 : 1;
}
