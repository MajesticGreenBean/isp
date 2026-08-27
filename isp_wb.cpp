#include "isp.hpp"
#include "isp_bayer.hpp"
#include "isp_def.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

/**
 * Calculate channel gains for white-balancing using grey world.
 */
int measure_wb_grey_world(std::vector<int16_t> const& in_bytes, std::array<double, bayer10p::NUM_PHASES>& gains, InputSetting const& settings) {
    gains.fill(1.0);
    if (in_bytes.size() != get_num_samples(settings.geom)) {
        std::cout << "Something's wrong with number of samples detected: " << in_bytes.size() << " and expected: " << get_num_samples(settings.geom)
                  << '\n';
        return HELPER_FAILURE;
    }
    std::array<double, bayer10p::NUM_PHASES> channel_means;
    if (get_channel_means(in_bytes, channel_means, settings) != HELPER_SUCCESS) {
        std::cout << "Failed getting channel means inside white balancing\n";
        return HELPER_FAILURE;
    }

    auto const ref_green =
        (channel_means[static_cast<size_t>(bayer_phase::greenblue)] + channel_means[static_cast<size_t>(bayer_phase::greenred)]) / 2.0;
    if (ref_green < bayer10p::MIN_CHANNEL_MEAN) {
        std::cout << "Reference Green gains are near 0, nothing to do for grey-world\n";
        return HELPER_SUCCESS; // reference near zero, nothing to be done...
    }

    // Normalize the mean to prevent div by 0....
    for (auto& mean : channel_means) {
        if (mean < bayer10p::MIN_CHANNEL_MEAN) {
            mean = ref_green;
        }
    }

    // Leave both greens at 1.0 since they are the reference.
    gains[static_cast<size_t>(bayer_phase::red)] =
        std::clamp(ref_green / channel_means[static_cast<size_t>(bayer_phase::red)], bayer10p::MIN_GAIN, bayer10p::MAX_GAIN);
    gains[static_cast<size_t>(bayer_phase::blue)] =
        std::clamp(ref_green / channel_means[static_cast<size_t>(bayer_phase::blue)], bayer10p::MIN_GAIN, bayer10p::MAX_GAIN);
    return HELPER_SUCCESS;
}

int apply_wb_gains(std::vector<int16_t> const& in_bytes, std::array<double, bayer10p::NUM_PHASES> gains, std::vector<int16_t>& white_balanced_bytes,
                   InputSetting const& settings) {
    if (in_bytes.size() != get_num_samples(settings.geom)) {
        std::cout << "Something's wrong with number of samples detected: " << in_bytes.size() << " and expected: " << get_num_samples(settings.geom)
                  << '\n';
        return HELPER_FAILURE;
    }
    white_balanced_bytes.resize(in_bytes.size());

    // TODO: MAYBE RUN PERF AND CHECK?
    for (size_t i = 0; i < settings.geom.height; ++i) {
        for (size_t j = 0; j < settings.geom.width; ++j) {
            auto const curr = i * settings.geom.width + j;
            auto const gain = gains[static_cast<size_t>(get_phase_at(i, j, settings.order))];

            white_balanced_bytes[curr] = static_cast<int16_t>(std::round(gain * in_bytes[curr]));
        }
    }
    return HELPER_SUCCESS;
}
