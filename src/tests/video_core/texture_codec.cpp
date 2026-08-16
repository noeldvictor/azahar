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
    SECTION("ETC1 block decode") {
        CheckETC1<VideoCore::PixelFormat::ETC1>();
    }
    SECTION("ETC1A4 block decode") {
        CheckETC1<VideoCore::PixelFormat::ETC1A4>();
    }
}
