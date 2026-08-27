#include "isp.hpp"
#include "isp_def.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int validate_specs(std::string const& filename, std::vector<int16_t> const& samples, OutImgSpec const& spec) {
    if (spec.nchannels != GRAY_NUM_CHANNELS && spec.nchannels != RGB_NUM_CHANNELS) {
        std::cout << filename << ": num channels must be " << GRAY_NUM_CHANNELS << "(P5, pgm) or ";
        std::cout << RGB_NUM_CHANNELS << "(P6, ppm). Instead got: " << spec.nchannels << '\n';
        return HELPER_FAILURE;
    }

    if (spec.h == 0 || spec.w == 0) {
        std::cout << filename << ": detected 0-sized dimension(s): (" << spec.w << "x" << spec.h << ")\n";
        return HELPER_FAILURE;
    }

    if (spec.maxval < 1 || spec.maxval > TWO_BYTE_MAXVAL) {
        std::cout << filename << ": maxval must stay within 2-bytes representable: [1," << TWO_BYTE_MAXVAL << "].";
        std::cout << " Instead got: " << spec.maxval << '\n';
        return HELPER_FAILURE;
    }

    uint64_t expected_size = spec.w * spec.h * spec.nchannels;
    if (expected_size != samples.size()) {
        std::cout << filename << "'s header declared " << spec.w << "x" << spec.h << "x" << spec.nchannels << " = ";
        std::cout << expected_size << " samples, but instead " << samples.size() << " was provided to writer\n";
        return HELPER_FAILURE;
    }

    return HELPER_SUCCESS;
}

std::string build_header_string(OutImgSpec const& spec) {
    std::string magic = "P5";
    if (spec.nchannels == RGB_NUM_CHANNELS) {
        magic = "P6";
    }
    return magic + '\n' + std::to_string(spec.w) + ' ' + std::to_string(spec.h) + '\n' + std::to_string(spec.maxval) + '\n';
}

std::vector<uint8_t> serialize(std::vector<int16_t> const& samples, OutImgSpec const& spec) {
    bool   needs_two_bytes_per_sample = spec.maxval > ONE_BYTE_MAXVAL;
    size_t out_size                   = samples.size();
    if (needs_two_bytes_per_sample) {
        out_size *= 2;
    }
    std::vector<uint8_t> serialized_samples(out_size);

    // At output, no more negatives representable.
    // Also, for 2 bytes, it is big-endian.
    if (needs_two_bytes_per_sample) {
        for (size_t i = 0, j = 0; i < samples.size(); ++i) {
            auto const clamped      = std::clamp(static_cast<int32_t>(samples[i]), int32_t{0}, spec.maxval);
            serialized_samples[j++] = static_cast<uint8_t>((static_cast<uint32_t>(clamped) >> ONE_BYTE_SHIFT) & ONE_BYTE_MASK);
            serialized_samples[j++] = static_cast<uint8_t>(static_cast<uint32_t>(clamped) & ONE_BYTE_MASK);
        }
    } else {
        for (size_t i = 0; i < samples.size(); ++i) {
            serialized_samples[i] = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(samples[i]), int32_t{0}, spec.maxval));
        }
    }
    return serialized_samples;
}
} // namespace

// "Simple" as in the very simple PPM and PGM format.
// TODO: Support for others is....for other time :D.
int write_simple_image(std::string const& filename, std::vector<int16_t> const& samples, OutImgSpec const& spec) {
    if (validate_specs(filename, samples, spec) != HELPER_SUCCESS) {
        return HELPER_FAILURE;
    }

    std::ofstream outfile_stream(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outfile_stream) {
        std::cout << "Failed to open " << filename << " for writing (permission? missing?)\n";
        return HELPER_FAILURE;
    }

    auto const header = build_header_string(spec);
    if (!outfile_stream.write(header.data(), static_cast<std::streamsize>(header.size()))) {
        std::cout << "Failed during writing of header to file " << filename << '\n';
        return HELPER_FAILURE;
    }

    auto const bytes = serialize(samples, spec);
    if (!outfile_stream.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        std::cout << "Failed writing " << bytes.size() << " bytes of image data to file " << filename << '\n';
        return HELPER_FAILURE;
    }

    outfile_stream.flush();
    if (!outfile_stream) {
        std::cout << "Failed to fush file stream for " << filename << '\n';
        return HELPER_FAILURE;
    }

    return HELPER_SUCCESS;
}
