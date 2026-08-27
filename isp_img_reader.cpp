#include "isp.hpp"
#include "isp_def.hpp"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

/**
 * https://netpbm.sourceforge.net/doc/pbm.html.
 */
int read_header_value(std::istream& in, int64_t& value) {
    char c = 0;

    // ==============================================================================================
    // At the start, ignore comments, whitespaces.
    // ==============================================================================================
    while (in.get(c)) {
        if (c == '#') {
            while (in.get(c) && c != '\n') {}
        } else if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            break;
        }
    }

    if (!in || std::isdigit(static_cast<unsigned char>(c)) == 0) {
        std::cout << "First value of file isn't a header-value (non-negative integer), please refer to netpbm format.\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // Found our guy, read him until delimit (whitespace).
    // ==============================================================================================
    value = 0;
    do {
        value = value * 10 + (c - '0');
    } while (in.get(c) && std::isdigit(static_cast<unsigned char>(c)) != 0);

    return HELPER_SUCCESS;
}

/**
 * Read the filetype.
 * P5 = pgm.
 * P6 = ppm.
 * https://netpbm.sourceforge.net/doc/pbm.html.
 */
int read_magic(std::istream& in, size_t& nchannels) {
    char magic_p   = '\0';
    char magic_num = '\0';
    if (!in.get(magic_p) || !in.get(magic_num) || magic_p != 'P') {
        std::cout << "First non-neg int value of header should either be P5 or P6. Please recheck.\n";
        return HELPER_FAILURE;
    }
    if (magic_num == '5') {
        nchannels = GRAY_NUM_CHANNELS;
        return HELPER_SUCCESS;
    }
    if (magic_num == '6') {
        nchannels = RGB_NUM_CHANNELS;
        return HELPER_SUCCESS;
    }
    std::cout << "First non-neg int value of header should either be P5 or P5. Please recheck.\n";
    return HELPER_FAILURE;
}

} // namespace

/**
 * Read a .pgm or .ppm image.
 * Reading is into an unpacked 10-bits format, so 2bytes array in-memory.
 */
int read_simple_image(std::string const& filename, std::vector<int16_t>& samples, OutImgSpec& spec) {
    std::ifstream in(filename, std::ios_base::in | std::ios_base::binary);
    if (!in) {
        std::cout << "Failed to open " << filename << " for reading.\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // Find the 'P5' or 'P6'.
    // ==============================================================================================
    if (read_magic(in, spec.nchannels) != HELPER_SUCCESS) {
        std::cout << "Unrecognized filetype/fileheader inside" << filename << " (expected P5 or P6) as first non-comment, non-whitespace value.\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // Header should be P5/P6 width height maxval separated by any whitespace, in that EXACT order.
    // ==============================================================================================
    int64_t width  = 0;
    int64_t height = 0;
    int64_t maxval = 0;
    if (read_header_value(in, width) == HELPER_FAILURE) {
        std::cout << "Reading width failed, header is probably bad.\n";
        return HELPER_FAILURE;
    }
    if (read_header_value(in, height) == HELPER_FAILURE) {
        std::cout << "Reading height failed, header is probably bad.\n";
        return HELPER_FAILURE;
    }
    if (read_header_value(in, maxval) == HELPER_FAILURE) {
        std::cout << "Reading maxval failed, header is probably bad.\n";
        return HELPER_FAILURE;
    }

    if (width <= 0 || height <= 0) {
        std::cout << filename << " specified invalid geometry (" << width << "x" << height << ")\n";
        return HELPER_FAILURE;
    }

    // TODO: do we ever need to support higher?
    if (maxval <= 0 || maxval > std::numeric_limits<int16_t>::max()) {
        std::cout << filename << " specified an invalid maxval: " << maxval << " (supported range is 1.." << std::numeric_limits<int16_t>::max()
                  << '\n';
        return HELPER_FAILURE;
    }

    spec.w                        = static_cast<size_t>(width);
    spec.h                        = static_cast<size_t>(height);
    spec.maxval                   = static_cast<int32_t>(maxval);
    size_t const num_samples      = spec.w * spec.h * spec.nchannels;
    size_t       bytes_per_sample = 0;
    if (spec.maxval > ONE_BYTE_MAXVAL) {
        bytes_per_sample = 2U;
    } else {
        bytes_per_sample = 1U;
    }

    std::vector<uint8_t> raw(num_samples * bytes_per_sample);
    if (!in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()))) {
        std::cout << filename << "'s header declares " << raw.size() << " bytes of data. However, file on disk has size " << in.gcount()
                  << " bytes\n";
        return HELPER_FAILURE;
    }

    samples.resize(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        if (bytes_per_sample == 1U) {
            samples[i] = static_cast<int16_t>(raw[i]);
        } else {
            // Netpbm 16-bit samples are big-endian, need to shift.
            samples[i] = static_cast<int16_t>((static_cast<uint32_t>(raw[i * 2U]) << ONE_BYTE_SHIFT) |
                                              (static_cast<uint32_t>(raw[i * 2U + 1U]) & ONE_BYTE_MASK));
        }
    }
    return HELPER_SUCCESS;
}
