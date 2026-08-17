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
using Service::Y2R::OutputFormat;

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

std::size_t OutputBytesPerPixel(OutputFormat format) {
    switch (format) {
    case OutputFormat::RGBA8:
        return 4;
    case OutputFormat::RGB8:
        return 3;
    case OutputFormat::RGB5A1:
    case OutputFormat::RGB565:
        return 2;
    }
    return 0;
}

void EncodeOutputReference(OutputFormat format, const u32* input, u8* output,
                           std::size_t pixel_count, u8 alpha) {
    const std::size_t bytes_per_pixel = OutputBytesPerPixel(format);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const u32 color = input[pixel];
        const u8 red = static_cast<u8>(color >> 24);
        const u8 green = static_cast<u8>(color >> 16);
        const u8 blue = static_cast<u8>(color >> 8);
        u8* const encoded = output + pixel * bytes_per_pixel;
        switch (format) {
        case OutputFormat::RGBA8:
            encoded[0] = alpha;
            encoded[1] = blue;
            encoded[2] = green;
            encoded[3] = red;
            break;
        case OutputFormat::RGB8:
            encoded[0] = blue;
            encoded[1] = green;
            encoded[2] = red;
            break;
        case OutputFormat::RGB5A1: {
            const u16 packed = static_cast<u16>((red >> 3) << 11 | (green >> 3) << 6 |
                                                (blue >> 3) << 1 | alpha >> 7);
            encoded[0] = static_cast<u8>(packed);
            encoded[1] = static_cast<u8>(packed >> 8);
            break;
        }
        case OutputFormat::RGB565: {
            const u16 packed = static_cast<u16>((red >> 3) << 11 | (green >> 2) << 5 | blue >> 3);
            encoded[0] = static_cast<u8>(packed);
            encoded[1] = static_cast<u8>(packed >> 8);
            break;
        }
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

TEST_CASE("Y2R output packing matches the scalar format reference", "[core][hw][y2r]") {
    constexpr std::array formats{
        OutputFormat::RGBA8,
        OutputFormat::RGB8,
        OutputFormat::RGB5A1,
        OutputFormat::RGB565,
    };
    constexpr std::array<u8, 16> channel_edges{
        0, 1, 7, 8, 31, 32, 63, 64, 127, 128, 247, 248, 249, 252, 254, 255,
    };

    for (const std::size_t pixel_count :
         {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{15}, std::size_t{16},
          std::size_t{17}, std::size_t{31}, std::size_t{32}, std::size_t{37}}) {
        std::vector<u32> input(pixel_count);
        for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
            const u32 red = channel_edges[pixel % channel_edges.size()];
            const u32 green = channel_edges[(pixel * 5 + 3) % channel_edges.size()];
            const u32 blue = channel_edges[(pixel * 11 + 7) % channel_edges.size()];
            input[pixel] = red << 24 | green << 16 | blue << 8;
        }

        for (const auto format : formats) {
            const std::size_t output_size = pixel_count * OutputBytesPerPixel(format);
            for (const u8 alpha : {u8{0}, u8{1}, u8{0x7F}, u8{0x80}, u8{0xFF}}) {
                std::vector<u8> expected(output_size + 32, 0xCD);
                std::vector<u8> actual(output_size + 32, 0xCD);
                EncodeOutputReference(format, input.data(), expected.data(), pixel_count, alpha);
                HW::Y2R::Testing::EncodeRGBToOutput(format, input.data(), actual.data(),
                                                    pixel_count, alpha);

                INFO("format=" << static_cast<unsigned int>(format) << " pixel_count="
                               << pixel_count << " alpha=" << static_cast<unsigned int>(alpha));
                CHECK(actual == expected);
            }
        }
    }
}

TEST_CASE("Y2R unrotated linear tiles write directly to the output strip", "[core][hw][y2r]") {
    constexpr std::size_t GuardWords = 16;

    for (const std::size_t num_tiles :
         {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        std::vector<ImageTile> tiles(num_tiles);
        for (std::size_t tile = 0; tile < num_tiles; ++tile) {
            for (std::size_t pixel = 0; pixel < tiles[tile].size(); ++pixel) {
                tiles[tile][pixel] = static_cast<u32>(0x10000000 | tile << 16 | pixel);
            }
        }

        for (const unsigned int height : {1u, 2u, 7u, 8u}) {
            for (const unsigned int padding : {0u, 5u}) {
                const unsigned int line_stride = static_cast<unsigned int>(num_tiles * 8) + padding;
                const std::size_t output_words = static_cast<std::size_t>(line_stride) * 8;
                std::vector<u32> expected(GuardWords + output_words + GuardWords, Canary);
                std::vector<u32> actual = expected;

                for (std::size_t tile = 0; tile < num_tiles; ++tile) {
                    for (unsigned int y = 0; y < height; ++y) {
                        for (unsigned int x = 0; x < 8; ++x) {
                            expected[GuardWords + y * line_stride + tile * 8 + x] =
                                tiles[tile][y * 8 + x];
                        }
                    }
                }

                HW::Y2R::Testing::WriteUnrotatedLinearTiles(
                    actual.data() + GuardWords, tiles.data(), num_tiles, height, line_stride);

                INFO("num_tiles=" << num_tiles << " height=" << height << " padding=" << padding);
                CHECK(actual == expected);
            }
        }
    }
}
