// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <cstddef>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/rasterizer_cache/surface_base.h"

namespace {

constexpr std::size_t guard_size = 32;
constexpr std::size_t payload_size = 320;

void ReferenceFill(u8* destination, std::size_t start_offset, std::size_t end_offset,
                   const std::array<u8, 4>& fill_data, std::size_t fill_size) {
    for (std::size_t offset = start_offset; offset < end_offset; ++offset) {
        destination[offset] = fill_data[offset % fill_size];
    }
}

VideoCore::SurfaceBase MakeFillSurface(const std::array<u8, 4>& fill_data, u32 fill_size) {
    VideoCore::SurfaceParams params{};
    params.type = VideoCore::SurfaceType::Fill;
    VideoCore::SurfaceBase surface{params, {}};
    surface.fill_data = fill_data;
    surface.fill_size = fill_size;
    return surface;
}

} // namespace

TEST_CASE("Fill surfaces materialize exact byte patterns", "[video_core][surface]") {
    for (const u32 fill_size : {2u, 3u, 4u}) {
        for (u32 pattern_case = 0; pattern_case < 8; ++pattern_case) {
            std::array<u8, 4> fill_data{};
            for (u32 index = 0; index < fill_size; ++index) {
                fill_data[index] = pattern_case == 0
                                       ? u8{0x5A}
                                       : static_cast<u8>(pattern_case * 47 + index * 83 + 11);
            }
            auto surface = MakeFillSurface(fill_data, fill_size);

            for (std::size_t start_offset = 0; start_offset < 16; ++start_offset) {
                for (std::size_t length = 0; length <= 256; ++length) {
                    CAPTURE(fill_size, pattern_case, start_offset, length);
                    std::array<u8, guard_size + payload_size + guard_size> initial{};
                    for (std::size_t index = 0; index < initial.size(); ++index) {
                        initial[index] = static_cast<u8>(index * 29 + 0xA5);
                    }
                    auto expected = initial;
                    auto actual = initial;
                    ReferenceFill(expected.data() + guard_size, start_offset, start_offset + length,
                                  fill_data, fill_size);
                    surface.FillMemory(actual.data() + guard_size, start_offset,
                                       start_offset + length);
                    REQUIRE(actual == expected);
                }
            }
        }
    }
}

TEST_CASE("Fill surfaces bulk-copy large unaligned ranges", "[video_core][surface]") {
    constexpr std::size_t large_size = 1024 * 1024 + 37;
    constexpr std::array<u8, 4> fill_data{0x13, 0xA7, 0x5C, 0};
    auto surface = MakeFillSurface(fill_data, 3);

    std::vector<u8> expected(guard_size + large_size + guard_size, 0xCD);
    auto actual = expected;
    ReferenceFill(expected.data() + guard_size, 1, large_size - 2, fill_data, 3);
    surface.FillMemory(actual.data() + guard_size, 1, large_size - 2);
    REQUIRE(actual == expected);
}
