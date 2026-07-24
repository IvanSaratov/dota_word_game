#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_APP_SOURCE_PATH
#error "DK_APP_SOURCE_PATH must name app.cpp"
#endif

int main() {
    std::ifstream input{DK_APP_SOURCE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read app.cpp\n";
        return 1;
    }

    const auto selected = source.find("if (selected)");
    const auto cancellation =
        source.find("if (cancellation_ && cancellation_())", selected);
    const auto send = source.find("input_.send_letters", selected);
    if (selected == std::string::npos || cancellation == std::string::npos ||
        send == std::string::npos || cancellation > send) {
        std::cerr << "App must recheck cancellation immediately before input dispatch\n";
        return 1;
    }

    const auto send_status = send;
    const auto cancelled =
        source.find("status == SendStatus::cancelled", send_status);
    const auto fatal = source.find("status == SendStatus::blocked", send_status);
    if (send_status == std::string::npos || cancelled == std::string::npos ||
        fatal == std::string::npos || cancelled > fatal) {
        std::cerr
            << "App must handle cancelled input nonfatally before blocked/partial failures\n";
        return 1;
    }
    return 0;
}
