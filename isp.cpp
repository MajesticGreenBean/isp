/**
 * A software isp, in C++.
 */

#include "isp.hpp"
#include "isp_def.hpp"
#include "isp_pipeline.hpp"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

namespace {

constexpr int32_t MIN_ARGC = 4;
constexpr int32_t MAX_ARGC = 13;

// TODO: Remove unpacked, or complete its implementations.
void print_help() {
    std::cout << "Usage: isp <frame.raw> <output>\n"
              << "  --packed    SBGGR10P ('pBaa'), 4 pixels per 5 bytes format\n"
              << "  --unpacked  16-bit little-endian pixels (SBGGR10)\n"
              << "  --dark <name> dark frame file name\n"
              << "  --stage <name>  stop after this stage (decode, black_level, white_balance, demosaic, ccm, gamma, all)\n"
              << "  --no-dump       skip the per-stage previews\n"
              << "  --illuminant <ct>  color temperature (in Kelvin) for illuminant (default = 5858K)\n\n"
              << "Each pipeline stage outputs are put into <output.*>, or into " << DEFAULT_OUTPUT_DIR << " directory when no dir is given.\n"
              << "A dark frame named \"black_packed000.raw\" must exist in running directory\n";
}

struct dir_t {
    fs::path full_path;
    fs::path name;
};

} // namespace

int main(int argc, char** argv) {

    if (argc < MIN_ARGC || argc > MAX_ARGC) {
        print_help();
        return EXIT_FAILURE;
    }

    PipelineConfig_t  cfg;
    std::string const mode = argv[1];
    if (mode != "--unpacked" && mode != "--packed") {}
    if (mode == "--packed") {
        cfg.format = inp_format::PACKED;
    } else if (mode == "--unpacked") {
        cfg.format = inp_format::UNPACKED;
    } else {
        std::cout << "Please provide --packed or --unpacked thanks.\n";
        print_help();
        return EXIT_FAILURE;
    }

    cfg.input_file = argv[2];
    cfg.out_base   = argv[3];
    cfg.stopper    = isp_stage::ALL;
    cfg.dump_on    = true;
    cfg.setting    = DEFAULT_INPUT_SETTINGS;
    cfg.illuminant = bayer10p::DEFAULT_ILLUMINANT;

    cfg.black_file = DEFAULT_BLACK_FRAME;
    std::string flag;
    for (int i = MIN_ARGC; i < argc; ++i) {
        flag = argv[i];
        if (flag == "--dark") {
            if (i + 1 >= argc) {
                std::cout << "[ARG ERROR] --dark needs a filepath.\n";
                return EXIT_FAILURE;
            }
            cfg.black_file = argv[++i];
        } else if (flag == "--stage") {
            if (i + 1 >= argc) {
                std::cout << "[ARG ERROR] --stage needs a stage name.\n";
                return EXIT_FAILURE;
            }
            if (get_stage_by_name(argv[++i], cfg.stopper) != HELPER_SUCCESS) {
                std::cout << "[ARG ERROR] Unknown stage: " << argv[i] << "\nSupported stages:";
                for (auto const& entry : STAGE_NAMES) {
                    std::cout << ' ' << entry.name;
                }
                std::cout << '\n';
                return EXIT_FAILURE;
            }
        } else if (flag == "--no-dump") {
            cfg.dump_on = false;
        } else if (flag == "--illuminant") {
            if (i + 1 >= argc) {
                std::cout << "[ARG ERROR] --illuminant needs a color temperature.\n";
                return EXIT_FAILURE;
            }
            // Safe guard reading human number input abit.
            if (strlen(argv[++i]) >= 8) {
                std::cout << "[ARG ERROR] you're trying to give a temperature with more than 8 digits? No way\n";
                return EXIT_FAILURE;
            } else if (std::isdigit(argv[i][0]) == 0) {
                std::cout << "[ARG ERROR] expeting anumber for color temperature\n";
                return EXIT_FAILURE;
            }
            cfg.illuminant = std::stod(argv[i]);
        }else {
            std::cout << "Unknown/Typo flag: " << flag << '\n';
            print_help();
            return EXIT_FAILURE;
        }
    }

    if (run_pipeline(cfg) != HELPER_SUCCESS) {
        std::cout << "[FINAL] Pipeline failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "[FINAL] Pipeline finished.\n";
    return EXIT_SUCCESS;
}
