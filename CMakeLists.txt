# Software ISP for packed RAW10 Bayer, pBAA, BRRG phase, 8-bit RGB out.
# Input this way because 'rpicam-raw' default output this format for some reason.
# Tested on LINUX(Debian) only.
#
#   cmake -S . -B build
#   cmake --build build
#   ./build/isp --packed packed000.raw out.pgm
#
#   Some other flags:
#   -DCMAKE_BUILD_TYPE=Release  (default here is Debug)
#   -DISP_SANITIZE=ON           (default off AddressSanitizer + UndefinedBehaviorSanitizer)
#   -DISP_WERROR=OFF            (don't do this, unless if it's 4:57AM in the morning)

cmake_minimum_required(VERSION 3.18...3.31)

project(isp LANGUAGES CXX)

include(GNUInstallDirs)

option(ISP_WERROR   "Treat compiler warnings as errors" ON)
option(ISP_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# neovim + clangd + clang-tidy is love.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# default dbug build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Shared compile settings.
# ---------------------------------------------------------------------------
add_library(isp_build_settings INTERFACE)
add_library(isp::build_settings ALIAS isp_build_settings)

target_compile_features(isp_build_settings INTERFACE cxx_std_20)

set(isp_warnings -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion)
if (ISP_WERROR)
    list(APPEND isp_warnings -Werror)
endif()

# Only windows (msvc) not supported yet here.... just run wsl2
target_compile_options(isp_build_settings INTERFACE
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:${isp_warnings}>")

if(ISP_SANITIZE)
    set(isp_sanitizers -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(isp_build_settings INTERFACE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:${isp_sanitizers}>")
    target_link_options(isp_build_settings INTERFACE
        "$<$<LINK_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:${isp_sanitizers}>")
endif()

# ---------------------------------------------------------------------------
# Targets # I like to avoid main() everytime linking intermediate helpers.
# ---------------------------------------------------------------------------
add_library(isp_core STATIC
    isp_decode.cpp
    isp_black_level.cpp
    isp_wb.cpp
    isp_demosaic.cpp
    isp_ccm.cpp
    isp_gamma.cpp
    isp_img_writer.cpp
    isp_img_reader.cpp
    isp_pipeline.cpp
    isp.hpp
    isp_def.hpp
    isp_bayer.hpp
    isp_pipeline.hpp)
add_library(isp::core ALIAS isp_core)

target_include_directories(isp_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(isp_core PUBLIC isp::build_settings)

# main() here.
add_executable(isp isp.cpp)
target_link_libraries(isp PRIVATE isp::core)

# Dev stuffs.
add_executable(isp_test_bayer tools/test_bayer_phases.cpp)
target_link_libraries(isp_test_bayer PRIVATE isp::core)
add_executable(isp_compare tools/isp_compare.cpp)
target_link_libraries(isp_compare PRIVATE isp::core)

# ---------------------------------------------------------------------------
# ctest, simple ones.
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME run_pipeline COMMAND isp --packed "${CMAKE_CURRENT_SOURCE_DIR}/packed000.raw" out.pgm --dark "${CMAKE_CURRENT_SOURCE_DIR}/black_packed000.raw" )

set(isp_stage_outputs
    unpacked_out.pgm
    bl_out.pgm
    nn_dm_out.ppm
    bl_nn_dm_out.ppm
    bl_wb_out.pgm
    bl_wb_b_dm_out.ppm
    bl_wb_b_dm_ccm_out.ppm
    final_out.ppm)

foreach(stage_file IN LISTS isp_stage_outputs)
    get_filename_component(stage_name "${stage_file}" NAME_WE)
    add_test(NAME compare_${stage_name} COMMAND 
                isp_compare
                "pipeline_out/${stage_file}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/${stage_file}"
            )
    set_tests_properties(compare_${stage_name} PROPERTIES DEPENDS run_pipeline)
endforeach()

add_test(NAME check_isp_bayer_indexing_logic COMMAND isp_test_bayer "${CMAKE_CURRENT_SOURCE_DIR}/packed000.raw")
