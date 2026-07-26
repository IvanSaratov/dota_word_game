#include "dk/session_log.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "session_log_test failure: " << message << '\n';
        std::exit(1);
    }
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

}  // namespace

int main() {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("dota-keyboard-session-log-" + std::to_string(unique));
    std::filesystem::create_directory(root);
    const auto path = root / "dota-keyboard.log";

    {
        std::ofstream old_session{path, std::ios::binary};
        old_session << "old session\n";
    }
    {
        dk::SessionLog session{path};
        require(session.is_open(), "existing writable file must open");
        require(session.sink() != nullptr, "open session must expose its sink");
        require(session.path() == path, "session must retain its exact path");
        *session.sink() << "current session\n";
    }

    const auto content = read_all(path);
    require(content == "current session\n",
            "opening a session must truncate the previous session");

    const auto unavailable_path = root / "missing-parent" / "dota-keyboard.log";
    dk::SessionLog unavailable{unavailable_path};
    require(!unavailable.is_open(), "missing parent must leave log unavailable");
    require(unavailable.sink() == nullptr,
            "unavailable session must not expose a sink");

    std::filesystem::remove_all(root);
}
