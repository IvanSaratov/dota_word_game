#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_OCR_RECOGNIZER_SOURCE_PATH
#error "DK_OCR_RECOGNIZER_SOURCE_PATH must name ocr_recognizer.cpp"
#endif

namespace {

bool require_text(
    const std::string& source, const std::string& expected, const char* requirement) {
    if (source.find(expected) != std::string::npos) {
        return true;
    }
    std::cerr << "missing OCR logging requirement: " << requirement << '\n';
    return false;
}

bool forbid_text(
    const std::string& source, const std::string& forbidden, const char* requirement) {
    if (source.find(forbidden) == std::string::npos) {
        return true;
    }
    std::cerr << "unexpected OCR logging configuration: " << requirement << '\n';
    return false;
}

}  // namespace

int main() {
    std::ifstream input{DK_OCR_RECOGNIZER_SOURCE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read ocr_recognizer.cpp\n";
        return 1;
    }

    const bool has_fatal = require_text(
        source, "ORT_LOGGING_LEVEL_FATAL", "fatal ONNX Runtime environment log level");
    const bool lacks_warning = forbid_text(
        source, "ORT_LOGGING_LEVEL_WARNING", "warning ONNX Runtime environment log level");
    return has_fatal && lacks_warning ? 0 : 1;
}
