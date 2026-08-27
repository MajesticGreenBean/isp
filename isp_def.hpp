#ifndef GREEN_SIMPLE_ISP_DEF_HPP_INCLUDED
#define GREEN_SIMPLE_ISP_DEF_HPP_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>

namespace bayer10p {
inline constexpr size_t   PIXELS_PER_GROUP         = 4;
inline constexpr size_t   BYTES_PER_GROUP          = 5;
inline constexpr size_t   UNPACKED_BYTES_PER_PIXEL = 2; // 10-bits to be exact.
inline constexpr size_t   LOWBITS_BYTE_ORD         = BYTES_PER_GROUP - 1;
inline constexpr uint16_t LOWBITS_NUM              = 2U;
inline constexpr uint16_t LOWBITS_MASK             = 0b11U;
inline constexpr size_t   NUM_PHASES               = 4;
inline constexpr double   EPSILON                  = 0.000001;
inline constexpr size_t   CELL_DIM                 = 2;
inline constexpr size_t   CCM_RED_ROW              = 0;
inline constexpr size_t   CCM_GREEN_ROW            = 1;
inline constexpr size_t   CCM_BLUE_ROW             = 2;
inline constexpr size_t   CCM_RED_COL              = 0;
inline constexpr size_t   CCM_GREEN_COL            = 1;
inline constexpr size_t   CCM_BLUE_COL             = 2;
inline constexpr double   MIN_CHANNEL_MEAN         = 10;  // Below this is probably just black, white balance might crash...
inline constexpr double   MIN_GAIN                 = 0.5; // Below this is just not sensible... (at least from what imx219.json shows)
inline constexpr double   MAX_GAIN                 = 3.5; // Above this is just not sensible... (at least from what imx219.json shows)
inline constexpr double   DEFAULT_ILLUMINANT       = 5858.0;

} // namespace bayer10p

inline constexpr size_t   GRAY_NUM_CHANNELS = 1; // P5
inline constexpr size_t   RGB_NUM_CHANNELS  = 3; // P6
inline constexpr int32_t  ONE_BYTE_MAXVAL   = 255;
inline constexpr int32_t  TWO_BYTE_MAXVAL   = 65535;
inline constexpr uint32_t ONE_BYTE_SHIFT    = 8U;
inline constexpr uint32_t ONE_BYTE_MASK     = 0xFFU;

inline constexpr size_t CCM_MAT_DIM         = 3;
inline constexpr size_t CCM_NUM_ILLUMINANTS = 6;

using CcmMat_t = std::array<std::array<double, CCM_MAT_DIM>, CCM_MAT_DIM>;

struct CcmEntry_t {
    double   color_temp;  // in Kelvin
    CcmMat_t ccm;
};

struct CtCurvePoint_t {
    double color_temp;  // in Kelvin
    double r_over_g;
    double b_over_g;
};

struct CtEstimate_t {
    double from_red;    // kelvin implied by r/g alone
    double from_blue;   // kelvin implied by b/g alone
    double spread;      // absolute difference |from_red - from_blue|
    bool   red_valid;
    bool   blue_valid;
};

/**
 * Verbatim from libcamera's IMX219 'ref_imx219.json'(rpi.ccm key) file.
 * https://github.com/raspberrypi/libcamera/blob/main/src/ipa/rpi/vc4/data/imx219.json.
 * TODO: Look into calibration? not sure yet how to do that.
 */
// clang-format off
inline constexpr std::array<CcmEntry_t, CCM_NUM_ILLUMINANTS> CCM_TABLE{{
    {2860.0, {{{ 2.12089, -0.52461, -0.59629}, {-0.85342,  2.80445, -0.95103}, {-0.26897, -1.14788,  2.41685}}}},
    {2960.0, {{{ 2.26962, -0.54174, -0.72789}, {-0.77008,  2.60271, -0.83262}, {-0.26036, -1.51254,  2.77289}}}},
    {3603.0, {{{ 2.18644, -0.66148, -0.52496}, {-0.77828,  2.69474, -0.91645}, {-0.25239, -0.83059,  2.08398}}}},
    {4650.0, {{{ 2.18174, -0.70887, -0.47287}, {-0.70196,  2.76426, -1.06231}, {-0.25157, -0.71978,  1.97135}}}},
    {5858.0, {{{ 2.32392, -0.88421, -0.43971}, {-0.63821,  2.58348, -0.94527}, {-0.28541, -0.54112,  1.82653}}}},
    {7580.0, {{{ 2.21175, -0.53242, -0.67933}, {-0.57875,  3.07922, -1.50047}, {-0.27709, -0.73338,  2.01048}}}},
}};


/**
 * The awb reference curve verbatim from libcamera's IMX219 'ref_imx219.json' (key rpi.nn.awb.ct_curve).
 * https://github.com/raspberrypi/libcamera/blob/main/src/ipa/rpi/vc4/data/imx219.json.
 * Although I already implemented the naive grey-world.
 */
inline constexpr std::array<CtCurvePoint_t, CCM_NUM_ILLUMINANTS> CT_CURVE{{
    {2860.0, 0.9514, 0.4156},
    {2960.0, 0.9289, 0.4372},
    {3603.0, 0.8305, 0.5251},
    {4650.0, 0.6756, 0.6433},
    {5858.0, 0.6193, 0.6807},
    {7580.0, 0.5019, 0.7495},
}};
// clang-format on

#define HELPER_SUCCESS (0U)
#define HELPER_FAILURE (1U)
#endif // GREEN_SIMPLE_ISP_DEF_HPP_INCLUDED
