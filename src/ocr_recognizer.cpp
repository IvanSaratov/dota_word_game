#include "dk/ocr_recognizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#include "dk/ctc_decoder.hpp"
#include "ocr_preprocessor.hpp"

namespace dk {
namespace {

constexpr int kInputChannels = 3;
constexpr int kInputHeight = 48;
constexpr int kInputWidth = 320;

Ort::Env& ort_environment() {
    static Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "dota_keyboard_ocr"};
    return environment;
}

Ort::SessionOptions make_session_options() {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(1);
    return options;
}

std::vector<std::string> load_dictionary(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open OCR dictionary: " + path.string());
    }

    std::vector<std::string> entries;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (entries.empty() && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }
        entries.push_back(std::move(line));
    }
    if (stream.bad()) {
        throw std::runtime_error("Unable to read OCR dictionary: " + path.string());
    }
    if (entries.empty()) {
        throw std::runtime_error("OCR dictionary is empty: " + path.string());
    }
    return entries;
}

void require_dimension(
    const int64_t actual,
    const int64_t expected,
    const std::string& description) {
    if (actual > 0 && actual != expected) {
        throw std::runtime_error(
            "Incompatible OCR model " + description + ": expected " +
            std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

void validate_input_shape(const std::vector<int64_t>& shape) {
    if (shape.size() != 4) {
        throw std::runtime_error(
            "Incompatible OCR model input shape: expected rank 4 [1, 3, 48, 320]");
    }
    require_dimension(shape[0], 1, "input batch dimension");
    require_dimension(shape[1], kInputChannels, "input channel dimension");
    require_dimension(shape[2], kInputHeight, "input height");
    require_dimension(shape[3], kInputWidth, "input width");
}

void validate_declared_output_shape(const std::vector<int64_t>& shape) {
    if (shape.size() != 3) {
        throw std::runtime_error(
            "Incompatible OCR model output shape: expected rank 3 [1, time, classes]");
    }
    require_dimension(shape[0], 1, "output batch dimension");
}

std::vector<std::string> make_class_list(
    const std::vector<std::string>& dictionary_entries,
    const std::size_t output_class_count) {
    const std::size_t without_space = dictionary_entries.size() + 1;
    const std::size_t with_space = dictionary_entries.size() + 2;
    if (output_class_count != without_space && output_class_count != with_space) {
        throw std::runtime_error(
            "Incompatible OCR model class count: model has " +
            std::to_string(output_class_count) + ", dictionary supports " +
            std::to_string(without_space) + " or " + std::to_string(with_space));
    }

    std::vector<std::string> classes;
    classes.reserve(output_class_count);
    classes.emplace_back();
    classes.insert(classes.end(), dictionary_entries.begin(), dictionary_entries.end());
    if (output_class_count == with_space) {
        classes.emplace_back(" ");
    }
    return classes;
}

cv::Mat convert_to_rgb(const cv::Mat& line) {
    if (line.depth() != CV_8U) {
        throw std::invalid_argument("OCR input crop must contain 8-bit pixels");
    }

    cv::Mat rgb;
    switch (line.channels()) {
        case 1:
            cv::cvtColor(line, rgb, cv::COLOR_GRAY2RGB);
            break;
        case 3:
            cv::cvtColor(line, rgb, cv::COLOR_BGR2RGB);
            break;
        case 4:
            cv::cvtColor(line, rgb, cv::COLOR_BGRA2RGB);
            break;
        default:
            throw std::invalid_argument("OCR input crop must have 1, 3, or 4 channels");
    }
    return rgb;
}

float maximum_class_score(
    const float* row,
    const std::size_t class_count,
    std::size_t& maximum_index) {
    maximum_index = 0;
    float maximum = -std::numeric_limits<float>::infinity();
    double sum = 0.0;
    bool could_be_probabilities = true;

    for (std::size_t index = 0; index < class_count; ++index) {
        const float value = row[index];
        if (!std::isfinite(value)) {
            throw std::runtime_error("OCR model output contains a non-finite value");
        }
        if (value > maximum) {
            maximum = value;
            maximum_index = index;
        }
        sum += value;
        could_be_probabilities =
            could_be_probabilities && value >= -1.0e-5F && value <= 1.00001F;
    }

    if (could_be_probabilities && std::abs(sum - 1.0) <= 1.0e-3) {
        return maximum;
    }

    double exponential_sum = 0.0;
    for (std::size_t index = 0; index < class_count; ++index) {
        exponential_sum += std::exp(static_cast<double>(row[index] - maximum));
    }
    if (!std::isfinite(exponential_sum) || exponential_sum <= 0.0) {
        throw std::runtime_error("Unable to apply softmax to OCR model output");
    }
    return static_cast<float>(1.0 / exponential_sum);
}

}  // namespace

struct OcrRecognizer::Impl {
    Impl(
        const std::filesystem::path& model_path,
        const std::filesystem::path& dictionary_path)
        : session_options(make_session_options()),
          session(ort_environment(), model_path.c_str(), session_options),
          dictionary_entries(load_dictionary(dictionary_path)) {
        if (session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
            throw std::runtime_error(
                "Incompatible OCR model: expected exactly one input and one output");
        }

        Ort::AllocatorWithDefaultOptions allocator;
        const auto allocated_input_name = session.GetInputNameAllocated(0, allocator);
        const auto allocated_output_name = session.GetOutputNameAllocated(0, allocator);
        input_name = allocated_input_name.get();
        output_name = allocated_output_name.get();

        const auto input_info = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error("Incompatible OCR model input type: expected float");
        }
        input_shape = input_info.GetShape();
        validate_input_shape(input_shape);

        const auto output_info = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
        if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error("Incompatible OCR model output type: expected float");
        }
        output_shape = output_info.GetShape();
        validate_declared_output_shape(output_shape);
        if (output_shape[2] > 0) {
            classes = make_class_list(
                dictionary_entries, static_cast<std::size_t>(output_shape[2]));
        }
    }

    OcrResult recognize(const cv::Mat& line) {
        if (line.empty()) {
            return {};
        }

        std::lock_guard lock(buffer_mutex);
        const cv::Mat rgb = convert_to_rgb(line);
        const double scale = static_cast<double>(kInputHeight) / rgb.rows;
        const int resized_width = std::clamp(
            static_cast<int>(std::lround(rgb.cols * scale)), 1, kInputWidth);

        cv::Mat resized;
        cv::resize(
            rgb,
            resized,
            cv::Size{resized_width, kInputHeight},
            0.0,
            0.0,
            cv::INTER_LINEAR);
        const std::size_t resized_storage_size =
            (static_cast<std::size_t>(resized.rows) - 1) * resized.step[0] +
            static_cast<std::size_t>(resized.cols * resized.channels());
        detail::write_rgb_to_nchw(
            std::span<const std::uint8_t>{resized.data, resized_storage_size},
            static_cast<std::size_t>(resized.rows),
            static_cast<std::size_t>(resized.cols),
            resized.step[0],
            kInputWidth,
            input_buffer);

        const std::array<int64_t, 4> tensor_shape{
            1, kInputChannels, kInputHeight, kInputWidth};
        auto memory_info =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            input_buffer.data(),
            input_buffer.size(),
            tensor_shape.data(),
            tensor_shape.size());

        const char* input_name_pointer = input_name.c_str();
        const char* output_name_pointer = output_name.c_str();
        auto outputs = session.Run(
            Ort::RunOptions{nullptr},
            &input_name_pointer,
            &input_tensor,
            1,
            &output_name_pointer,
            1);
        if (outputs.size() != 1 || !outputs.front().IsTensor()) {
            throw std::runtime_error("OCR model did not return one output tensor");
        }

        const auto actual_output_info =
            outputs.front().GetTensorTypeAndShapeInfo();
        if (actual_output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error("OCR model returned a non-float output tensor");
        }
        const std::vector<int64_t> actual_shape = actual_output_info.GetShape();
        if (actual_shape.size() != 3 || actual_shape[0] != 1 ||
            actual_shape[1] <= 0 || actual_shape[2] <= 0) {
            throw std::runtime_error(
                "Incompatible OCR model output shape: expected [1, time, classes]");
        }

        const auto time_step_count = static_cast<std::size_t>(actual_shape[1]);
        const auto class_count = static_cast<std::size_t>(actual_shape[2]);
        if (classes.size() != class_count) {
            classes = make_class_list(dictionary_entries, class_count);
        }
        if (actual_output_info.GetElementCount() != time_step_count * class_count) {
            throw std::runtime_error("OCR model output tensor has an inconsistent size");
        }

        const float* output_data = outputs.front().GetTensorData<float>();
        class_ids.resize(time_step_count);
        class_scores.resize(time_step_count);
        for (std::size_t step = 0; step < time_step_count; ++step) {
            std::size_t maximum_index = 0;
            class_scores[step] = maximum_class_score(
                output_data + step * class_count, class_count, maximum_index);
            class_ids[step] = static_cast<int64_t>(maximum_index);
        }

        const CtcResult decoded = decode_ctc(class_ids, class_scores, classes);
        return OcrResult{decoded.text, decoded.confidence};
    }

    Ort::SessionOptions session_options;
    Ort::Session session;
    std::vector<std::string> dictionary_entries;
    std::vector<std::string> classes;
    std::string input_name;
    std::string output_name;
    std::vector<int64_t> input_shape;
    std::vector<int64_t> output_shape;
    std::vector<float> input_buffer;
    std::vector<int64_t> class_ids;
    std::vector<float> class_scores;
    std::mutex buffer_mutex;
};

OcrRecognizer::OcrRecognizer(
    const std::filesystem::path& model,
    const std::filesystem::path& dictionary)
    : impl_(std::make_unique<Impl>(model, dictionary)) {}

OcrRecognizer::~OcrRecognizer() = default;
OcrRecognizer::OcrRecognizer(OcrRecognizer&&) noexcept = default;
OcrRecognizer& OcrRecognizer::operator=(OcrRecognizer&&) noexcept = default;

OcrResult OcrRecognizer::recognize(const cv::Mat& line) {
    if (!impl_) {
        throw std::logic_error("Cannot use a moved-from OCR recognizer");
    }
    return impl_->recognize(line);
}

}  // namespace dk
