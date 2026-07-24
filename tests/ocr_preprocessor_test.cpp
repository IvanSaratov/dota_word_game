#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "ocr_preprocessor.hpp"

TEST_CASE("OCR tensor keeps right padding zero after normalizing black pixels") {
    const std::vector<std::uint8_t> black_rgb{0, 0, 0};
    std::vector<float> tensor;

    dk::detail::write_rgb_to_nchw(black_rgb, 1, 1, 3, 2, tensor);

    REQUIRE(tensor.size() == 6);
    CHECK(tensor[0] == -1.0F);
    CHECK(tensor[1] == 0.0F);
    CHECK(tensor[2] == -1.0F);
    CHECK(tensor[3] == 0.0F);
    CHECK(tensor[4] == -1.0F);
    CHECK(tensor[5] == 0.0F);
}
