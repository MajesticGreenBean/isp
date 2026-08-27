#include "isp.hpp"
#include "isp_def.hpp"
#include <cmath>
#include <vector>
#include <iostream>

namespace {
double dot_product_ccm(int16_t const r, int16_t const g, int16_t const b, std::array<double, CCM_MAT_DIM> const& ccm_row) {
    return (double)r * ccm_row[bayer10p::CCM_RED_COL] + (double)g * ccm_row[bayer10p::CCM_GREEN_COL] + (double)b * ccm_row[bayer10p::CCM_BLUE_COL];
}

/**
 * Apparently, the better way to interpolate color temperature is in mired/reciprocal. 
 * https://en.wikipedia.org/wiki/Correlated_color_temperature.
 * https://en.wikipedia.org/wiki/Mired shows isotherm has nice uniform spacing on this mired scale.
 */
constexpr double to_meg_recip(double const val) {
    return 1.0e6 / val;
}

/**
 * Given channel gain, trynna invert to see which color temperature curve it's on.
 * As color temp increases: * r/g decreases,  b/g increases.
 * Negating r/g helps simplify inteperlation code.
 */
int get_ct_by_gain(double const gain, bool const is_red, double& ct) {
    auto get_gain = [is_red](CtCurvePoint_t const& curve) { return is_red ? -curve.r_over_g : curve.b_over_g; };
    double eff_gain = gain;
    if (is_red) {
        eff_gain = -gain;
    }

    // Gain is outside of available reference range.
    if (eff_gain <= get_gain(CT_CURVE.front()) || eff_gain >= get_gain(CT_CURVE.back())) {
        return HELPER_FAILURE;
    }

    for (size_t i = 1; i < CCM_NUM_ILLUMINANTS; ++i) {
        auto const& lo = CT_CURVE[i - 1];
        auto const& hi = CT_CURVE[i];
        if (eff_gain > get_gain(hi)) {
            continue;
        }

        double const dgain = get_gain(hi) - get_gain(lo);
        double weight = 0;
        if (std::abs(dgain) > std::numeric_limits<double>::epsilon()) {
            weight = (eff_gain - get_gain(lo)) / dgain;
        }
        double const mired_lo = to_meg_recip(lo.color_temp);
        double const mired_hi = to_meg_recip(hi.color_temp);
        ct = to_meg_recip(mired_lo + weight * (mired_hi - mired_lo));
        return HELPER_SUCCESS;
    }
    return HELPER_FAILURE;
}

}

int apply_ccm(std::vector<int16_t> const& in_pixels, CcmMat_t const& ccm, std::vector<int16_t>& cor_pixels) {
    auto const num_pixels = in_pixels.size();
    if (num_pixels < 3) {
        return HELPER_FAILURE; // Something seriously wrong.
    }
    cor_pixels.resize(num_pixels);
    for (size_t i = 0; i <= num_pixels - 3; i += 3) {
        // RED
        cor_pixels[i] =
            (int16_t)std::round(dot_product_ccm(in_pixels[i], in_pixels[i + 1], in_pixels[i + 2], ccm[bayer10p::CCM_RED_ROW]));
        // GREEN
        cor_pixels[i + 1] =
            (int16_t)std::round(dot_product_ccm(in_pixels[i], in_pixels[i + 1], in_pixels[i + 2], ccm[bayer10p::CCM_GREEN_ROW]));
        // BLUE
        cor_pixels[i + 2] =
            (int16_t)std::round(dot_product_ccm(in_pixels[i], in_pixels[i + 1], in_pixels[i + 2], ccm[bayer10p::CCM_BLUE_ROW]));
    }

    return HELPER_SUCCESS;
}

/**
 * Look up the ccm given the color temperature.
 * if exact is available from rpi's imx219 calibration file, yay.
 * else do a simple linear interpolation.
 */
int get_ccm(double const ct, CcmMat_t& out) {
    if (ct <= 0.0) {
        std::cout << "select_ccm: colour temperature must be positive, got " << ct << '\n';
        return HELPER_FAILURE;
    }

    // Outside range, just clamp.
    if (ct <= CCM_TABLE.front().color_temp) {
        out = CCM_TABLE.front().ccm;
        return HELPER_SUCCESS;
    }
    if (ct >= CCM_TABLE.back().color_temp) {
        out = CCM_TABLE.back().ccm;
        return HELPER_SUCCESS;
    }

    for (size_t i = 1; i < CCM_NUM_ILLUMINANTS; ++i) {
        auto const& lo = CCM_TABLE[i - 1];
        auto const& hi = CCM_TABLE[i];
        if (ct > hi.color_temp) {
            continue;
        }

        auto const mired    = to_meg_recip(ct);
        auto const mired_lo = to_meg_recip(lo.color_temp);
        auto const mired_hi = to_meg_recip(hi.color_temp);
        auto const weight   = (mired_lo - mired) / (mired_lo - mired_hi);  // mired is reciprocal, lo is higher here.

        for (size_t i = 0; i < CCM_MAT_DIM; ++i) {
            for (size_t j = 0; j < CCM_MAT_DIM; ++j) {
                out[i][j] = lo.ccm[i][j] + weight * (hi.ccm[i][j] - lo.ccm[i][j]);
            }
        }
        break;
    }

    return HELPER_SUCCESS;
}

/**
 * Estimate the illuminant from the pipeline's calculated white-balance gains.
 */
int estimate_ct_by_gain(std::array<double, bayer10p::NUM_PHASES> const& gains, CtEstimate_t& out) {
    out = CtEstimate_t{};

    auto const red_gain  = gains[static_cast<size_t>(bayer_phase::red)];
    auto const blue_gain = gains[static_cast<size_t>(bayer_phase::blue)];
    if (red_gain <= 0.0 || blue_gain <= 0.0) {
        return HELPER_FAILURE;
    }

    // gain = G_ref / mean, curve is color_temp vs mean / ref, so reciprocal.
    out.red_valid  = (get_ct_by_gain(1.0 / red_gain,  true,  out.from_red)  == HELPER_SUCCESS);
    out.blue_valid = (get_ct_by_gain(1.0 / blue_gain, false, out.from_blue) == HELPER_SUCCESS);

    if (out.red_valid && out.blue_valid) {
        out.spread = std::fabs(out.from_red - out.from_blue);
    }
    return HELPER_SUCCESS;
}
