// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_types.h"
#include "core/hle/service/cam/y2r_u.h"
#include "core/hw/y2r_testing.h"

namespace {

using HW::Y2R::Testing::ImageTile;
using Service::Y2R::CoefficientSet;
using Service::Y2R::InputFormat;

constexpr u32 Canary = 0xC0DEC0DE;

void ConvertReference(InputFormat input_format, const u8* input_y, const u8* input_u,
                      const u8* input_v, ImageTile output[], unsigned int width,
                      unsigned int height, const CoefficientSet& coefficients) {
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            s32 y_sample;
            s32 u_sample;
            s32 v_sample;
            if (input_format == InputFormat::YUV422_Indiv8 ||
                input_format == InputFormat::YUV422_Indiv16) {
                y_sample = input_y[y * width + x];
                u_sample = input_u[(y * width + x) / 2];
                v_sample = input_v[(y * width + x) / 2];
            } else if (input_format == InputFormat::YUV420_Indiv8 ||
                       input_format == InputFormat::YUV420_Indiv16) {
                y_sample = input_y[y * width + x];
                u_sample = input_u[((y / 2) * width + x) / 2];
                v_sample = input_v[((y / 2) * width + x) / 2];
            } else {
                y_sample = input_y[(y * width + x) * 2];
                u_sample = input_y[(y * width + (x / 2) * 2) * 2 + 1];
                v_sample = input_y[(y * width + (x / 2) * 2) * 2 + 3];
            }

            const s32 cy = coefficients[0] * y_sample;
            s32 red = cy + coefficients[1] * v_sample;
            s32 green = cy - coefficients[2] * v_sample - coefficients[3] * u_sample;
            s32 blue = cy + coefficients[4] * u_sample;
            red = (red >> 3) + coefficients[5] + 0x18;
            green = (green >> 3) + coefficients[6] + 0x18;
            blue = (blue >> 3) + coefficients[7] + 0x18;

            output[x / 8][y * 8 + x % 8] = static_cast<u32>(std::clamp(red >> 5, 0, 0xFF)) << 24 |
                                           static_cast<u32>(std::clamp(green >> 5, 0, 0xFF)) << 16 |
                                           static_cast<u32>(std::clamp(blue >> 5, 0, 0xFF)) << 8;
        }
    }
}

} // namespace

TEST_CASE("Y2R conversion matches the scalar hardware reference", "[core][hw][y2r]") {
    constexpr std::array formats{
        InputFormat::YUV422_Indiv8,  InputFormat::YUV420_Indiv8,       InputFormat::YUV422_Indiv16,
        InputFormat::YUV420_Indiv16, InputFormat::YUYV422_Interleaved,
    };
    constexpr std::array<CoefficientSet, 6> coefficient_sets{{
        {{0x100, 0x166, 0xB6, 0x58, 0x1C5, -0x166F, 0x10EE, -0x1C5B}},
        {{0x12A, 0x1CA, 0x88, 0x36, 0x21C, -0x1F04, 0x99C, -0x2421}},
        {{0, 0, 0, 0, 0, 0, 0, 0}},
        {{1, -1, 2, -2, 3, -4, 5, -6}},
        {{32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767}},
        {{-32768, -32768, -32768, -32768, -32768, -32768, -32768, -32768}},
    }};

    for (const unsigned int width : {8u, 16u, 24u}) {
        for (const unsigned int height : {1u, 2u, 7u, 8u}) {
            std::vector<u8> y_plane(width * height);
            std::vector<u8> u_plane(width * height);
            std::vector<u8> v_plane(width * height);
            std::vector<u8> yuyv(width * height * 2);
            for (std::size_t i = 0; i < y_plane.size(); ++i) {
                y_plane[i] = static_cast<u8>((i * 37 + width * 11 + height * 3) & 0xFF);
                u_plane[i] = static_cast<u8>((i * 73 + width * 5 + 0x55) & 0xFF);
                v_plane[i] = static_cast<u8>((i * 109 + height * 7 + 0xAA) & 0xFF);
            }
            for (std::size_t i = 0; i < yuyv.size(); ++i) {
                yuyv[i] = static_cast<u8>((i * 43 + width * 13 + height * 17) & 0xFF);
            }

            for (const auto format : formats) {
                const bool interleaved = format == InputFormat::YUYV422_Interleaved;
                const u8* input_y = interleaved ? yuyv.data() : y_plane.data();
                const u8* input_u = interleaved ? nullptr : u_plane.data();
                const u8* input_v = interleaved ? nullptr : v_plane.data();

                for (const auto& coefficients : coefficient_sets) {
                    std::vector<ImageTile> expected(width / 8);
                    std::vector<ImageTile> actual(width / 8);
                    for (auto& tile : expected) {
                        tile.fill(Canary);
                    }
                    for (auto& tile : actual) {
                        tile.fill(Canary);
                    }

                    ConvertReference(format, input_y, input_u, input_v, expected.data(), width,
                                     height, coefficients);
                    HW::Y2R::Testing::ConvertYUVToRGB(format, input_y, input_u, input_v,
                                                      actual.data(), width, height, coefficients);

                    INFO("format=" << static_cast<unsigned int>(format) << " width=" << width
                                   << " height=" << height);
                    CHECK(actual == expected);
                }
            }
        }
    }
}
