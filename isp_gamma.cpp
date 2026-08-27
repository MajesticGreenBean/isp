#include "isp.hpp"
#include "isp_def.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>


// The simplest of simplest of gamma
int encode_gamma(std::vector<std::int16_t> const& bayer_pixels, std::vector<std::int16_t>& screen_pixels, int32_t white_point) {
    if (white_point <= 0) {
        std::cout << "Check white point, it is non-positive: " << white_point << '\n';
        return HELPER_FAILURE;
    }
    auto const num_pixels = bayer_pixels.size();
    screen_pixels.resize(num_pixels);
    auto const max_pixel_val = white_point;
    auto const pow_factor = 1.0/2.2;
    auto const scale_factor = 255.0;

    double normalized = 0.0;
    double encoded = 0.0;
    for (size_t i = 0; i < num_pixels; ++i) {
        normalized = static_cast<double>(std::max(bayer_pixels[i], int16_t{0})) / static_cast<double>(max_pixel_val);
        encoded    = std::round(std::pow(normalized, pow_factor)*scale_factor);
        screen_pixels[i] = static_cast<int16_t>(std::clamp(encoded, 0.0, scale_factor));
    }
    return HELPER_SUCCESS;
}
