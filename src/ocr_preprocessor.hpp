#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dk::detail {

void write_rgb_to_nchw(
    std::span<const std::uint8_t> rgb,
    std::size_t height,
    std::size_t width,
    std::size_t row_stride,
    std::size_t padded_width,
    std::vector<float>& tensor);

}  // namespace dk::detail
