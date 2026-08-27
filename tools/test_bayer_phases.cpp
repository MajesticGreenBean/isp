/*reap*
 * A few tests to make sure our encoding/decoding keeps the phases correctly.
 */

#include "isp.hpp"
#include "isp_bayer.hpp"
#include "isp_def.hpp"
#include <cstdlib>
#include <iostream>
#include <fstream>

struct order_comb {
    bayer_order order;
    char const* name;
};

namespace {

/**
 * Convenient for printing.
 */
char const* get_phase_name(bayer_phase const phase) {
    switch (phase) {
    case bayer_phase::red: {
        return "red";
    }
    case bayer_phase::greenred: {
        return "greenred";
    }
    case bayer_phase::greenblue: {
        return "greenblue";
    }
    case bayer_phase::blue: {
        return "blue";
    }
    }
    return "???";
}

/**
 * String-based, easier to reason and check.
 * From the string order (one of "RGGB", "GRBG", "GBRG", "BGGR") and the current phase position (0,1,2,3), get the phase.
 * Example input looks like:
 * B G G R
 * We read as 2x2 table:
 * B(0b00) G(0b01)
 * G(0b10) R(0b11)
 */
constexpr bayer_phase get_phase_by_name(char const* name, size_t const k) {
    if (name[k] == 'R') {
        return bayer_phase::red;
    } else if (name[k] == 'B') {
        return bayer_phase::blue;
    }

    if (name[k ^ 1U] == 'R') {
        return bayer_phase::greenred;
    }
    return bayer_phase::greenblue;
}

/**
 * Test frame has all-equal r-g-b cells. means of each should be exact same, and leveling should result in 0.
 */
bool check_ordering(order_comb const& o) {
    InputSetting  setting = {.order = o.order, .geom = DEFAULT_GEOM};
    Geom_t const& geom    = setting.geom;
    // well-rounded, decently far random nums to test get mean and black leveling index the correct cells.
    constexpr std::array<int16_t, bayer10p::NUM_PHASES> TEST_IN = {100, 200, 300, 400};
    std::vector<int16_t>                                test_frame(get_num_samples(geom), 0);
    // give all pixels same red,green,blue values.
    for (size_t i = 0; i < geom.height; i += bayer10p::CELL_DIM) {
        for (size_t j = 0; j < geom.width; j += bayer10p::CELL_DIM) {
            for (size_t k = 0; k < bayer10p::NUM_PHASES; ++k) {
                size_t const row                   = i + (k / bayer10p::CELL_DIM);
                size_t const col                   = j + (k % bayer10p::CELL_DIM);
                test_frame[row * geom.width + col] = TEST_IN[static_cast<size_t>(get_phase_by_name(o.name, k))];
            }
        }
    }

    std::array<double, bayer10p::NUM_PHASES> means{};
    if (get_channel_means(test_frame, means, setting) != HELPER_SUCCESS) {
        std::cout << "[BLACKLEVEL TEST] Failed calculating mean from test frame.\n";
        return false;
    }
    // average of 1 value is itself.
    for (size_t k = 0; k < bayer10p::NUM_PHASES; ++k) {
        auto const measured_mean = static_cast<int16_t>(means[k]);
        if (measured_mean != TEST_IN[k]) {
            std::cout << "[BLACKLEVEL TEST] Wrong phase mean for: " << o.name << ": " << get_phase_name(static_cast<bayer_phase>(k)) << "("
                      << measured_mean << ") expected(" << TEST_IN[k] << ")\n";
            return false;
        }
    }
    std::array<int32_t, bayer10p::NUM_PHASES> test_in_32{};
    for (size_t k = 0; k < bayer10p::NUM_PHASES; ++k) {
        test_in_32[k] = static_cast<int32_t>(TEST_IN[k]);
    }
    std::vector<int16_t> leveled;
    if (level_black(test_frame, test_in_32, leveled, setting) != HELPER_SUCCESS) {
        std::cout << "[BLACKLEVEL TEST] Failed applying black leveling for test frame.\n";
        return false;
    }
    // subtraction should be zeroize.
    for (size_t i = 0; i < leveled.size(); ++i) {
        if (leveled[i] != 0) {
            std::cout << "[BLACKLEVEL TEST] result at (" << i / geom.width << "," << i % geom.width << ")=" << leveled[i] << ", expected 0.\n";
            return false;
        }
    }

    std::cout << "[BLACKLEVEL TEST] order: " << o.name << ":ok.\n";
    return true;
}

/**
 * Simple pack then unpack. Result after round-trip should be 0 difference.
 */
bool check_round_trip() {
    constexpr size_t num_groups = 1024U;
    constexpr size_t num_pixels = num_groups * bayer10p::PIXELS_PER_GROUP;
    constexpr size_t num_bytes  = num_groups * bayer10p::BYTES_PER_GROUP;

    // Synthesize an input for testing. Just monotonous function with varying high bits within group and lower 2 bits between groups.
    std::vector<int16_t> original(num_pixels);
    for (size_t i = 0; i < original.size(); ++i) {
        auto const group_id = i / bayer10p::PIXELS_PER_GROUP;
        auto const pos_id   = i % bayer10p::PIXELS_PER_GROUP;
        original[i]         = static_cast<int16_t>((group_id + pos_id) % num_groups);
    }

    std::vector<uint8_t> packed;
    if (pack_bayer_10p(original, packed) != HELPER_SUCCESS) {
        std::cout << "[ROUND TRIP TEST] Failed repacking unpacked data.\n";
        return false;
    }

    if (packed.size() != num_bytes) {
        std::cout << "[ROUND TRIP TEST] result packed has " << packed.size() << " bytes, but expected: " << num_bytes << '\n';
        return false;
    }

    std::vector<int16_t> decoded;
    if (decode_bayer_10p(packed, decoded) != HELPER_SUCCESS) {
        std::cout << "[ROUND TRIP TEST] Failed unpacking packed data.\n";
        return false;
    }

    for (size_t i = 0; i < original.size(); ++i) {
        if (decoded[i] != original[i]) {
            std::cout << "[ROUND TRIP TEST] round-trip error at i = " << i << " data=" << decoded[i] << ", reference=" << original[i] << '\n';
            return false;
        }
    }
    std::cout << "[ROUND TRIP TEST] ok.\n";
    return true;
}

/**
 * Some negative test to make sure guard is guarding.
 */
bool check_partial_group_rejection() {
    std::vector<int16_t> decoded{};
    // 4th group with only 2 bytes.
    std::vector<uint8_t> const short_packed(bayer10p::BYTES_PER_GROUP * 3U + 2U);
    if (decode_bayer_10p(short_packed, decoded) != HELPER_FAILURE) {
        std::cout << "[PARTIAL GROUP TEST] decode_bayer_10p accepted " << short_packed.size() << " bytes(not divisible by "
                  << bayer10p::BYTES_PER_GROUP << "). Who removed my guard??\n";
        return false;
    }

    std::vector<uint8_t> const short_unpacked(7U);
    if (decode_bayer_10(short_unpacked, decoded) != HELPER_FAILURE) {
        std::cout << "[PARTIAL GROUP TEST] decode_bayer_10 accepted " << short_unpacked.size() << " bytes(not divisible by "
                  << bayer10p::UNPACKED_BYTES_PER_PIXEL << "). Who removed my guard?\n";
        return false;
    }

    std::vector<uint8_t> const empty_frame;
    if (decode_bayer_10p(empty_frame, decoded) != HELPER_FAILURE) {
        std::cout << "[PARTIAL GROUP TEST] decode_bayer_10p accepted an empty frame. No good, who removed my guard??\n";
        return false;
    }

    // Might as well test the packer
    std::vector<int16_t> const short_pixels(bayer10p::PIXELS_PER_GROUP * 10U + 1U);
    std::vector<uint8_t>       packed{};
    if (pack_bayer_10p(short_pixels, packed) != HELPER_FAILURE) {
        std::cout << "[PARTIAL GROUP TEST] pack_bayer_10p accepted " << short_pixels.size() << " pixels(not divisible by "
                  << bayer10p::PIXELS_PER_GROUP << "). Who removed my guard?\n";
        return false;
    }

    std::cout << "[PARTIAL GROUP TEST] ok.\n";
    return true;
}

/**
 * Test made for decoding logic of 10bit-packed bayer frames.
 * The high byte of decoded must equate exactly to that from raw.
 */
bool check_high_byte(char const* raw_path) {
    std::ifstream file(raw_path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file) {
        std::cout << "[HIGH BYTE TEST] Failed opening raw frame: " << raw_path << '\n';
        return false;
    }
    auto const num_bytes = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> raw(static_cast<size_t>(num_bytes));
    if (!file.read(reinterpret_cast<char*>(raw.data()), num_bytes)) {
        std::cout << "[HIGH BYTE TEST] Failed reading raw data from " << raw_path << '\n';
        return false;
    }

    std::vector<int16_t> decoded;
    if (decode_bayer_10p(raw, decoded) != HELPER_SUCCESS) {
        std::cout << "[HIGH BYTE TEST] Unpacking " << raw_path << " failed\n";
        return false;
    }

    for (size_t i = 0, j = 0; i < raw.size(); i += bayer10p::BYTES_PER_GROUP, j += bayer10p::PIXELS_PER_GROUP) {
        for (size_t k = 0; k < bayer10p::PIXELS_PER_GROUP; ++k) {
            auto const high_bits = static_cast<int>(decoded[j + k] >> bayer10p::LOWBITS_NUM);
            auto const raw_byte  = static_cast<int>(raw[i + k]);
            if (high_bits != raw_byte) {
                std::cout << "[HIGH BYTE TEST] sample " << (j + k) << " (group " << (i / bayer10p::BYTES_PER_GROUP) << ", cell " << k
                          << "): decoded=" << decoded[j + k] << " has high byte " << high_bits << ", raw byte is " << raw_byte << '\n';
                return false;
            }
        }
    }

    std::cout << "[HIGH BYTE TEST] ok.\n";
    return true;
}

}


int main(int argc, char** argv) {
    // test bayer decoding
    bool allgood = check_round_trip();

    // test input size validity check
    allgood &= check_partial_group_rejection();

    // test foundational unpacking (high-bit only)
    if (argc > 1) {
        allgood &= check_high_byte(argv[1]);
    } else {
        std::cout << "[HIGH BYTE TEST] Detected no input frame test. Skipping.\n";
    }

    // test bayer phase indexing
    constexpr std::array<order_comb, 4> ORDER_COMBOS{
        {{bayer_order::RGGB, "RGGB"}, {bayer_order::GRBG, "GRBG"}, {bayer_order::GBRG, "GBRG"}, {bayer_order::BGGR, "BGGR"}}
    };
    for (auto const& combo : ORDER_COMBOS) {
        allgood &= check_ordering(combo);
    }

    if (!allgood) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
