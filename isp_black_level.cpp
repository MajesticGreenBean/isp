#include "isp.hpp"
#include "isp_bayer.hpp"
#include "isp_def.hpp"
#include <array>
#include <cmath>
#include <iostream>

#define MYINT16_T(x) static_cast<int16_t>(x)
#define MYSIZE_T(x)  static_cast<size_t>(x)

// For simplicity, let's just use a size-3 array as output!
// R-G-B order in memory.
// TODO: implement functions for other image format?? (This is 10bit packed)
// TODO BOUND CHECKING, JUST LIKE ALL THE OTHER FUNCIONS
// TODO: MEAN IS EASY TO DO, BUT MEDIAN MIGHT ACTUALLY BE WHAT WE WANT...
int get_channel_means(std::vector<std::int16_t> const& bayer_bytes, std::array<double, bayer10p::NUM_PHASES>& means, InputSetting const& setting) {
    if (bayer_bytes.size() != get_num_samples(setting.geom)) {
        return HELPER_FAILURE;
    }

    for (auto& mean : means) {
        mean = 0.0;
    }

    std::array<int64_t, bayer10p::NUM_PHASES> sums = {0, 0, 0, 0};

    auto const num_cells = static_cast<double>(static_cast<size_t>(setting.geom.width / bayer10p::CELL_DIM) *
                                               static_cast<size_t>(setting.geom.height / bayer10p::CELL_DIM));

    if (num_cells == 0) {
        return HELPER_FAILURE; // Something's seriously wrong.
    }

    // TODO: TEST WHAT HAPPEN ON PARTIALLY / WEIRD INPUTS
    // TODO LOOPING THIS IS ALMOST REPEATED CODE FROM DEMOSAIC, DEDUPE?
    Cell_t cell{};
    for (size_t i = 0; i <= setting.geom.height - bayer10p::CELL_DIM; i += bayer10p::CELL_DIM) {
        for (size_t j = 0; j <= setting.geom.width - bayer10p::CELL_DIM; j += bayer10p::CELL_DIM) {
            cell                                               = get_cell_at(setting.geom, i, j, setting.order);
            sums[static_cast<size_t>(bayer_phase::red)]       += bayer_bytes[cell.red];
            sums[static_cast<size_t>(bayer_phase::greenred)]  += bayer_bytes[cell.greenred];
            sums[static_cast<size_t>(bayer_phase::greenblue)] += bayer_bytes[cell.greenblue];
            sums[static_cast<size_t>(bayer_phase::blue)]      += bayer_bytes[cell.blue];
        }
    }

    // TODO: WHAT HAPPEN IF NO FPU? (+ n/2) / n? But then...OVERFLOW???
    for (size_t i = 0; i < bayer10p::NUM_PHASES; ++i) {
        means[i] = static_cast<double>(sums[i]) / num_cells;
    }

    return HELPER_SUCCESS;
}

int level_black(std::vector<std::int16_t> const& bayer_bytes, std::array<int32_t, bayer10p::NUM_PHASES> const& black_level_means,
                std::vector<std::int16_t>& leveled_bytes, InputSetting const& settings) {

    if (bayer_bytes.size() == 0) {
        std::cout << "Empty bayer bytes\n";
        return HELPER_FAILURE;
    }
    if (settings.geom.width < bayer10p::CELL_DIM || settings.geom.height < bayer10p::CELL_DIM) {
        std::cout << "Check requested geometry (image-size), something's wrong.\n";
        return HELPER_FAILURE;
    }

    leveled_bytes.resize(bayer_bytes.size());

    // TODO: TEST WHAT HAPPEN ON PARTIALLY / WEIRD INPUTS
    // TODO LOOPING THIS IS ALMOST REPEATED CODE FROM DEMOSAIC, DEDUPE?
    Cell_t cell{};
    for (size_t i = 0; i <= settings.geom.height - bayer10p::CELL_DIM; i += bayer10p::CELL_DIM) {
        for (size_t j = 0; j <= settings.geom.width - bayer10p::CELL_DIM; j += bayer10p::CELL_DIM) {
            cell = get_cell_at(settings.geom, i, j, settings.order);
            leveled_bytes[cell.blue]      = MYINT16_T(bayer_bytes[cell.blue] - MYINT16_T(black_level_means[MYSIZE_T(bayer_phase::blue)]));
            leveled_bytes[cell.greenred]  = MYINT16_T(bayer_bytes[cell.greenred] - MYINT16_T(black_level_means[MYSIZE_T(bayer_phase::greenred)]));
            leveled_bytes[cell.greenblue] = MYINT16_T(bayer_bytes[cell.greenblue] - MYINT16_T(black_level_means[MYSIZE_T(bayer_phase::greenblue)]));
            leveled_bytes[cell.red]       = MYINT16_T(bayer_bytes[cell.red] - MYINT16_T(black_level_means[MYSIZE_T(bayer_phase::red)]));
        }
    }
    return HELPER_SUCCESS;
}

#undef MYINT16_T
#undef MYSIZE_T
