#ifndef GREEN_SIMPLE_ISP_BAYER_HPP_INCLUDED
#define GREEN_SIMPLE_ISP_BAYER_HPP_INCLUDED

#include "isp_def.hpp"
#include <array>
#include <cstddef>

struct Cell_t {
    size_t red;
    size_t greenred;
    size_t greenblue;
    size_t blue;
};

struct Geom_t {
    size_t width;
    size_t height;
};

enum class bayer_phase : size_t { red = 0U, greenred = 1U, greenblue = 2U, blue = 3U };

/**
 * https://www.kernel.org/doc/html/v6.12/userspace-api/media/v4l/pixfmt-bayer.html.
 * https://www.kernel.org/doc/html/v6.12/userspace-api/media/v4l/pixfmt-srggb10p.html.
 */
enum class bayer_order : size_t { RGGB = 0U, GRBG = 1U, GBRG = 2U, BGGR = 3U };

// clang-format off
constexpr inline std::array<std::array<bayer_phase, 4>, 4> BAYER_PHASE_LUT = {
    {
        {{bayer_phase::red, bayer_phase::greenred, bayer_phase::greenblue, bayer_phase::blue}}, // RGGB
        {{bayer_phase::greenred, bayer_phase::red, bayer_phase::blue, bayer_phase::greenblue}}, // GRBG
        {{bayer_phase::greenblue, bayer_phase::blue, bayer_phase::red, bayer_phase::greenred}}, // GBRG
        {{bayer_phase::blue, bayer_phase::greenblue, bayer_phase::greenred, bayer_phase::red}}, // BGGR
    }
};

/**
 * Map (row,col) -> (top_left, top_right btm_left, btm_right) with color info.
 */
constexpr inline Cell_t get_cell_at(Geom_t const& geom, size_t const row, size_t const col, bayer_order const order) {
    Cell_t cell{};
    // 2x2 cell
    auto const top_left  = row * geom.width + col;
    auto const top_right = row * geom.width + (col + 1);
    auto const btm_left  = (row + 1) * geom.width + col;
    auto const btm_right = (row + 1) * geom.width + (col + 1);

    std::array<size_t, 4> const cell_idx {{top_left, top_right, btm_left, btm_right}};

    for (size_t i = 0; i < 4U; ++i) {
        switch (BAYER_PHASE_LUT[static_cast<size_t>(order)][i]) {
            case bayer_phase::red: { cell.red = cell_idx[i]; break; }
            case bayer_phase::greenred: { cell.greenred = cell_idx[i]; break; }
            case bayer_phase::greenblue: { cell.greenblue = cell_idx[i]; break; }
            case bayer_phase::blue: { cell.blue = cell_idx[i]; break; }
        }
    }

    return cell;
}

/** Given the pixel row and col, look up its phase. (1 cell has 4 pixels). */
constexpr inline bayer_phase get_phase_at(size_t const pix_row, size_t const pix_col, bayer_order const order) {
    return BAYER_PHASE_LUT[static_cast<size_t>(order)][(pix_row % bayer10p::CELL_DIM) * bayer10p::CELL_DIM + (pix_col % bayer10p::CELL_DIM)];
}

/** 
 * From geometry of image, calculate the expected size. 
 */
constexpr inline size_t get_num_samples(Geom_t const& geom) {
    return geom.height * geom.width;
}

/** temporary solution. */
constexpr inline Geom_t DEFAULT_GEOM = {.width = 640, .height = 480};
constexpr inline bayer_order DEFAULT_BAYER_ORDER = bayer_order::BGGR;

#endif // GREEN_SIMPLE_ISP_BAYER_HPP_INCLUDED
