/**
 * Compares two netpbm images sample by sample. cmake -E compare_files answers "are these the same file"; when a golden moves
 * the useful question is "how", and that used to need a scratch worktree and a throwaway script.
 */
#include "isp.hpp"
#include "isp_def.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int MIN_ARGC = 3;

void print_help() {
    std::cout << "Usage: isp_compare <a.pgm|a.ppm> <b.pgm|b.ppm>\n"
              << "Exits EXIT_SUCCESS(0) when every sample matches, EXIT_FAILURE(1) otherwise.\n";
}

char const* get_channel_name(size_t const channel, size_t const nchannels) {
    if (nchannels != RGB_NUM_CHANNELS) {
        return "gray";
    }
    switch (channel) {
    case 0:
        return "R";
    case 1:
        return "G";
    default:
        return "B";
    }
}

void print_spec(char const* imgfile, OutImgSpec const& spec) {
    std::cout << imgfile << ' ';
    if (spec.nchannels == RGB_NUM_CHANNELS) {
        std::cout << "P6 ";
    } else {
        std::cout << "P5 ";
    }
    std::cout << '(' << spec.w << 'x' << spec.h << "), maxval=" << spec.maxval << '\n';
}

bool is_equal_specs(OutImgSpec const& a, OutImgSpec const& b) {
    return (a.w == b.w) && (a.h == b.h) && (a.nchannels == b.nchannels) && (a.maxval == b.maxval);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != MIN_ARGC) {
        print_help();
        return EXIT_FAILURE;
    }

    // ==============================================================================================
    // Quick read and check headers (specs).
    // ==============================================================================================
    std::vector<int16_t> a_samples{};
    std::vector<int16_t> b_samples{};
    OutImgSpec           a_spec{};
    OutImgSpec           b_spec{};
    if (read_simple_image(argv[1], a_samples, a_spec) != HELPER_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (read_simple_image(argv[2], b_samples, b_spec) != HELPER_SUCCESS) {
        return EXIT_FAILURE;
    }
    print_spec(argv[1], a_spec);
    print_spec(argv[2], b_spec);
    if (!is_equal_specs(a_spec, b_spec)) {
        std::cout << "headers mismatched, no need to compare further.\n";
        return EXIT_FAILURE;
    }

    // ==============================================================================================
    // Count differences.
    // ==============================================================================================
    size_t const        total = a_samples.size();
    std::vector<size_t> per_channel(a_spec.nchannels, 0U);
    size_t              num_differing = 0;
    size_t              first_diff    = total;
    int32_t             max_diff      = std::numeric_limits<int32_t>::min();
    int64_t             sum_diff      = 0;
    for (size_t i = 0; i < total; ++i) {
        auto const diff = std::abs(static_cast<int32_t>(a_samples[i]) - static_cast<int32_t>(b_samples[i]));
        if (diff == 0) {
            continue;
        }

        ++num_differing;
        sum_diff += diff;
        max_diff  = std::max(diff, max_diff);
        ++per_channel[i % a_spec.nchannels];
        if (first_diff == total) {
            first_diff = i;
        }
    }

    // ==============================================================================================
    // Dump various stats.
    // ==============================================================================================
    if (num_differing == 0) {
        std::cout << "identical: " << total << " / " << total << " samples\n";
        return EXIT_SUCCESS;
    }
    std::cout << "differ: " << num_differing << " / " << total << " samples ("
              << (100.0 * static_cast<double>(num_differing) / static_cast<double>(total)) << "%)\n";
    std::cout << "  |max_diff| = " << max_diff << ", |avg_diff| = " << (static_cast<double>(sum_diff) / static_cast<double>(num_differing))
              << " pixels\n";
    std::cout << "  by channel:";
    for (size_t c = 0; c < a_spec.nchannels; ++c) {
        std::cout << ' ' << get_channel_name(c, a_spec.nchannels) << ' ' << per_channel[c];
    }
    std::cout << '\n';
    size_t const first_diff_pixel = first_diff / a_spec.nchannels;
    std::cout << "  first at (" << first_diff_pixel / a_spec.w << "," << first_diff_pixel % a_spec.w << "), channel "
              << get_channel_name(first_diff % a_spec.nchannels, a_spec.nchannels) << ": " << a_samples[first_diff] << "(" << argv[1] << ") vs "
              << b_samples[first_diff] << "(" << argv[2] << ")\n";

    return EXIT_FAILURE;
}
