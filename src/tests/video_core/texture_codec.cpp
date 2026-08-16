// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/rasterizer_cache/texture_codec.h"

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

} // namespace

TEST_CASE("PICA tile codec matches scalar Morton layout", "[video_core][texture]") {
    SECTION("RGBA8 native") {
        CheckTileCodec<VideoCore::PixelFormat::RGBA8>();
    }
    SECTION("RGBA8 converted byte order") {
        CheckTileCodec<VideoCore::PixelFormat::RGBA8, true>();
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
}
