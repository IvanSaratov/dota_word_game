#include "dk/session_log.hpp"

namespace dk {

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

SessionLog::SessionLog(const std::filesystem::path& path)
    : path_(path),
      stream_(path_, std::ios::binary | std::ios::trunc) {}

bool SessionLog::is_open() const noexcept {
    return stream_.is_open();
}

std::ostream* SessionLog::sink() noexcept {
    return is_open() ? &stream_ : nullptr;
}

const std::filesystem::path& SessionLog::path() const noexcept {
    return path_;
}

}  // namespace dk
