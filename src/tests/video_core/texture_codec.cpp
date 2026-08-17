// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/rasterizer_cache/texture_codec.h"
#include "video_core/rasterizer_cache/utils.h"

namespace {

template <u32 bytes_per_pixel, bool reverse_pixel_bytes>
std::array<u8, 10 * 8 * bytes_per_pixel> ReferenceDecode(
    const std::array<u8, 8 * 8 * bytes_per_pixel>& tiled) {
    std::array<u8, 10 * 8 * bytes_per_pixel> linear{};
    linear.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 source = VideoCore::MortonInterleave(x, y) * bytes_per_pixel;
            const u32 dest = ((7 - y) * 10 + x) * bytes_per_pixel;
            for (u32 component = 0; component < bytes_per_pixel; ++component) {
                const u32 source_component =
                    reverse_pixel_bytes ? bytes_per_pixel - 1 - component : component;
                linear[dest + component] = tiled[source + source_component];
            }
        }
    }
    return linear;
}

template <VideoCore::PixelFormat format, bool converted = false>
void CheckTileCodec() {
    constexpr u32 bytes_per_pixel = VideoCore::GetFormatBpp(format) / 8;
    constexpr bool reverse_pixel_bytes = format == VideoCore::PixelFormat::RGBA8 && converted;

    std::array<u8, 8 * 8 * bytes_per_pixel> tiled{};
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = static_cast<u8>((i * 37 + 19) & 0xFF);
    }

    const auto expected = ReferenceDecode<bytes_per_pixel, reverse_pixel_bytes>(tiled);
    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, format, converted>(10, tiled, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, 8 * 8 * bytes_per_pixel> encoded{};
    encoded.fill(0xA5);
    VideoCore::MortonCopyTile<false, format, converted>(10, encoded, decoded);
    REQUIRE(encoded == tiled);
}

template <VideoCore::PixelFormat format>
void CheckExpandedTexture() {
    constexpr u32 bytes_per_pixel = VideoCore::GetFormatBpp(format) / 8;
    std::array<u8, 8 * 8 * bytes_per_pixel> tiled{};
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = static_cast<u8>((i * 53 + 7) & 0xFF);
    }

    std::array<u8, 10 * 8 * 4> expected{};
    expected.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 source = VideoCore::MortonInterleave(x, y) * bytes_per_pixel;
            const u32 dest = ((7 - y) * 10 + x) * 4;
            if constexpr (format == VideoCore::PixelFormat::IA8) {
                expected[dest] = tiled[source + 1];
                expected[dest + 1] = tiled[source + 1];
                expected[dest + 2] = tiled[source + 1];
                expected[dest + 3] = tiled[source];
            } else if constexpr (format == VideoCore::PixelFormat::RG8) {
                expected[dest] = tiled[source + 1];
                expected[dest + 1] = tiled[source];
                expected[dest + 2] = 0;
                expected[dest + 3] = 0xFF;
            } else if constexpr (format == VideoCore::PixelFormat::I8) {
                expected[dest] = tiled[source];
                expected[dest + 1] = tiled[source];
                expected[dest + 2] = tiled[source];
                expected[dest + 3] = 0xFF;
            } else if constexpr (format == VideoCore::PixelFormat::A8) {
                expected[dest] = 0;
                expected[dest + 1] = 0;
                expected[dest + 2] = 0;
                expected[dest + 3] = tiled[source];
            } else {
                static_assert(format == VideoCore::PixelFormat::IA4);
                const u8 intensity = tiled[source] >> 4;
                const u8 alpha = tiled[source] & 0x0F;
                expected[dest] = static_cast<u8>((intensity << 4) | intensity);
                expected[dest + 1] = expected[dest];
                expected[dest + 2] = expected[dest];
                expected[dest + 3] = static_cast<u8>((alpha << 4) | alpha);
            }
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, format, false>(10, tiled, decoded);
    REQUIRE(decoded == expected);
}

template <VideoCore::PixelFormat format>
void CheckExpanded4BitTexture() {
    static_assert(format == VideoCore::PixelFormat::I4 || format == VideoCore::PixelFormat::A4);
    std::array<u8, 8 * 8 / 2> tiled{};
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = static_cast<u8>((i * 59 + 13) & 0xFF);
    }

    std::array<u8, 10 * 8 * 4> expected{};
    expected.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 morton = VideoCore::MortonInterleave(x, y);
            const u8 packed = tiled[morton / 2];
            const u8 nibble = (morton & 1) != 0 ? packed >> 4 : packed & 0x0F;
            const u8 value = static_cast<u8>((nibble << 4) | nibble);
            const u32 dest = ((7 - y) * 10 + x) * 4;
            if constexpr (format == VideoCore::PixelFormat::I4) {
                expected[dest] = value;
                expected[dest + 1] = value;
                expected[dest + 2] = value;
                expected[dest + 3] = 0xFF;
            } else {
                expected[dest] = 0;
                expected[dest + 1] = 0;
                expected[dest + 2] = 0;
                expected[dest + 3] = value;
            }
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, format, false>(10, tiled, decoded);
    REQUIRE(decoded == expected);
}

void CheckConvertedRGB8() {
    std::array<u8, 8 * 8 * 3> tiled{};
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = static_cast<u8>((i * 29 + 11) & 0xFF);
    }

    std::array<u8, 10 * 8 * 4> expected{};
    expected.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 source = VideoCore::MortonInterleave(x, y) * 3;
            const u32 dest = ((7 - y) * 10 + x) * 4;
            expected[dest] = tiled[source + 2];
            expected[dest + 1] = tiled[source + 1];
            expected[dest + 2] = tiled[source];
            expected[dest + 3] = 0xFF;
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, VideoCore::PixelFormat::RGB8, true>(10, tiled, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, 8 * 8 * 3> encoded{};
    encoded.fill(0xA5);
    VideoCore::MortonCopyTile<false, VideoCore::PixelFormat::RGB8, true>(10, encoded, decoded);
    REQUIRE(encoded == tiled);
}

template <VideoCore::PixelFormat format>
Common::Vec4<u8> DecodeConverted16(const u8* source) {
    static_assert(format == VideoCore::PixelFormat::RGB5A1 ||
                  format == VideoCore::PixelFormat::RGB565 ||
                  format == VideoCore::PixelFormat::RGBA4);
    if constexpr (format == VideoCore::PixelFormat::RGB5A1) {
        return Common::Color::DecodeRGB5A1(source);
    } else if constexpr (format == VideoCore::PixelFormat::RGB565) {
        return Common::Color::DecodeRGB565(source);
    } else {
        return Common::Color::DecodeRGBA4(source);
    }
}

template <VideoCore::PixelFormat format>
void CheckConverted16() {
    constexpr u32 values_per_tile = 8 * 8;
    for (u32 first_value = 0; first_value <= 0xFFFF; first_value += values_per_tile) {
        std::array<u8, values_per_tile * 2> tiled{};
        for (u32 index = 0; index < values_per_tile; ++index) {
            const u16 value = static_cast<u16>(first_value + index);
            tiled[index * 2] = static_cast<u8>(value);
            tiled[index * 2 + 1] = static_cast<u8>(value >> 8);
        }

        std::array<u8, 10 * 8 * 4> expected{};
        expected.fill(0xCD);
        for (u32 y = 0; y < 8; ++y) {
            for (u32 x = 0; x < 8; ++x) {
                const u32 source = VideoCore::MortonInterleave(x, y) * 2;
                const u32 dest = ((7 - y) * 10 + x) * 4;
                const auto rgba = DecodeConverted16<format>(tiled.data() + source);
                std::memcpy(expected.data() + dest, rgba.AsArray(), 4);
            }
        }

        auto decoded = expected;
        decoded.fill(0xCD);
        VideoCore::MortonCopyTile<true, format, true>(10, tiled, decoded);
        REQUIRE(decoded == expected);

        std::array<u8, values_per_tile * 2> encoded{};
        encoded.fill(0xA5);
        VideoCore::MortonCopyTile<false, format, true>(10, encoded, decoded);
        REQUIRE(encoded == tiled);
    }
}

void CheckD24S8() {
    std::array<u8, 8 * 8 * 4> tiled{};
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = static_cast<u8>((i * 43 + 23) & 0xFF);
    }

    std::array<u8, 10 * 8 * 4> expected{};
    expected.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 source = VideoCore::MortonInterleave(x, y) * 4;
            const u32 dest = ((7 - y) * 10 + x) * 4;
            expected[dest] = tiled[source + 3];
            expected[dest + 1] = tiled[source];
            expected[dest + 2] = tiled[source + 1];
            expected[dest + 3] = tiled[source + 2];
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, VideoCore::PixelFormat::D24S8, false>(10, tiled, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, 8 * 8 * 4> encoded{};
    encoded.fill(0xA5);
    VideoCore::MortonCopyTile<false, VideoCore::PixelFormat::D24S8, false>(10, encoded, decoded);
    REQUIRE(encoded == tiled);
}

void CheckConvertedD24() {
    constexpr std::array<u32, 8> edge_depths = {
        0, 1, 0x7FFFFF, 0x800000, 0xFFFFFE, 0xFFFFFF, 0x123456, 0xABCDEF,
    };
    std::array<u8, 8 * 8 * 3> tiled{};
    for (u32 index = 0; index < 8 * 8; ++index) {
        const u32 depth = index < edge_depths.size()
                              ? edge_depths[index]
                              : ((index * 0x45D9F3Bu) ^ (index << 17) ^ 0x5A39C7u) & 0xFFFFFF;
        tiled[index * 3] = static_cast<u8>(depth);
        tiled[index * 3 + 1] = static_cast<u8>(depth >> 8);
        tiled[index * 3 + 2] = static_cast<u8>(depth >> 16);
    }

    std::array<u8, 10 * 8 * 4> expected{};
    expected.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 source = VideoCore::MortonInterleave(x, y) * 3;
            const u32 depth = static_cast<u32>(tiled[source]) |
                              (static_cast<u32>(tiled[source + 1]) << 8) |
                              (static_cast<u32>(tiled[source + 2]) << 16);
            const float normalized = static_cast<float>(depth) / 16777215.0f;
            const u32 dest = ((7 - y) * 10 + x) * 4;
            std::memcpy(expected.data() + dest, &normalized, sizeof(normalized));
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, VideoCore::PixelFormat::D24, true>(10, tiled, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, 8 * 8 * 3> expected_encoded{};
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 source = ((7 - y) * 10 + x) * 4;
            float normalized;
            std::memcpy(&normalized, expected.data() + source, sizeof(normalized));
            const u32 depth = static_cast<u32>(normalized * 0xFFFFFF);
            const u32 dest = VideoCore::MortonInterleave(x, y) * 3;
            expected_encoded[dest] = static_cast<u8>(depth);
            expected_encoded[dest + 1] = static_cast<u8>(depth >> 8);
            expected_encoded[dest + 2] = static_cast<u8>(depth >> 16);
        }
    }

    std::array<u8, 8 * 8 * 3> encoded{};
    encoded.fill(0xA5);
    VideoCore::MortonCopyTile<false, VideoCore::PixelFormat::D24, true>(10, encoded, decoded);
    REQUIRE(encoded == expected_encoded);
}

template <bool depth_float>
void CheckDepthStencilUnpack() {
    constexpr std::array<u32, 12> pixel_counts = {0, 1, 3, 15, 16, 17, 31, 32, 33, 63, 64, 65};
    constexpr std::array<u32, 8> edge_depths = {
        0, 1, 0x7FFFFF, 0x800000, 0xFFFFFE, 0xFFFFFF, 0x123456, 0xABCDEF,
    };

    for (const u32 pixel_count : pixel_counts) {
        constexpr std::size_t canary_size = 32;
        const std::size_t data_size = pixel_count * 5;
        std::vector<u8> actual(data_size + canary_size, 0xA5);
        const auto depth_for_pixel = [&](u32 pixel) {
            return pixel < edge_depths.size() ? edge_depths[pixel]
                                              : (pixel * 0x1F123B + 0x654321) & 0xFFFFFF;
        };
        for (u32 pixel = 0; pixel < pixel_count; ++pixel) {
            const u32 depth = depth_for_pixel(pixel);
            const u8 stencil = static_cast<u8>(pixel * 73 + 19);
            const u32 packed = depth << 8 | stencil;
            std::memcpy(actual.data() + pixel * sizeof(packed), &packed, sizeof(packed));
        }

        auto expected = actual;
        for (u32 pixel = 0; pixel < pixel_count; ++pixel) {
            const u32 depth = depth_for_pixel(pixel);
            expected[pixel_count * sizeof(u32) + pixel] = static_cast<u8>(pixel * 73 + 19);
            if constexpr (depth_float) {
                const float normalized = static_cast<float>(depth) / 16777215.0f;
                std::memcpy(expected.data() + pixel * sizeof(u32), &normalized, sizeof(normalized));
            } else {
                std::memcpy(expected.data() + pixel * sizeof(u32), &depth, sizeof(depth));
            }
        }

        const auto mode = depth_float ? VideoCore::DepthStencilUnpackMode::D32Float
                                      : VideoCore::DepthStencilUnpackMode::D24Unorm;
        const u32 depth_size =
            VideoCore::UnpackDepthStencil(std::span{actual.data(), data_size}, mode);
        REQUIRE(depth_size == pixel_count * sizeof(u32));
        REQUIRE(actual == expected);
    }
}

void CheckLinearConvertedRGBA8() {
    constexpr std::size_t pixel_count = 37;
    std::array<u8, pixel_count * 4 + 3> encoded{};
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        encoded[i] = static_cast<u8>((i * 61 + 17) & 0xFF);
    }

    std::array<u8, pixel_count * 4 + 3> expected{};
    expected.fill(0xCD);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        for (std::size_t component = 0; component < 4; ++component) {
            expected[pixel * 4 + component] = encoded[pixel * 4 + 3 - component];
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::LinearCopy<true, VideoCore::PixelFormat::RGBA8, true>(encoded, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, pixel_count * 4 + 3> roundtrip{};
    roundtrip.fill(0xA5);
    VideoCore::LinearCopy<false, VideoCore::PixelFormat::RGBA8, true>(decoded, roundtrip);
    auto expected_roundtrip = encoded;
    expected_roundtrip[pixel_count * 4] = 0xA5;
    expected_roundtrip[pixel_count * 4 + 1] = 0xA5;
    expected_roundtrip[pixel_count * 4 + 2] = 0xA5;
    REQUIRE(roundtrip == expected_roundtrip);
}

void CheckLinearConvertedRGB8() {
    constexpr std::size_t pixel_count = 37;
    std::array<u8, pixel_count * 3 + 2> encoded{};
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        encoded[i] = static_cast<u8>((i * 67 + 29) & 0xFF);
    }

    std::array<u8, pixel_count * 4 + 3> expected{};
    expected.fill(0xCD);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        expected[pixel * 4] = encoded[pixel * 3 + 2];
        expected[pixel * 4 + 1] = encoded[pixel * 3 + 1];
        expected[pixel * 4 + 2] = encoded[pixel * 3];
        expected[pixel * 4 + 3] = 0xFF;
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::LinearCopy<true, VideoCore::PixelFormat::RGB8, true>(encoded, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, pixel_count * 3 + 2> roundtrip{};
    roundtrip.fill(0xA5);
    VideoCore::LinearCopy<false, VideoCore::PixelFormat::RGB8, true>(decoded, roundtrip);
    auto expected_roundtrip = encoded;
    expected_roundtrip[pixel_count * 3] = 0xA5;
    expected_roundtrip[pixel_count * 3 + 1] = 0xA5;
    REQUIRE(roundtrip == expected_roundtrip);
}

template <VideoCore::PixelFormat format>
void CheckLinearConverted16() {
    constexpr std::size_t pixel_count = 37;
    std::array<u8, pixel_count * 2 + 3> encoded{};
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const u16 value = static_cast<u16>((pixel * 0x8421U + 0x1F3DU) & 0xFFFFU);
        encoded[pixel * 2] = static_cast<u8>(value);
        encoded[pixel * 2 + 1] = static_cast<u8>(value >> 8);
    }
    encoded[pixel_count * 2] = 0xD1;
    encoded[pixel_count * 2 + 1] = 0xD2;
    encoded[pixel_count * 2 + 2] = 0xD3;

    std::array<u8, pixel_count * 4 + 3> expected{};
    expected.fill(0xCD);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const auto rgba = DecodeConverted16<format>(encoded.data() + pixel * 2);
        std::memcpy(expected.data() + pixel * 4, rgba.AsArray(), 4);
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::LinearCopy<true, format, true>(encoded, decoded);
    REQUIRE(decoded == expected);

    std::array<u8, pixel_count * 2 + 3> roundtrip{};
    roundtrip.fill(0xA5);
    VideoCore::LinearCopy<false, format, true>(decoded, roundtrip);
    auto expected_roundtrip = encoded;
    expected_roundtrip[pixel_count * 2] = 0xA5;
    expected_roundtrip[pixel_count * 2 + 1] = 0xA5;
    expected_roundtrip[pixel_count * 2 + 2] = 0xA5;
    REQUIRE(roundtrip == expected_roundtrip);
}

template <bool has_alpha>
void CheckETC1SubtileDirect(u64 value, u64 alpha, std::ptrdiff_t output_stride) {
    constexpr std::size_t row_stride = 24;
    constexpr std::size_t guard_size = 16;
    std::array<u8, guard_size * 2 + row_stride * 4> expected{};
    expected.fill(0xCD);

    const std::size_t output_offset =
        guard_size + (output_stride < 0 ? row_stride * 3 : std::size_t{0});
    u8* const expected_output = expected.data() + output_offset;
    for (u32 y = 0; y < 4; ++y) {
        for (u32 x = 0; x < 4; ++x) {
            u8* const pixel = expected_output + y * output_stride + x * 4;
            const auto rgb = Pica::Texture::SampleETC1Subtile(value, x, y);
            pixel[0] = rgb.r();
            pixel[1] = rgb.g();
            pixel[2] = rgb.b();
            if constexpr (has_alpha) {
                const u8 alpha4 = static_cast<u8>((alpha >> (4 * (x * 4 + y))) & 0xF);
                pixel[3] = static_cast<u8>((alpha4 << 4) | alpha4);
            } else {
                pixel[3] = 0xFF;
            }
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    u8* const decoded_output = decoded.data() + output_offset;
    if constexpr (has_alpha) {
        Pica::Texture::DecodeETC1A4Subtile(value, alpha, decoded_output, output_stride);
    } else {
        Pica::Texture::DecodeETC1Subtile(value, decoded_output, output_stride);
    }
    REQUIRE(decoded == expected);
}

void CheckETC1SubtileCoverage() {
    constexpr std::array<u16, 8> bit_patterns = {
        0x0000, 0xFFFF, 0xAAAA, 0x5555, 0x8001, 0xF00F, 0x0FF0, 0x6996,
    };
    constexpr std::array<u64, 4> alpha_patterns = {
        0x0000000000000000ULL,
        0xFFFFFFFFFFFFFFFFULL,
        0x0123456789ABCDEFULL,
        0xF0E1D2C3B4A59687ULL,
    };
    constexpr u64 controlled_bits = 0xFFFFFFFFULL | (u64{0xFF} << 32);

    u64 state = 0xD1B54A32D192ED03ULL;
    for (u32 iteration = 0; iteration < 128; ++iteration) {
        state = state * 0x5851F42D4C957F2DULL + 0x14057B7EF767814FULL;
        u64 value = state & ~controlled_bits;
        value |= static_cast<u64>(bit_patterns[iteration & 7]);
        value |= static_cast<u64>(bit_patterns[(iteration / 8 + 3) & 7]) << 16;
        value |= static_cast<u64>(iteration & 3) << 32;
        value |= static_cast<u64>(iteration & 7) << 34;
        value |= static_cast<u64>((7 - iteration) & 7) << 37;

        const u64 alpha = iteration < alpha_patterns.size() ? alpha_patterns[iteration]
                                                            : state ^ 0xA5F03C96963CF0A5ULL;
        for (const std::ptrdiff_t output_stride : {std::ptrdiff_t{24}, std::ptrdiff_t{-24}}) {
            CheckETC1SubtileDirect<false>(value, alpha, output_stride);
            CheckETC1SubtileDirect<true>(value, alpha, output_stride);
        }
    }
}

template <VideoCore::PixelFormat format>
void CheckETC1() {
    static_assert(format == VideoCore::PixelFormat::ETC1 ||
                  format == VideoCore::PixelFormat::ETC1A4);
    constexpr bool has_alpha = format == VideoCore::PixelFormat::ETC1A4;
    constexpr u32 subtile_size = has_alpha ? 16 : 8;
    std::array<u8, subtile_size * 4> tiled{};
    for (std::size_t i = 0; i < tiled.size(); ++i) {
        tiled[i] = static_cast<u8>((i * 47 + 31) & 0xFF);
    }

    // Exercise all four flip/differential mode combinations across the tile's subblocks.
    for (u32 subtile = 0; subtile < 4; ++subtile) {
        constexpr u32 color_offset = has_alpha ? 8 : 0;
        tiled[subtile * subtile_size + color_offset + 4] =
            static_cast<u8>((tiled[subtile * subtile_size + color_offset + 4] & ~0x3U) | subtile);
    }

    std::array<u8, 10 * 8 * 4> expected{};
    expected.fill(0xCD);
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u32 subtile = x / 4 + 2 * (y / 4);
            const u8* subtile_ptr = tiled.data() + subtile * subtile_size;
            u8 alpha = 0xFF;
            if constexpr (has_alpha) {
                const u64_le packed_alpha = VideoCore::MakeInt<u64_le>(subtile_ptr);
                alpha = Common::Color::Convert4To8(
                    static_cast<u8>((packed_alpha >> (4 * ((x % 4) * 4 + y % 4))) & 0xF));
                subtile_ptr += sizeof(u64);
            }

            const u64_le value = VideoCore::MakeInt<u64_le>(subtile_ptr);
            const auto rgb = Pica::Texture::SampleETC1Subtile(value, x % 4, y % 4);
            const u32 dest = ((7 - y) * 10 + x) * 4;
            expected[dest] = rgb.r();
            expected[dest + 1] = rgb.g();
            expected[dest + 2] = rgb.b();
            expected[dest + 3] = alpha;
        }
    }

    auto decoded = expected;
    decoded.fill(0xCD);
    VideoCore::MortonCopyTile<true, format, false>(10, tiled, decoded);
    REQUIRE(decoded == expected);
}

} // namespace

TEST_CASE("PICA tile codec matches scalar Morton layout", "[video_core][texture]") {
    SECTION("RGBA8 native") {
        CheckTileCodec<VideoCore::PixelFormat::RGBA8>();
    }
    SECTION("RGBA8 converted byte order") {
        CheckTileCodec<VideoCore::PixelFormat::RGBA8, true>();
    }
    SECTION("RGB8 native") {
        CheckTileCodec<VideoCore::PixelFormat::RGB8>();
    }
    SECTION("RGB8 converted to RGBA8") {
        CheckConvertedRGB8();
    }
    SECTION("RGB5A1 converted to RGBA8 exhaustive") {
        CheckConverted16<VideoCore::PixelFormat::RGB5A1>();
    }
    SECTION("RGB565 converted to RGBA8 exhaustive") {
        CheckConverted16<VideoCore::PixelFormat::RGB565>();
    }
    SECTION("RGBA4 converted to RGBA8 exhaustive") {
        CheckConverted16<VideoCore::PixelFormat::RGBA4>();
    }
    SECTION("RGB5A1") {
        CheckTileCodec<VideoCore::PixelFormat::RGB5A1>();
    }
    SECTION("RGB565") {
        CheckTileCodec<VideoCore::PixelFormat::RGB565>();
    }
    SECTION("RGBA4") {
        CheckTileCodec<VideoCore::PixelFormat::RGBA4>();
    }
    SECTION("D16") {
        CheckTileCodec<VideoCore::PixelFormat::D16>();
    }
    SECTION("D24") {
        CheckTileCodec<VideoCore::PixelFormat::D24>();
    }
    SECTION("D24 converted to D32 float") {
        CheckConvertedD24();
    }
    SECTION("D24S8 byte rotation") {
        CheckD24S8();
    }
    SECTION("IA8 expansion") {
        CheckExpandedTexture<VideoCore::PixelFormat::IA8>();
    }
    SECTION("RG8 expansion") {
        CheckExpandedTexture<VideoCore::PixelFormat::RG8>();
    }
    SECTION("I8 expansion") {
        CheckExpandedTexture<VideoCore::PixelFormat::I8>();
    }
    SECTION("A8 expansion") {
        CheckExpandedTexture<VideoCore::PixelFormat::A8>();
    }
    SECTION("IA4 expansion") {
        CheckExpandedTexture<VideoCore::PixelFormat::IA4>();
    }
    SECTION("I4 expansion") {
        CheckExpanded4BitTexture<VideoCore::PixelFormat::I4>();
    }
    SECTION("A4 expansion") {
        CheckExpanded4BitTexture<VideoCore::PixelFormat::A4>();
    }
    SECTION("ETC1 block decode") {
        CheckETC1<VideoCore::PixelFormat::ETC1>();
    }
    SECTION("ETC1A4 block decode") {
        CheckETC1<VideoCore::PixelFormat::ETC1A4>();
    }
    SECTION("ETC1 direct decoder edge coverage") {
        CheckETC1SubtileCoverage();
    }
}

TEST_CASE("PICA linear converted codec preserves pixel layout", "[video_core][texture]") {
    SECTION("RGBA8 converted byte order") {
        CheckLinearConvertedRGBA8();
    }
    SECTION("RGB8 converted to RGBA8") {
        CheckLinearConvertedRGB8();
    }
    SECTION("RGB5A1 converted to RGBA8") {
        CheckLinearConverted16<VideoCore::PixelFormat::RGB5A1>();
    }
    SECTION("RGB565 converted to RGBA8") {
        CheckLinearConverted16<VideoCore::PixelFormat::RGB565>();
    }
    SECTION("RGBA4 converted to RGBA8") {
        CheckLinearConverted16<VideoCore::PixelFormat::RGBA4>();
    }
}

TEST_CASE("Vulkan depth-stencil staging unpack preserves planes", "[video_core][texture]") {
    SECTION("D24 unorm depth") {
        CheckDepthStencilUnpack<false>();
    }
    SECTION("D32 float depth") {
        CheckDepthStencilUnpack<true>();
    }
}
