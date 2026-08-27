#ifndef GREEN_SIMPLE_ISP_PIPELINE_HPP_INCLUDED
#define GREEN_SIMPLE_ISP_PIPELINE_HPP_INCLUDED

#include "isp.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

enum class isp_stage : size_t { DECODE = 0U, BLACK_LEVEL = 1U, WHITE_BALANCE = 2U, DEMOSAIC = 3U, CCM = 4U, GAMMA = 5U, ALL = 6U };
enum class inp_format : int32_t { UNPACKED, PACKED };
inline constexpr std::string_view DEFAULT_BLACK_FRAME = "black_packed000.raw";
inline constexpr std::string_view DEFAULT_OUTPUT_DIR  = "pipeline_out";

struct PipelineConfig_t {
    bool                  dump_on{false};
    isp_stage             stopper{isp_stage::ALL};
    inp_format            format{inp_format::PACKED};
    double                illuminant{bayer10p::DEFAULT_ILLUMINANT};
    InputSetting          setting{DEFAULT_INPUT_SETTINGS};
    std::filesystem::path input_file;
    std::filesystem::path black_file{DEFAULT_BLACK_FRAME};
    std::filesystem::path out_base;
};

struct stage_name_t {
    std::string_view name;
    isp_stage        stage;
};

// Purposely not using a htable here since it's smoll number of choices..
constexpr inline std::array<stage_name_t, 7> STAGE_NAMES{
    {
     {"decode", isp_stage::DECODE},
     {"black_level", isp_stage::BLACK_LEVEL},
     {"white_balance", isp_stage::WHITE_BALANCE},
     {"demosaic", isp_stage::DEMOSAIC},
     {"ccm", isp_stage::CCM},
     {"gamma", isp_stage::GAMMA},
     {"all", isp_stage::ALL},
     }
};

int get_stage_by_name(std::string_view const name, isp_stage& stage);
int run_pipeline(PipelineConfig_t const& p_cfg);


#endif // GREEN_SIMPLE_ISP_PIPELINE_HPP_INCLUDED
