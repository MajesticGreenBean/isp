#ifndef GREEN_SIMPLE_ISP_HPP_INCLUDED
#define GREEN_SIMPLE_ISP_HPP_INCLUDED

#include "isp_bayer.hpp"
#include "isp_def.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct OutImgSpec {
    size_t  w;
    size_t  h;
    size_t  nchannels;
    int32_t maxval;
};

struct InputSetting {
    bayer_order order;
    Geom_t      geom;
};

constexpr inline InputSetting DEFAULT_INPUT_SETTINGS{.order = DEFAULT_BAYER_ORDER, .geom = DEFAULT_GEOM};

int decode_bayer_10(std::vector<std::uint8_t> const& raw_bytes, std::vector<std::int16_t>& decoded_bytes);
int decode_bayer_10p(std::vector<std::uint8_t> const& raw_bytes, std::vector<std::int16_t>& decoded_bytes);
int pack_bayer_10p(std::vector<int16_t> const& pixels, std::vector<uint8_t>& packed);

int demosaic_nn_bayer10p(std::vector<int16_t> const& bayer_bytes, std::vector<std::int16_t>& demosaiced_bytes, InputSetting const& settings);
int demosaic_bilinear_bayer10p(std::vector<int16_t> const& bayer_bytes, std::vector<int16_t>& demosaiced_bytes, InputSetting const& settings);

int get_channel_means(std::vector<std::int16_t> const& bayer_bytes, std::array<double, bayer10p::NUM_PHASES>& means, InputSetting const& setting);
int level_black(std::vector<std::int16_t> const& bayer_bytes, std::array<int32_t, bayer10p::NUM_PHASES> const& black_level_means,
                std::vector<std::int16_t>& leveled_bytes, InputSetting const& settings);

int measure_wb_grey_world(std::vector<int16_t> const& in_bytes, std::array<double, bayer10p::NUM_PHASES>& gains, InputSetting const& settings);
int apply_wb_gains(std::vector<int16_t> const& in_bytes, std::array<double, bayer10p::NUM_PHASES> gains, std::vector<int16_t>& white_balanced_bytes,
                   InputSetting const& settings);

int get_ccm(double const ct, CcmMat_t& out);
int apply_ccm(std::vector<int16_t> const& in_pixels, CcmMat_t const& ccm, std::vector<int16_t>& cor_pixels);
int estimate_ct_by_gain(std::array<double, bayer10p::NUM_PHASES> const& gains, CtEstimate_t& out);

int write_simple_image(std::string const& filename, std::vector<int16_t> const& samples, OutImgSpec const& spec);
int read_simple_image(std::string const& filename, std::vector<int16_t>& samples, OutImgSpec& spec);

int encode_gamma(std::vector<std::int16_t> const& bayer_pixels, std::vector<std::int16_t>& screen_pixels, int32_t white_point);
#endif // GREEN_SIMPLE_ISP_HPP_INCLUDED
