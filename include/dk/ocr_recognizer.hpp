#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

namespace dk {

struct OcrResult {
    std::string text;
    float confidence{};
};

class LineRecognizer {
public:
    virtual ~LineRecognizer() = default;
    [[nodiscard]] virtual OcrResult recognize(const cv::Mat& line) = 0;
};

class OcrRecognizer final : public LineRecognizer {
public:
    OcrRecognizer(
        const std::filesystem::path& model,
        const std::filesystem::path& dictionary);
    ~OcrRecognizer();

    OcrRecognizer(OcrRecognizer&&) noexcept;
    OcrRecognizer& operator=(OcrRecognizer&&) noexcept;

    [[nodiscard]] OcrResult recognize(const cv::Mat& line) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dk
