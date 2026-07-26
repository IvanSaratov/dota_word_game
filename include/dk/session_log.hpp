#pragma once

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

namespace dk {

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

class SessionLog {
public:
    explicit SessionLog(const std::filesystem::path& path);

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::ostream* sink() noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
    std::ofstream stream_;
};

}  // namespace dk
