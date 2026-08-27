#include "isp.hpp"
#include "isp_bayer.hpp"
#include "isp_def.hpp"

#include <iostream>

int demosaic_nn_bayer10p(std::vector<int16_t> const& bayer_bytes, std::vector<int16_t>& demosaiced_bytes, InputSetting const& settings) {

    if (bayer_bytes.size() < get_num_samples(settings.geom)) {
        std::cout << "Insufficient bayer samples deteced(" << bayer_bytes.size() << "). There is less than expected("
                  << get_num_samples(settings.geom) << ")\n";
        return HELPER_FAILURE;
    }

    if (settings.geom.width < bayer10p::CELL_DIM || settings.geom.height < bayer10p::CELL_DIM) {
        std::cout << "Requested image dimension is too small. Please check.\n";
    }

    /**
     * Solve with following view:
     *    0      1    2    3 ... 639
     *  640    641  642  643 ... 1279
     *  ...
     *  306560  ....             307199
     *
     * Meaning BGGR_1: 0 1 640 641
     *         BGGR_2: 2 3 642 643
     *         ...
     *
     * Our in-memory array store them as: (0|1) (2|3) (4|5).
     * Each cell has 3 values R-G-B.
     */
    // TODO: TEST WHAT HAPPEN ON PARTIALLY / WEIRD INPUTS
    size_t const num_cell_rows = settings.geom.height / bayer10p::CELL_DIM;
    size_t const num_cell_cols = settings.geom.width / bayer10p::CELL_DIM;
    size_t const num_cells     = num_cell_rows * num_cell_cols;
    demosaiced_bytes.resize(num_cells * RGB_NUM_CHANNELS);
    size_t k = 0;
    Cell_t cell{};
    for (size_t i = 0; i <= settings.geom.height - bayer10p::CELL_DIM; i += bayer10p::CELL_DIM) {
        for (size_t j = 0; j <= settings.geom.width - bayer10p::CELL_DIM; j += bayer10p::CELL_DIM) {
            cell = get_cell_at(settings.geom, i, j, settings.order);
            // RED.
            demosaiced_bytes[k++] = static_cast<int16_t>(bayer_bytes[cell.red]);
            // GREEN.
            demosaiced_bytes[k++] = static_cast<int16_t>(((int32_t)bayer_bytes[cell.greenblue] + (int32_t)bayer_bytes[cell.greenred]) / 2);
            // BLUE.
            demosaiced_bytes[k++] = static_cast<int16_t>(bayer_bytes[cell.blue]);
        }
    }
    return HELPER_SUCCESS;
}

// At the edge, we pad with mirror about edge.
// At every pixel, we fill up R-G-B in that order (in-memory).
int demosaic_bilinear_bayer10p(std::vector<int16_t> const& bayer_bytes, std::vector<int16_t>& demosaiced_bytes, InputSetting const& settings) {
    if (bayer_bytes.size() < get_num_samples(settings.geom)) {
        std::cout << "Insufficient bayer samples deteced(" << bayer_bytes.size() << "). There is less than expected("
                  << get_num_samples(settings.geom) << ")\n";
        return HELPER_FAILURE;
    }

    if (settings.geom.width < bayer10p::CELL_DIM || settings.geom.height < bayer10p::CELL_DIM) {
        std::cout << "Requested image dimension is too small. Please check.\n";
    }

    // TODO: TEST WHAT HAPPEN ON PARTIALLY / WEIRD INPUTS
    demosaiced_bytes.resize(bayer_bytes.size() * RGB_NUM_CHANNELS);

    auto const get_pix = [&](size_t const row, size_t const col) { return static_cast<int32_t>(bayer_bytes[row * settings.geom.width + col]); };

    size_t k = 0;
    // TODO: MIGHT NEED TO CHECK PERF HERE. INTERP PER PIXEL FOR HIGHER RES MIGHT JUST NOT BE NICE.
    for (size_t i = 0; i < settings.geom.height; ++i) {

        size_t up = 1U;
        if (i > 0) {
            up = i - 1U;
        }
        size_t down = i;
        if (i == settings.geom.height - 1U) {
            down -= 1U;
        } else {
            down += 1U;
        }

        for (size_t j = 0; j < settings.geom.width; ++j) {

            size_t left = 1U;
            if (j > 0) {
                left = j - 1U;
            }
            size_t right = j;
            if (j == settings.geom.width - 1U) {
                right -= 1U;
            } else {
                right += 1U;
            }

            int32_t red_val   = 0;
            int32_t green_val = 0;
            int32_t blue_val  = 0;

            // TODO: FIX THE TRUNCATION, DO CALC IN FLOAT THEN ROUND. FOR NOW IT ACCIDENTALLY WORKS (PRETTY CLOSE TO CORRECT).
            // TODO: SO LEAVE IT BE FOR NOW.
            switch (get_phase_at(i, j, settings.order)) {
            case bayer_phase::red: {
                red_val   = get_pix(i, j);
                green_val = (get_pix(up, j) + get_pix(down, j) + get_pix(i, left) + get_pix(i, right)) / 4;
                blue_val  = (get_pix(up, left) + get_pix(up, right) + get_pix(down, left) + get_pix(down, right)) / 4;
                break;
            }
            case bayer_phase::greenred: {
                red_val   = (get_pix(i, left) + get_pix(i, right)) / 2;
                green_val = get_pix(i, j);
                blue_val  = (get_pix(up, j) + get_pix(down, j)) / 2;
                break;
            }
            case bayer_phase::greenblue: {
                red_val   = (get_pix(up, j) + get_pix(down, j)) / 2;
                green_val = get_pix(i, j);
                blue_val  = (get_pix(i, left) + get_pix(i, right)) / 2;
                break;
            }
            case bayer_phase::blue: {
                red_val   = (get_pix(up, left) + get_pix(up, right) + get_pix(down, left) + get_pix(down, right)) / 4;
                green_val = (get_pix(up, j) + get_pix(down, j) + get_pix(i, left) + get_pix(i, right)) / 4;
                blue_val  = get_pix(i, j);
                break;
            }
            }

            demosaiced_bytes[k++] = static_cast<int16_t>(red_val);
            demosaiced_bytes[k++] = static_cast<int16_t>(green_val);
            demosaiced_bytes[k++] = static_cast<int16_t>(blue_val);
        }
    }
    return HELPER_SUCCESS;
}
