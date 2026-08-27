#include "isp.hpp"
#include "isp_bayer.hpp"
#include "isp_def.hpp"
#include "isp_pipeline.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct DumpConfig_t {
    bool     on{false};
    fs::path dir;
    fs::path stem;
};

inline constexpr std::string_view DEFAULT_OUTPUT_DIR = "pipeline_out";
inline constexpr int32_t  WHITE_POINT_10BITS       = (1U << 10U) - 1;

// Can't be bothered with more detailed prints, like this first.
bool is_error(int32_t errcode, std::string_view func) {
    if (errcode != HELPER_SUCCESS) {
        std::cout << "Failed: " << func << '\n';
        return true;
    }
    return false;
}

int read_byte_blob(fs::path const& filename, std::vector<std::uint8_t>& out_buf) {
    std::ifstream input_fs(filename, std::ios_base::in | std::ios_base::ate | std::ios_base::binary);
    if (!input_fs) {
        std::cout << "failed creating input_fs at " << __PRETTY_FUNCTION__ << ": " << __LINE__ << '\n';
        return HELPER_FAILURE;
    }

    std::streamsize const input_fs_size = input_fs.tellg();
    if (input_fs_size < 0) {
        std::cout << "streamsize not right at " << __PRETTY_FUNCTION__ << ": " << __LINE__ << '\n';
        return HELPER_FAILURE;
    }

    out_buf.resize(static_cast<size_t>(input_fs_size));
    input_fs.seekg(0);
    if (!input_fs.read(reinterpret_cast<char*>(out_buf.data()), input_fs_size)) {
        std::cout << "read " << input_fs_size << " bytes from " << filename << " failed at " << __PRETTY_FUNCTION__ << ": " << __LINE__ << '\n';
        return HELPER_FAILURE;
    }

    if (input_fs.tellg() != input_fs_size) {
        std::cout << "read " << input_fs.tellg() << " bytes from " << filename << " , which is different from fliesize detected (" << input_fs_size
                  << ") at " << __PRETTY_FUNCTION__ << ": " << __LINE__ << '\n';
        return HELPER_FAILURE;
    }
    return HELPER_SUCCESS;
}

/**
 * Too many args but it's kinda convenient kek.
 */
int32_t dump_stage(std::vector<int16_t> const& samples, DumpConfig_t const& d_cfg, std::string_view prefix, std::string_view suffix,
                   OutImgSpec const& img_spec, std::string_view label) {
    if (!d_cfg.on) {
        return HELPER_SUCCESS;
    }

    auto const filepath = (d_cfg.dir / (std::string{prefix} + d_cfg.stem.string() + std::string{suffix})).string();
    if (write_simple_image(filepath, samples, img_spec) != HELPER_SUCCESS) {
        std::cout << "Failed to create " << label << "o output\n";
        return HELPER_FAILURE;
    }
    std::cout << "Created " << label << " preview " << filepath << '\n';
    return HELPER_SUCCESS;
}

// Create the directory and save the names into dcfg. Kinda mesys but eh
int create_output_dir(fs::path const& out_path, DumpConfig_t& d_cfg) {
    d_cfg.stem = out_path.stem();
    d_cfg.dir  = out_path.parent_path();
    if (d_cfg.dir.empty()) {
        d_cfg.dir = DEFAULT_OUTPUT_DIR;
    }
    std::error_code ec;
    fs::create_directories(d_cfg.dir, ec);
    if (ec) {
        std::cout << "Output dir creation failed.\nCategory: " << ec.category().name() << '\n';
        std::cout << "Value: " << ec.value() << "\nMessage: " << ec.message() << '\n';
        return HELPER_FAILURE;
    }

    std::cout << "Output dir " << d_cfg.dir << " is here.\n";
    return HELPER_SUCCESS;
}

/**
 * Validate if requested configuration fits the input data.
 */
bool is_valid_input_size(size_t const in_bytes_size, inp_format const format, fs::path const& file, Geom_t const& geom) {
    size_t expected_bytes = 0;
    if (format == inp_format::PACKED) {
        expected_bytes = geom.height * (geom.width * bayer10p::BYTES_PER_GROUP / bayer10p::PIXELS_PER_GROUP);
    } else {
        expected_bytes = geom.height * geom.width * bayer10p::UNPACKED_BYTES_PER_PIXEL;
    }

    if (in_bytes_size != expected_bytes) {
        std::cout << file << " is " << in_bytes_size << " bytes, but " << geom.width << "x" << geom.height << " needs " << expected_bytes << ".\n";
        return false;
    }
    return true;
}

int measure_black_level(fs::path const& dark_frame, InputSetting const& settings, std::array<int32_t, bayer10p::NUM_PHASES>& blv_means) {
    std::vector<std::uint8_t> black_raw_bytes;
    if (read_byte_blob(dark_frame, black_raw_bytes) != HELPER_SUCCESS) {
        std::cout << "Failed to read black-bytes. Make sure << dark_frame.string() <<  exists (And it should be packed version :P).\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // Check size vs config compat real quick.
    // ==============================================================================================
    if (!is_valid_input_size(black_raw_bytes.size(), inp_format::PACKED, dark_frame, settings.geom)) {
        std::cout << "Black frame size is not compatible with requested config.\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // Now do actual measuring and leveling.
    // ==============================================================================================
    std::vector<std::int16_t> black_decoded_bytes;
    if (is_error(decode_bayer_10p(black_raw_bytes, black_decoded_bytes), "decode_bayer_10p")) {
        std::cout << "Decoding black frames failed.\n";
        return HELPER_FAILURE;
    }
    std::array<double, bayer10p::NUM_PHASES> dbl_means{};
    if (is_error(get_channel_means(black_decoded_bytes, dbl_means, settings), "get_channel_means")) {
        std::cout << "get channel means for black frame failed\n";
        return HELPER_FAILURE;
    }
    for (size_t i = 0; i < bayer10p::NUM_PHASES; ++i) {
        blv_means[i] = static_cast<int32_t>(std::llround(dbl_means[i]));
    }

    return HELPER_SUCCESS;
}

void print_summary_statistics(std::array<int32_t, bayer10p::NUM_PHASES> const& black_levels, std::array<double, bayer10p::NUM_PHASES> const& gains,
                              int32_t const white_point, double const illuminant, std::vector<int16_t> const& final_rgb) {
    constexpr std::array<char const*, bayer10p::NUM_PHASES> PHASE_NAMES{"R", "Gr", "Gb", "B"};
    constexpr std::array<char const*, RGB_NUM_CHANNELS>     RGB_NAMES{"R", "G", "B"};

    std::cout << std::fixed << std::setprecision(4);

    std::cout << "\n==== pipeline stats ====\n";
    std::cout << "black level: ";
    for (size_t i = 0; i < bayer10p::NUM_PHASES; ++i) {
        std::cout << PHASE_NAMES[i] << '=' << black_levels[i] << ' ';
    }
    std::cout << "\nwb gains: ";
    for (size_t i = 0; i < bayer10p::NUM_PHASES; ++i) {
        std::cout << PHASE_NAMES[i] << '=' << gains[i] << ' ';
    }
    std::cout << "\nwhite point: " << white_point << '\n';

    std::cout << "illuminant : " << illuminant << " K (selected)\n";
    CtEstimate_t ct_est{};
    if (estimate_ct_by_gain(gains, ct_est) == HELPER_SUCCESS && ct_est.red_valid && ct_est.blue_valid) {
        std::cout << "color temperature estimate: r/g=" << ct_est.from_red << "(K), b/g=" << ct_est.from_blue << "(K)\n";
        std::cout << "color temperature estimate spread=" << ct_est.spread << "(K)\n";
    } else {
        std::cout << "color temperature estimate: unavailable (gain outside calibrated range)\n";
    }

    std::array<int64_t, RGB_NUM_CHANNELS> sums{};
    size_t                                clipped_high = 0;
    size_t                                clipped_low  = 0;
    for (size_t i = 0; i < final_rgb.size(); ++i) {
        sums[i % RGB_NUM_CHANNELS] += final_rgb[i];
        if (final_rgb[i] >= ONE_BYTE_MAXVAL) {
            ++clipped_high;
        }
        if (final_rgb[i] <= 0) {
            ++clipped_low;
        }
    }

    auto const channel_size = static_cast<double>(final_rgb.size()) / static_cast<double>(RGB_NUM_CHANNELS);
    auto const total_size   = static_cast<double>(final_rgb.size());
    double     overall      = 0.0;

    std::cout << "\n==== final image ====\n";
    for (size_t c = 0; c < RGB_NUM_CHANNELS; ++c) {
        auto const mean  = static_cast<double>(sums[c]) / channel_size;
        overall         += mean;
        std::cout << RGB_NAMES[c] << " mean=" << mean << '\n';
    }
    overall /= static_cast<double>(RGB_NUM_CHANNELS);

    std::cout << "overall " << (100.0 * overall / ONE_BYTE_MAXVAL) << "% of full scale\n";
    std::cout << "clipped at " << ONE_BYTE_MAXVAL << ": " << (100.0 * static_cast<double>(clipped_high) / total_size)
              << "%\nclipped at 0: " << (100.0 * static_cast<double>(clipped_low) / total_size) << "%\n";

}


} // anonymous namespace

/**
 * Just an enum lookup.
 */
int get_stage_by_name(std::string_view const name, isp_stage& stage) {
    for (auto const& entry : STAGE_NAMES) {
        if (entry.name == name) {
            stage = entry.stage;
            return HELPER_SUCCESS;
        }
    }
    return HELPER_FAILURE;
}

int run_pipeline(PipelineConfig_t const& p_cfg) {
    DumpConfig_t dump_cfg{};
    dump_cfg.on = p_cfg.dump_on;
    if (is_error(create_output_dir(p_cfg.out_base, dump_cfg), "create_output_dir")) {
        std::cout << "failed creating output dir: " << p_cfg.out_base << '\n';
        return HELPER_FAILURE;
    }

    Geom_t const&    geom = p_cfg.setting.geom;
    OutImgSpec const fullres_gray{.w = geom.width, .h = geom.height, .nchannels = GRAY_NUM_CHANNELS, .maxval = WHITE_POINT_10BITS};
    OutImgSpec const nnres_rgb{
        .w = geom.width / bayer10p::CELL_DIM, .h = geom.height / bayer10p::CELL_DIM, .nchannels = RGB_NUM_CHANNELS, .maxval = WHITE_POINT_10BITS};

    // ==============================================================================================
    // Read from disk into memory.
    // TODO: Is there a point where memory isn't enough for bigger res (not on my 5k PC of course).
    // ==============================================================================================
    std::vector<std::uint8_t> raw_bytes;
    if (is_error(read_byte_blob(p_cfg.input_file, raw_bytes), "read_byte_blob")) {
        std::cout << "Failed to read bytes from input\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // Check size vs config compat real quick.
    // ==============================================================================================
    if (!is_valid_input_size(raw_bytes.size(), p_cfg.format, p_cfg.input_file, p_cfg.setting.geom)) {
        std::cout << "Input size is not compatible with requested config.\n";
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // if packed format, unpack.
    // if unpacked format, copy into work array.
    // ==============================================================================================
    std::vector<std::int16_t> work_bytes;
    int                       errcode = HELPER_SUCCESS;
    if (p_cfg.format == inp_format::PACKED) {
        errcode = decode_bayer_10p(raw_bytes, work_bytes);
    } else {
        errcode = decode_bayer_10(raw_bytes, work_bytes);
    }
    if (is_error(errcode, "decode")) {
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // if packed format, unpack.
    // if unpacked format, copy into work array.
    // ==============================================================================================
    if (is_error(dump_stage(work_bytes, dump_cfg, "unpacked_", ".pgm", fullres_gray, "unpacked(pgm)"), "dump_stage_decode")) {
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // test see if nn produces reasonable output.
    // don't early return here, cuz these are just convenient help-with-dev stuffs.
    // ==============================================================================================
    {
        std::vector<int16_t> nn_dm_bytes;
        if (is_error(demosaic_nn_bayer10p(work_bytes, nn_dm_bytes, p_cfg.setting), "demosaic_nn_bayer10p")) {
            std::cout << "[WARN] Failed nn demosaic somehow, continuing pipeline...\n";
        }
        if (is_error(dump_stage(nn_dm_bytes, dump_cfg, "nn_dm_", ".ppm", nnres_rgb, "nearest neighbor demosaic(ppm)"), "dump_stage_nn_dm")) {
            std::cout << "[WARN] Failed dumping out nn demosaic result, continuing pipelne...\n";
        }
    }
    if (p_cfg.stopper == isp_stage::DECODE) {
        return HELPER_SUCCESS;
    }

    // ==============================================================================================
    // black leveling, and also white balance gains
    // (messy, but need it to be here for accurate white points when dumping images).
    // measure then apply.
    // ==============================================================================================
    std::array<int32_t, bayer10p::NUM_PHASES> black_level_means{};
    if (is_error(measure_black_level(p_cfg.black_file, p_cfg.setting, black_level_means), "measure_black_level")) {
        return HELPER_FAILURE;
    }
    std::vector<int16_t> leveled_bytes;
    if (is_error(level_black(work_bytes, black_level_means, leveled_bytes, p_cfg.setting), "level_black")) {
        return HELPER_FAILURE;
    }

    std::array<double, bayer10p::NUM_PHASES> gw_gains{};
    if (is_error(measure_wb_grey_world(leveled_bytes, gw_gains, p_cfg.setting), "measure_wb_grey_world")) {
        return HELPER_FAILURE;
    }
    // white point will be the one with lowest max, this clips highlights but at least doens't change image's coloring.
    int32_t white_point = std::numeric_limits<int32_t>::max();
    for (size_t i = 0; i < bayer10p::NUM_PHASES; ++i) {
        auto const max_reach = (int32_t)(std::llround((double)(WHITE_POINT_10BITS - black_level_means[i]) * gw_gains[i]));
        white_point          = std::min(white_point, max_reach);
    }

    OutImgSpec fullres_gray_lv{.w = geom.width, .h = geom.height, .nchannels = GRAY_NUM_CHANNELS, .maxval = white_point};
    if (is_error(dump_stage(leveled_bytes, dump_cfg, "bl_", ".pgm", fullres_gray_lv, "black-leveld(pgm)"), "dump_stage_black_level")) {
        return HELPER_FAILURE;
    }

    // ==============================================================================================
    // test see if bl produces reasonable output.
    // don't early return here, cuz these are just convenient help-with-dev stuffs.
    // ==============================================================================================
    {
        std::vector<std::int16_t> demosaiced_bytes_leveled;
        if (is_error(demosaic_nn_bayer10p(leveled_bytes, demosaiced_bytes_leveled, p_cfg.setting), "demosaic_nn_bayer10p")) {
            std::cout << "[WARN] nearest-neighbor demosaic of black-leveled bytes failed....Continuing pipieline\n";
        }
        OutImgSpec halfres_rgb_lv{
            .w = geom.width / bayer10p::CELL_DIM, .h = geom.height / bayer10p::CELL_DIM, .nchannels = RGB_NUM_CHANNELS, .maxval = white_point};
        if (is_error(dump_stage(demosaiced_bytes_leveled, dump_cfg, "bl_nn_dm_", ".ppm", halfres_rgb_lv, "black-leveld, nn demosaiced(ppm)"),
                     "dump_stage_bl_nn_dm")) {
            std::cout << "[WARN] dumping result nearest-neighbor demosaic of black-leveled bytes failed....Continuing pipieline\n";
        }
    }
    if (p_cfg.stopper == isp_stage::BLACK_LEVEL) {
        return HELPER_SUCCESS;
    }

    // ==============================================================================================
    // white balance. grey-world here specifically.
    // ==============================================================================================
    std::vector<int16_t> white_balanced_bytes;
    if (is_error(apply_wb_gains(leveled_bytes, gw_gains, white_balanced_bytes, p_cfg.setting), "apply_wb_gains")) {
        std::cout << "Failed at applying white balancing(grey-world).\n";
        return HELPER_FAILURE;
    }
    if (is_error(dump_stage(white_balanced_bytes, dump_cfg, "bl_wb_", ".pgm", fullres_gray_lv, "black-leveled, white-balanced(pgm)"),
                 "dump_stage_bl_wb")) {
        std::cout << "Failed at dumping a black-level, white balanced(grey-world).\n";
        return HELPER_FAILURE;
    }
    if (p_cfg.stopper == isp_stage::WHITE_BALANCE) {
        return HELPER_SUCCESS;
    }

    // ==============================================================================================
    // demosaic. bilinear interpolation.
    // ==============================================================================================
    std::vector<std::int16_t> demosaiced_bytes_bilin;
    if (is_error(demosaic_bilinear_bayer10p(white_balanced_bytes, demosaiced_bytes_bilin, p_cfg.setting), "demosaic_bilinear_bayer10p")) {
        std::cout << "Failed demosaic black-leveled, white-balanced samples with bilinear.\n";
        return HELPER_FAILURE;
    }
    OutImgSpec fullres_rgb_lv{.w = geom.width, .h = geom.height, .nchannels = RGB_NUM_CHANNELS, .maxval = white_point};
    if (is_error(dump_stage(demosaiced_bytes_bilin, dump_cfg, "bl_wb_b_dm_", ".ppm", fullres_rgb_lv,
                            "black-leveled, white-balanced, bilinear demosaiced(ppm)"),
                 "dump_stage_bl_wb_b_dm")) {
        return HELPER_FAILURE;
    }
    if (p_cfg.stopper == isp_stage::DEMOSAIC) {
        return HELPER_SUCCESS;
    }

    // ==============================================================================================
    // color correction.
    // ==============================================================================================
    CcmMat_t ccm{};
    if (is_error(get_ccm(p_cfg.illuminant, ccm), "get_ccm")) {
        std::cout << "Failed finding color correction matrix.\n";
        return EXIT_FAILURE;
    }

    std::vector<std::int16_t> ccm_corrected_bytes;
    if (is_error(apply_ccm(demosaiced_bytes_bilin, ccm, ccm_corrected_bytes), "apply_ccm")) {
        std::cout << "Failed applying color correction matrix.\n";
        return HELPER_FAILURE;
    }
    if (is_error(dump_stage(ccm_corrected_bytes, dump_cfg, "bl_wb_b_dm_ccm_", ".ppm", fullres_rgb_lv,
                            "black-leveled, white-balanced, bilinear demosaiced(ppm), color-corrected"),
                 "dump_stage_bl_wb_b_dm_ccm")) {
        return HELPER_FAILURE;
    }
    if (p_cfg.stopper == isp_stage::CCM) {
        return HELPER_SUCCESS;
    }

    // ==============================================================================================
    // encode gamma.
    // ==============================================================================================
    OutImgSpec                screen_space_spec{.w = geom.width, .h = geom.height, .nchannels = RGB_NUM_CHANNELS, .maxval = ONE_BYTE_MAXVAL};
    std::vector<std::int16_t> gamma_encoded_bytes;
    if (is_error(encode_gamma(ccm_corrected_bytes, gamma_encoded_bytes, white_point), "encode_gamma")) {
        std::cout << "Failed encoding gamma.\n";
        return HELPER_FAILURE;
    }
    if (is_error(dump_stage(gamma_encoded_bytes, dump_cfg, "final_", ".ppm", screen_space_spec,
                            "black-leveled, white-balanced, bilinear demosaiced(ppm), color-corrected"),
                 "dump_stage_final")) {
        std::cout << "Failed to create black-leveled, white-balanced, bilinear, demosaiced, color corrected, gamma(ppm) output\n";
        return HELPER_FAILURE;
    }

    print_summary_statistics(black_level_means, gw_gains, white_point, p_cfg.illuminant, gamma_encoded_bytes);

    return HELPER_SUCCESS;
}
