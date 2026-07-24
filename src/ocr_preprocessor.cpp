#include "ocr_preprocessor.hpp"

#include <stdexcept>

namespace dk::detail {

void write_rgb_to_nchw(
    const std::span<const std::uint8_t> rgb,
    const std::size_t height,
    const std::size_t width,
    const std::size_t row_stride,
    const std::size_t padded_width,
    std::vector<float>& tensor) {
    if (height == 0 || width == 0 || padded_width < width ||
        row_stride < width * 3) {
        throw std::invalid_argument("Invalid RGB dimensions for OCR preprocessing");
    }
    const std::size_t required_bytes = (height - 1) * row_stride + width * 3;
    if (rgb.size() < required_bytes) {
        throw std::invalid_argument("RGB buffer is too small for OCR preprocessing");
    }

    const std::size_t plane_size = height * padded_width;
    tensor.assign(3 * plane_size, 0.0F);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            const std::size_t source_offset = row * row_stride + column * 3;
            const std::size_t target_offset = row * padded_width + column;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                tensor[channel * plane_size + target_offset] =
                    (rgb[source_offset + channel] / 255.0F - 0.5F) / 0.5F;
            }
        }
    }
}

}  // namespace dk::detail
