#include "isp.hpp"
#include "isp_def.hpp"
#include <iostream>

/**
 * Following Linux Kernel's documentation:
 * https://www.kernel.org/doc/html/v6.12/userspace-api/media/v4l/pixfmt-srggb10.html
 */
int decode_bayer_10([[maybe_unused]] std::vector<std::uint8_t> const& raw_bytes, [[maybe_unused]] std::vector<std::int16_t>& decoded_bytes) {
    // TODO: Reject partial groups for now, future can find a way to handle.
    if (raw_bytes.size() == 0 || (raw_bytes.size() % bayer10p::UNPACKED_BYTES_PER_PIXEL) != 0) {
        std::cout << "unpacked input size must be divisible by " << bayer10p::UNPACKED_BYTES_PER_PIXEL << "(bytes), instead, got " << raw_bytes.size()
                  << '\n';
        return HELPER_FAILURE;
    }
    decoded_bytes.resize(raw_bytes.size() / 2);
    for (size_t i = 0, j = 0; i < raw_bytes.size(); i += 2, ++j) {
        decoded_bytes[j] = static_cast<int16_t>((raw_bytes[i]) | (static_cast<int16_t>(raw_bytes[i + 1]) << 8U));
    }
    return HELPER_SUCCESS;
}

/**
 * Following Linux Kernel's documentation:
 * https://www.kernel.org/doc/html/v6.12/userspace-api/media/v4l/pixfmt-srggb10p.html. RPi capturing
 * these image were flashed with 6.12.
 *
 * Decoding output are int16_t instead of unsigned because each value are 10-bits max. (and we also cap multiplicative factors
 * later).
 */
int decode_bayer_10p(std::vector<std::uint8_t> const& raw_bytes, std::vector<std::int16_t>& decoded_bytes) {
    // TODO: Reject partial groups for now, future can find a way to handle.
    if (raw_bytes.size() == 0 || (raw_bytes.size() % bayer10p::BYTES_PER_GROUP) != 0) {
        std::cout << "packed input size must be divisible by " << bayer10p::BYTES_PER_GROUP << "(bytes), instead, got " << raw_bytes.size() << '\n';
        return HELPER_FAILURE;
    }

    decoded_bytes.resize(bayer10p::PIXELS_PER_GROUP * raw_bytes.size() / bayer10p::BYTES_PER_GROUP);

    for (size_t i = 0, j = 0; i < raw_bytes.size(); i += bayer10p::BYTES_PER_GROUP, j += bayer10p::PIXELS_PER_GROUP) {
        std::int16_t const lowbit_byte = raw_bytes[i + bayer10p::LOWBITS_BYTE_ORD];

        decoded_bytes[j + 3] =
            static_cast<int16_t>(((lowbit_byte >> 6U) & bayer10p::LOWBITS_MASK) | ((int16_t)raw_bytes[i + 3U] << bayer10p::LOWBITS_NUM));
        decoded_bytes[j + 2] =
            static_cast<int16_t>(((lowbit_byte >> 4U) & bayer10p::LOWBITS_MASK) | ((int16_t)raw_bytes[i + 2U] << bayer10p::LOWBITS_NUM));
        decoded_bytes[j + 1] =
            static_cast<int16_t>(((lowbit_byte >> 2U) & bayer10p::LOWBITS_MASK) | ((int16_t)raw_bytes[i + 1U] << bayer10p::LOWBITS_NUM));
        decoded_bytes[j] = static_cast<int16_t>((lowbit_byte & bayer10p::LOWBITS_MASK) | ((int16_t)raw_bytes[i] << bayer10p::LOWBITS_NUM));
    }
    return HELPER_SUCCESS;
}

/**
 * Re-pack an unpacked bayer 10p frame back to the packed version.
 * useful to test out different implementations of unpacking (round-trip difference shoul be 0).
 * https://www.kernel.org/doc/html/v6.12/userspace-api/media/v4l/pixfmt-srggb10p.html.
 */
int pack_bayer_10p(std::vector<int16_t> const& pixels, std::vector<uint8_t>& packed) {
    if (pixels.size() == 0 || (pixels.size() % bayer10p::PIXELS_PER_GROUP) != 0) {
        std::cout << "unpacked input must be divisible by " << bayer10p::PIXELS_PER_GROUP << "(pixels), instead, got " << pixels.size() << '\n';
        return HELPER_FAILURE;
    }

    if (pixels.size() == 0) {
        std::cout << "Pixel count is 0, something's wrong.\n";
        return HELPER_FAILURE;
    }
    packed.assign(pixels.size() / bayer10p::PIXELS_PER_GROUP * bayer10p::BYTES_PER_GROUP, 0U);
    uint32_t lowbits = 0U;
    for (size_t i = 0, j = 0; j < pixels.size(); i += bayer10p::BYTES_PER_GROUP, j += bayer10p::PIXELS_PER_GROUP) {
        for (size_t k = 0; k < bayer10p::PIXELS_PER_GROUP; ++k) {
            auto const pix  = pixels[j + k];
            packed[i + k]   = static_cast<uint8_t>((static_cast<uint16_t>(pix) >> bayer10p::LOWBITS_NUM) & ONE_BYTE_MASK);
            lowbits        |= static_cast<uint32_t>(pix & bayer10p::LOWBITS_MASK) << (bayer10p::LOWBITS_NUM * k);
        }
        packed[i + bayer10p::LOWBITS_BYTE_ORD] = static_cast<uint8_t>(lowbits);
        lowbits                                = 0;
    }
    return HELPER_SUCCESS;
}
