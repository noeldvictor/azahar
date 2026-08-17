// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include "common/alignment.h"
#include "common/arch.h"
#include "common/color.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/texture/etc1.h"
#include "video_core/utils.h"

#if CITRA_ARCH(arm64)
#include <arm_neon.h>
#endif

namespace VideoCore {

template <typename T>
inline T MakeInt(const u8* bytes) {
    T integer{};
    std::memcpy(&integer, bytes, sizeof(T));

    return integer;
}

template <PixelFormat format, bool converted>
constexpr void DecodePixel(const u8* source, u8* dest) {
    using namespace Common::Color;
    constexpr u32 bytes_per_pixel = GetFormatBpp(format) / 8;

    if constexpr (format == PixelFormat::RGBA8 && converted) {
        const auto abgr = DecodeRGBA8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGB8 && converted) {
        const auto abgr = DecodeRGB8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGB565 && converted) {
        const auto abgr = DecodeRGB565(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGB5A1 && converted) {
        const auto abgr = DecodeRGB5A1(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGBA4 && converted) {
        const auto abgr = DecodeRGBA4(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::IA8) {
        const auto abgr = DecodeIA8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RG8) {
        const auto abgr = DecodeRG8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::I8) {
        const auto abgr = DecodeI8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::A8) {
        const auto abgr = DecodeA8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::IA4) {
        const auto abgr = DecodeIA4(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::D24 && converted) {
        const auto d32 = DecodeD24(source) / 16777215.f;
        std::memcpy(dest, &d32, sizeof(d32));
    } else if constexpr (format == PixelFormat::D24S8) {
        const u32 d24s8 = std::rotl(MakeInt<u32>(source), 8);
        std::memcpy(dest, &d24s8, sizeof(u32));
    } else {
        std::memcpy(dest, source, bytes_per_pixel);
    }
}

template <PixelFormat format>
constexpr void DecodePixel4(u32 x, u32 y, const u8* source_tile, u8* dest_pixel) {
    const u32 morton_offset = VideoCore::MortonInterleave(x, y);
    const u8 value = source_tile[morton_offset >> 1];
    const u8 pixel = Common::Color::Convert4To8((morton_offset % 2) ? (value >> 4) : (value & 0xF));

    if constexpr (format == PixelFormat::I4) {
        std::memset(dest_pixel, pixel, 3);
        dest_pixel[3] = 255;
    } else {
        std::memset(dest_pixel, 0, 3);
        dest_pixel[3] = pixel;
    }
}

template <PixelFormat format, bool converted>
constexpr void EncodePixel(const u8* source, u8* dest) {
    using namespace Common::Color;
    constexpr u32 bytes_per_pixel = GetFormatBpp(format) / 8;

    if constexpr (format == PixelFormat::RGBA8 && converted) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeRGBA8(rgba, dest);
    } else if constexpr (format == PixelFormat::RGB8 && converted) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeRGB8(rgba, dest);
    } else if constexpr (format == PixelFormat::RGB565 && converted) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeRGB565(rgba, dest);
    } else if constexpr (format == PixelFormat::RGB5A1 && converted) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeRGB5A1(rgba, dest);
    } else if constexpr (format == PixelFormat::RGBA4 && converted) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeRGBA4(rgba, dest);
    } else if constexpr (format == PixelFormat::IA8) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeIA8(rgba, dest);
    } else if constexpr (format == PixelFormat::RG8) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeRG8(rgba, dest);
    } else if constexpr (format == PixelFormat::I8) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeI8(rgba, dest);
    } else if constexpr (format == PixelFormat::A8) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeA8(rgba, dest);
    } else if constexpr (format == PixelFormat::IA4) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        EncodeIA4(rgba, dest);
    } else if constexpr (format == PixelFormat::D24 && converted) {
        float d32;
        std::memcpy(&d32, source, sizeof(d32));
        EncodeD24(static_cast<u32>(d32 * 0xFFFFFF), dest);
    } else if constexpr (format == PixelFormat::D24S8) {
        const u32 s8d24 = std::rotr(MakeInt<u32>(source), 8);
        std::memcpy(dest, &s8d24, sizeof(u32));
    } else {
        std::memcpy(dest, source, bytes_per_pixel);
    }
}

template <PixelFormat format>
constexpr void EncodePixel4(u32 x, u32 y, const u8* source_pixel, u8* dest_tile_buffer) {
    Common::Vec4<u8> rgba;
    std::memcpy(rgba.AsArray(), source_pixel, 4);

    u8 pixel;
    if constexpr (format == PixelFormat::I4) {
        pixel = Common::Color::AverageRgbComponents(rgba);
    } else {
        pixel = rgba.a();
    }

    const u32 morton_offset = VideoCore::MortonInterleave(x, y);
    const u32 byte_offset = morton_offset >> 1;

    const u8 current_values = dest_tile_buffer[byte_offset];
    const u8 new_value = Common::Color::Convert8To4(pixel);

    if (morton_offset % 2) {
        dest_tile_buffer[byte_offset] = (new_value << 4) | (current_values & 0x0F);
    } else {
        dest_tile_buffer[byte_offset] = (current_values & 0xF0) | new_value;
    }
}

template <PixelFormat format>
inline void MortonCopyTileETC1(u32 stride, const u8* tile_buffer, u8* linear_buffer) {
    static_assert(format == PixelFormat::ETC1 || format == PixelFormat::ETC1A4);
    constexpr bool has_alpha = format == PixelFormat::ETC1A4;
    constexpr u32 subtile_size = has_alpha ? 16 : 8;
    constexpr u32 subtile_width = 4;
    constexpr u32 subtile_height = 4;
    const std::ptrdiff_t output_stride = -static_cast<std::ptrdiff_t>(stride * sizeof(u32));

    for (u32 subtile_y = 0; subtile_y < 2; ++subtile_y) {
        for (u32 subtile_x = 0; subtile_x < 2; ++subtile_x) {
            const u32 subtile_index = subtile_x + 2 * subtile_y;
            const u8* subtile_ptr = tile_buffer + subtile_index * subtile_size;
            u8* const output = linear_buffer + ((7 - subtile_y * subtile_height) * stride +
                                                subtile_x * subtile_width) *
                                                   sizeof(u32);

            if constexpr (has_alpha) {
                const u64_le alpha = MakeInt<u64_le>(subtile_ptr);
                const u64_le value = MakeInt<u64_le>(subtile_ptr + sizeof(u64));
                Pica::Texture::DecodeETC1A4Subtile(value, alpha, output, output_stride);
            } else {
                const u64_le value = MakeInt<u64_le>(subtile_ptr);
                Pica::Texture::DecodeETC1Subtile(value, output, output_stride);
            }
        }
    }
}

#if CITRA_ARCH(arm64)

enum class Pixel32TransformA64 {
    None,
    ReverseBytes,
    D24S8,
};

inline constexpr std::array<u8, 16> D24S8_TO_HOST_A64 = {
    3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14,
};
inline constexpr std::array<u8, 16> D24S8_FROM_HOST_A64 = {
    1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12,
};

static_assert([] {
    for (u32 i = 0; i < D24S8_TO_HOST_A64.size(); ++i) {
        if (D24S8_TO_HOST_A64[D24S8_FROM_HOST_A64[i]] != i) {
            return false;
        }
    }
    return true;
}());

template <bool morton_to_linear, Pixel32TransformA64 transform>
inline uint8x16_t TransformPixel32A64(uint8x16_t pixels) {
    if constexpr (transform == Pixel32TransformA64::ReverseBytes) {
        return vrev32q_u8(pixels);
    } else if constexpr (transform == Pixel32TransformA64::D24S8) {
        constexpr const auto& indices = morton_to_linear ? D24S8_TO_HOST_A64 : D24S8_FROM_HOST_A64;
        return vqtbl1q_u8(pixels, vld1q_u8(indices.data()));
    } else {
        return pixels;
    }
}

template <bool morton_to_linear, Pixel32TransformA64 transform>
inline void MortonCopyTile32A64(u32 stride, u8* tile_buffer, u8* linear_buffer) {
    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) * sizeof(u32);
        u8* const first_row = linear_buffer + (7 - y) * stride * sizeof(u32);
        u8* const second_row = first_row - stride * sizeof(u32);

        if constexpr (morton_to_linear) {
            const uint64x2x2_t left =
                vld2q_u64(reinterpret_cast<const u64*>(tile_buffer + tile_offset));
            const uint64x2x2_t right =
                vld2q_u64(reinterpret_cast<const u64*>(tile_buffer + tile_offset + 64));

            uint8x16_t first_left = vreinterpretq_u8_u64(left.val[0]);
            uint8x16_t second_left = vreinterpretq_u8_u64(left.val[1]);
            uint8x16_t first_right = vreinterpretq_u8_u64(right.val[0]);
            uint8x16_t second_right = vreinterpretq_u8_u64(right.val[1]);
            if constexpr (transform != Pixel32TransformA64::None) {
                first_left = TransformPixel32A64<morton_to_linear, transform>(first_left);
                second_left = TransformPixel32A64<morton_to_linear, transform>(second_left);
                first_right = TransformPixel32A64<morton_to_linear, transform>(first_right);
                second_right = TransformPixel32A64<morton_to_linear, transform>(second_right);
            }

            vst1q_u8(first_row, first_left);
            vst1q_u8(first_row + 16, first_right);
            vst1q_u8(second_row, second_left);
            vst1q_u8(second_row + 16, second_right);
        } else {
            uint8x16_t first_left = vld1q_u8(first_row);
            uint8x16_t first_right = vld1q_u8(first_row + 16);
            uint8x16_t second_left = vld1q_u8(second_row);
            uint8x16_t second_right = vld1q_u8(second_row + 16);
            if constexpr (transform != Pixel32TransformA64::None) {
                first_left = TransformPixel32A64<morton_to_linear, transform>(first_left);
                first_right = TransformPixel32A64<morton_to_linear, transform>(first_right);
                second_left = TransformPixel32A64<morton_to_linear, transform>(second_left);
                second_right = TransformPixel32A64<morton_to_linear, transform>(second_right);
            }

            uint64x2x2_t left;
            left.val[0] = vreinterpretq_u64_u8(first_left);
            left.val[1] = vreinterpretq_u64_u8(second_left);
            vst2q_u64(reinterpret_cast<u64*>(tile_buffer + tile_offset), left);

            uint64x2x2_t right;
            right.val[0] = vreinterpretq_u64_u8(first_right);
            right.val[1] = vreinterpretq_u64_u8(second_right);
            vst2q_u64(reinterpret_cast<u64*>(tile_buffer + tile_offset + 64), right);
        }
    }
}

template <bool morton_to_linear>
inline void MortonCopyTile16A64(u32 stride, u8* tile_buffer, u8* linear_buffer) {
    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) * sizeof(u16);
        u16* const first_row =
            reinterpret_cast<u16*>(linear_buffer + (7 - y) * stride * sizeof(u16));
        u16* const second_row = first_row - stride;

        if constexpr (morton_to_linear) {
            const uint32x2x2_t left =
                vld2_u32(reinterpret_cast<const u32*>(tile_buffer + tile_offset));
            const uint32x2x2_t right =
                vld2_u32(reinterpret_cast<const u32*>(tile_buffer + tile_offset + 32));
            vst1q_u16(first_row, vreinterpretq_u16_u32(vcombine_u32(left.val[0], right.val[0])));
            vst1q_u16(second_row, vreinterpretq_u16_u32(vcombine_u32(left.val[1], right.val[1])));
        } else {
            const uint32x4_t first = vreinterpretq_u32_u16(vld1q_u16(first_row));
            const uint32x4_t second = vreinterpretq_u32_u16(vld1q_u16(second_row));

            uint32x2x2_t left;
            left.val[0] = vget_low_u32(first);
            left.val[1] = vget_low_u32(second);
            vst2_u32(reinterpret_cast<u32*>(tile_buffer + tile_offset), left);

            uint32x2x2_t right;
            right.val[0] = vget_high_u32(first);
            right.val[1] = vget_high_u32(second);
            vst2_u32(reinterpret_cast<u32*>(tile_buffer + tile_offset + 32), right);
        }
    }
}

// A two-row band in a 24-bit Morton tile is split into two eight-pixel chunks. Within each
// chunk, pairs of pixels alternate between the two rows. Express that permutation directly as
// an AArch64 table lookup instead of expanding 64 per-pixel Morton offsets and 3/4-byte copies.
inline constexpr std::array<u8, 16> MORTON_24_TO_ROWS_A64 = {
    0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
};
inline constexpr std::array<u8, 16> ROWS_TO_MORTON_24_A64 = {
    0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
};

static_assert([] {
    for (u32 i = 0; i < MORTON_24_TO_ROWS_A64.size(); ++i) {
        if (MORTON_24_TO_ROWS_A64[ROWS_TO_MORTON_24_A64[i]] != i) {
            return false;
        }
    }
    return true;
}());

inline uint8x16_t Morton24ToRowsA64(uint8x8_t left, uint8x8_t right) {
    const uint8x16_t pixels = vcombine_u8(left, right);
    const uint8x16_t indices = vld1q_u8(MORTON_24_TO_ROWS_A64.data());
    return vqtbl1q_u8(pixels, indices);
}

inline uint8x16_t RowsToMorton24A64(uint8x8_t first, uint8x8_t second) {
    const uint8x16_t rows = vcombine_u8(first, second);
    const uint8x16_t indices = vld1q_u8(ROWS_TO_MORTON_24_A64.data());
    return vqtbl1q_u8(rows, indices);
}

// Cortex-A510 executes a D-form byte ST3 only once every 17 cycles. Two TBL2 permutations plus
// ordinary Q/D stores retain the exact packed 24-bit byte stream while avoiding that slow
// store. TBL2 and ordinary stores are also competitive with ST3 on the X3, A715, and A710.
inline constexpr std::array<std::array<u8, 16>, 2> RGB8_STORE_SHUFFLES_A64 = {{
    {0, 8, 16, 1, 9, 17, 2, 10, 18, 3, 11, 19, 4, 12, 20, 5},
    {13, 21, 6, 14, 22, 7, 15, 23, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
}};

static_assert([] {
    for (u32 output = 0; output < 24; ++output) {
        const u32 pixel = output / 3;
        const u32 component = output % 3;
        const u8 expected_index = static_cast<u8>(component * 8 + pixel);
        if (RGB8_STORE_SHUFFLES_A64[output / 16][output % 16] != expected_index) {
            return false;
        }
    }
    return true;
}());

inline void StoreRGB8RowA64(u8* dest, uint8x8_t component_0, uint8x8_t component_1,
                            uint8x8_t component_2) {
    const uint8x16x2_t table = {
        vcombine_u8(component_0, component_1),
        vcombine_u8(component_2, vdup_n_u8(0)),
    };
    const uint8x16_t first = vqtbl2q_u8(table, vld1q_u8(RGB8_STORE_SHUFFLES_A64[0].data()));
    const uint8x16_t second = vqtbl2q_u8(table, vld1q_u8(RGB8_STORE_SHUFFLES_A64[1].data()));
    vst1q_u8(dest, first);
    vst1_u8(dest + 16, vget_low_u8(second));
}

// Avoid ST4 here. It is a complex, throughput-limited store on the Cortex-X3/A710 and is
// especially expensive on the Cortex-A510 efficiency cores in Snapdragon 8 Gen 2. ZIP keeps the
// interleave on the vector ALUs and lets the store pipelines consume ordinary contiguous vectors.
inline void StoreRGBA8RowsA64(u8* first_row, u8* second_row, uint8x16_t red, uint8x16_t green,
                              uint8x16_t blue, uint8x16_t alpha) {
    const uint8x16x2_t red_green = vzipq_u8(red, green);
    const uint8x16x2_t blue_alpha = vzipq_u8(blue, alpha);
    const uint16x8x2_t first =
        vzipq_u16(vreinterpretq_u16_u8(red_green.val[0]), vreinterpretq_u16_u8(blue_alpha.val[0]));
    const uint16x8x2_t second =
        vzipq_u16(vreinterpretq_u16_u8(red_green.val[1]), vreinterpretq_u16_u8(blue_alpha.val[1]));

    vst1q_u8(first_row, vreinterpretq_u8_u16(first.val[0]));
    vst1q_u8(first_row + 16, vreinterpretq_u8_u16(first.val[1]));
    vst1q_u8(second_row, vreinterpretq_u8_u16(second.val[0]));
    vst1q_u8(second_row + 16, vreinterpretq_u8_u16(second.val[1]));
}

template <PixelFormat format>
inline uint8x8x4_t DecodePixels16A64(uint16x8_t pixels) {
    static_assert(format == PixelFormat::RGB5A1 || format == PixelFormat::RGB565 ||
                  format == PixelFormat::RGBA4);

    uint16x8_t red;
    uint16x8_t green;
    uint16x8_t blue;
    uint16x8_t alpha;
    if constexpr (format == PixelFormat::RGB565) {
        red = vshrq_n_u16(pixels, 11);
        green = vandq_u16(vshrq_n_u16(pixels, 5), vdupq_n_u16(0x3F));
        blue = vandq_u16(pixels, vdupq_n_u16(0x1F));
        red = vorrq_u16(vshlq_n_u16(red, 3), vshrq_n_u16(red, 2));
        green = vorrq_u16(vshlq_n_u16(green, 2), vshrq_n_u16(green, 4));
        blue = vorrq_u16(vshlq_n_u16(blue, 3), vshrq_n_u16(blue, 2));
        alpha = vdupq_n_u16(0xFF);
    } else if constexpr (format == PixelFormat::RGB5A1) {
        red = vshrq_n_u16(pixels, 11);
        green = vandq_u16(vshrq_n_u16(pixels, 6), vdupq_n_u16(0x1F));
        blue = vandq_u16(vshrq_n_u16(pixels, 1), vdupq_n_u16(0x1F));
        red = vorrq_u16(vshlq_n_u16(red, 3), vshrq_n_u16(red, 2));
        green = vorrq_u16(vshlq_n_u16(green, 3), vshrq_n_u16(green, 2));
        blue = vorrq_u16(vshlq_n_u16(blue, 3), vshrq_n_u16(blue, 2));
        alpha = vceqq_u16(vandq_u16(pixels, vdupq_n_u16(1)), vdupq_n_u16(1));
    } else {
        red = vshrq_n_u16(pixels, 12);
        green = vandq_u16(vshrq_n_u16(pixels, 8), vdupq_n_u16(0x0F));
        blue = vandq_u16(vshrq_n_u16(pixels, 4), vdupq_n_u16(0x0F));
        alpha = vandq_u16(pixels, vdupq_n_u16(0x0F));
        red = vorrq_u16(vshlq_n_u16(red, 4), red);
        green = vorrq_u16(vshlq_n_u16(green, 4), green);
        blue = vorrq_u16(vshlq_n_u16(blue, 4), blue);
        alpha = vorrq_u16(vshlq_n_u16(alpha, 4), alpha);
    }

    uint8x8x4_t result;
    result.val[0] = vmovn_u16(red);
    result.val[1] = vmovn_u16(green);
    result.val[2] = vmovn_u16(blue);
    result.val[3] = vmovn_u16(alpha);
    return result;
}

template <PixelFormat format>
inline uint16x8_t EncodePixels16A64(const uint8x8x4_t& pixels) {
    static_assert(format == PixelFormat::RGB5A1 || format == PixelFormat::RGB565 ||
                  format == PixelFormat::RGBA4);

    const uint16x8_t red = vmovl_u8(pixels.val[0]);
    const uint16x8_t green = vmovl_u8(pixels.val[1]);
    const uint16x8_t blue = vmovl_u8(pixels.val[2]);
    const uint16x8_t alpha = vmovl_u8(pixels.val[3]);
    if constexpr (format == PixelFormat::RGB565) {
        const uint16x8_t red5 = vshlq_n_u16(vshrq_n_u16(red, 3), 11);
        const uint16x8_t green6 = vshlq_n_u16(vshrq_n_u16(green, 2), 5);
        const uint16x8_t blue5 = vshrq_n_u16(blue, 3);
        return vorrq_u16(vorrq_u16(red5, green6), blue5);
    } else if constexpr (format == PixelFormat::RGB5A1) {
        const uint16x8_t red5 = vshlq_n_u16(vshrq_n_u16(red, 3), 11);
        const uint16x8_t green5 = vshlq_n_u16(vshrq_n_u16(green, 3), 6);
        const uint16x8_t blue5 = vshlq_n_u16(vshrq_n_u16(blue, 3), 1);
        const uint16x8_t alpha1 = vshrq_n_u16(alpha, 7);
        return vorrq_u16(vorrq_u16(red5, green5), vorrq_u16(blue5, alpha1));
    } else {
        const uint16x8_t red4 = vshlq_n_u16(vshrq_n_u16(red, 4), 12);
        const uint16x8_t green4 = vshlq_n_u16(vshrq_n_u16(green, 4), 8);
        const uint16x8_t blue4 = vshlq_n_u16(vshrq_n_u16(blue, 4), 4);
        const uint16x8_t alpha4 = vshrq_n_u16(alpha, 4);
        return vorrq_u16(vorrq_u16(red4, green4), vorrq_u16(blue4, alpha4));
    }
}

template <bool morton_to_linear, PixelFormat format>
inline void MortonCopyTile16ConvertedA64(u32 stride, u8* tile_buffer, u8* linear_buffer) {
    static_assert(format == PixelFormat::RGB5A1 || format == PixelFormat::RGB565 ||
                  format == PixelFormat::RGBA4);

    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) * sizeof(u16);
        u8* const first_row = linear_buffer + (7 - y) * stride * sizeof(u32);
        u8* const second_row = first_row - stride * sizeof(u32);

        if constexpr (morton_to_linear) {
            const uint32x2x2_t left =
                vld2_u32(reinterpret_cast<const u32*>(tile_buffer + tile_offset));
            const uint32x2x2_t right =
                vld2_u32(reinterpret_cast<const u32*>(tile_buffer + tile_offset + 32));
            const uint16x8_t first = vreinterpretq_u16_u32(vcombine_u32(left.val[0], right.val[0]));
            const uint16x8_t second =
                vreinterpretq_u16_u32(vcombine_u32(left.val[1], right.val[1]));
            const uint8x8x4_t first_pixels = DecodePixels16A64<format>(first);
            const uint8x8x4_t second_pixels = DecodePixels16A64<format>(second);
            StoreRGBA8RowsA64(first_row, second_row,
                              vcombine_u8(first_pixels.val[0], second_pixels.val[0]),
                              vcombine_u8(first_pixels.val[1], second_pixels.val[1]),
                              vcombine_u8(first_pixels.val[2], second_pixels.val[2]),
                              vcombine_u8(first_pixels.val[3], second_pixels.val[3]));
        } else {
            const uint16x8_t first = EncodePixels16A64<format>(vld4_u8(first_row));
            const uint16x8_t second = EncodePixels16A64<format>(vld4_u8(second_row));
            const uint32x4_t first_words = vreinterpretq_u32_u16(first);
            const uint32x4_t second_words = vreinterpretq_u32_u16(second);

            uint32x2x2_t left;
            left.val[0] = vget_low_u32(first_words);
            left.val[1] = vget_low_u32(second_words);
            vst2_u32(reinterpret_cast<u32*>(tile_buffer + tile_offset), left);

            uint32x2x2_t right;
            right.val[0] = vget_high_u32(first_words);
            right.val[1] = vget_high_u32(second_words);
            vst2_u32(reinterpret_cast<u32*>(tile_buffer + tile_offset + 32), right);
        }
    }
}

inline uint8x8_t Expand4BitPixelsA64(uint8x8_t packed) {
    uint8x8_t low = vand_u8(packed, vdup_n_u8(0x0F));
    uint8x8_t high = vshr_n_u8(packed, 4);
    low = vsli_n_u8(low, low, 4);
    high = vsli_n_u8(high, high, 4);
    return vzip_u8(low, high).val[0];
}

template <PixelFormat format>
inline void MortonCopyTile4To32A64(u32 stride, const u8* tile_buffer, u8* linear_buffer) {
    static_assert(format == PixelFormat::I4 || format == PixelFormat::A4);
    const uint8x16_t zero = vdupq_n_u8(0);
    const uint8x16_t opaque = vdupq_n_u8(0xFF);

    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) / 2;
        const u32 left = MakeInt<u32>(tile_buffer + tile_offset);
        const u32 right = MakeInt<u32>(tile_buffer + tile_offset + 8);
        const uint8x8_t packed = vreinterpret_u8_u64(
            vcreate_u64(static_cast<u64>(left) | (static_cast<u64>(right) << 32)));
        const uint8x8x2_t packed_rows = vuzp_u8(packed, packed);
        const uint8x16_t pixels = vcombine_u8(Expand4BitPixelsA64(packed_rows.val[0]),
                                              Expand4BitPixelsA64(packed_rows.val[1]));
        u8* const first_row = linear_buffer + (7 - y) * stride * sizeof(u32);
        u8* const second_row = first_row - stride * sizeof(u32);

        if constexpr (format == PixelFormat::I4) {
            StoreRGBA8RowsA64(first_row, second_row, pixels, pixels, pixels, opaque);
        } else {
            StoreRGBA8RowsA64(first_row, second_row, zero, zero, zero, pixels);
        }
    }
}

template <bool morton_to_linear, bool converted>
inline void MortonCopyTile24A64(u32 stride, u8* tile_buffer, u8* linear_buffer) {
    constexpr u32 encoded_bytes_per_pixel = 3;
    constexpr u32 linear_bytes_per_pixel = converted ? 4 : encoded_bytes_per_pixel;
    const uint8x16_t opaque = vdupq_n_u8(0xFF);

    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) * encoded_bytes_per_pixel;
        u8* const first_row = linear_buffer + (7 - y) * stride * linear_bytes_per_pixel;
        u8* const second_row = first_row - stride * linear_bytes_per_pixel;

        if constexpr (morton_to_linear) {
            const uint8x8x3_t left = vld3_u8(tile_buffer + tile_offset);
            const uint8x8x3_t right =
                vld3_u8(tile_buffer + tile_offset + 16 * encoded_bytes_per_pixel);
            const uint8x16_t component_0 = Morton24ToRowsA64(left.val[0], right.val[0]);
            const uint8x16_t component_1 = Morton24ToRowsA64(left.val[1], right.val[1]);
            const uint8x16_t component_2 = Morton24ToRowsA64(left.val[2], right.val[2]);

            if constexpr (converted) {
                StoreRGBA8RowsA64(first_row, second_row, component_2, component_1, component_0,
                                  opaque);
            } else {
                StoreRGB8RowA64(first_row, vget_low_u8(component_0), vget_low_u8(component_1),
                                vget_low_u8(component_2));
                StoreRGB8RowA64(second_row, vget_high_u8(component_0), vget_high_u8(component_1),
                                vget_high_u8(component_2));
            }
        } else {
            uint8x16_t component_0;
            uint8x16_t component_1;
            uint8x16_t component_2;
            if constexpr (converted) {
                const uint8x8x4_t first = vld4_u8(first_row);
                const uint8x8x4_t second = vld4_u8(second_row);
                component_0 = RowsToMorton24A64(first.val[2], second.val[2]);
                component_1 = RowsToMorton24A64(first.val[1], second.val[1]);
                component_2 = RowsToMorton24A64(first.val[0], second.val[0]);
            } else {
                const uint8x8x3_t first = vld3_u8(first_row);
                const uint8x8x3_t second = vld3_u8(second_row);
                component_0 = RowsToMorton24A64(first.val[0], second.val[0]);
                component_1 = RowsToMorton24A64(first.val[1], second.val[1]);
                component_2 = RowsToMorton24A64(first.val[2], second.val[2]);
            }

            StoreRGB8RowA64(tile_buffer + tile_offset, vget_low_u8(component_0),
                            vget_low_u8(component_1), vget_low_u8(component_2));
            StoreRGB8RowA64(tile_buffer + tile_offset + 16 * encoded_bytes_per_pixel,
                            vget_high_u8(component_0), vget_high_u8(component_1),
                            vget_high_u8(component_2));
        }
    }
}

template <u32 byte_shift>
inline uint8x8_t NarrowD24ComponentA64(uint32x4_t first, uint32x4_t second) {
    static_assert(byte_shift == 0 || byte_shift == 8 || byte_shift == 16);

    uint16x4_t first_narrow;
    uint16x4_t second_narrow;
    if constexpr (byte_shift == 0) {
        first_narrow = vmovn_u32(first);
        second_narrow = vmovn_u32(second);
    } else if constexpr (byte_shift == 8) {
        first_narrow = vshrn_n_u32(first, 8);
        second_narrow = vshrn_n_u32(second, 8);
    } else {
        first_narrow = vshrn_n_u32(first, 16);
        second_narrow = vshrn_n_u32(second, 16);
    }
    return vmovn_u16(vcombine_u16(first_narrow, second_narrow));
}

// PICA's D24 Morton tiles contain 64 packed little-endian 24-bit integers. Vulkan staging uses
// D32 float rows instead. Keep the exact scalar UCVTF/FDIV and FMUL/FCVTZU operations, but process
// four depths per instruction. The shuffle deliberately uses TBL1 plus ZIP/narrow operations:
// TBL4 issues only once every nine cycles on the Cortex-A510, while one-table TBL and ZIP have much
// higher documented throughput. D-form LD3 also de-interleaves each packed eight-pixel chunk in
// one instruction.
template <bool morton_to_linear>
inline void MortonCopyTileD24ConvertedA64(u32 stride, u8* tile_buffer, u8* linear_buffer) {
    constexpr u32 encoded_bytes_per_pixel = 3;
    constexpr u32 linear_bytes_per_pixel = sizeof(float);
    const float32x4_t d24_max = vdupq_n_f32(16777215.0f);

    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) * encoded_bytes_per_pixel;
        u8* const first_row = linear_buffer + (7 - y) * stride * linear_bytes_per_pixel;
        u8* const second_row = first_row - stride * linear_bytes_per_pixel;

        if constexpr (morton_to_linear) {
            const uint8x8x3_t left = vld3_u8(tile_buffer + tile_offset);
            const uint8x8x3_t right =
                vld3_u8(tile_buffer + tile_offset + 16 * encoded_bytes_per_pixel);
            const uint8x16_t low = Morton24ToRowsA64(left.val[0], right.val[0]);
            const uint8x16_t middle = Morton24ToRowsA64(left.val[1], right.val[1]);
            const uint8x16_t high = Morton24ToRowsA64(left.val[2], right.val[2]);
            const uint8x16_t zero = vdupq_n_u8(0);

            const uint8x16x2_t low_middle = vzipq_u8(low, middle);
            const uint8x16x2_t high_zero = vzipq_u8(high, zero);
            const uint16x8x2_t first = vzipq_u16(vreinterpretq_u16_u8(low_middle.val[0]),
                                                 vreinterpretq_u16_u8(high_zero.val[0]));
            const uint16x8x2_t second = vzipq_u16(vreinterpretq_u16_u8(low_middle.val[1]),
                                                  vreinterpretq_u16_u8(high_zero.val[1]));

            const uint32x4_t first_low = vreinterpretq_u32_u16(first.val[0]);
            const uint32x4_t first_high = vreinterpretq_u32_u16(first.val[1]);
            const uint32x4_t second_low = vreinterpretq_u32_u16(second.val[0]);
            const uint32x4_t second_high = vreinterpretq_u32_u16(second.val[1]);
            vst1q_f32(reinterpret_cast<float*>(first_row),
                      vdivq_f32(vcvtq_f32_u32(first_low), d24_max));
            vst1q_f32(reinterpret_cast<float*>(first_row + 16),
                      vdivq_f32(vcvtq_f32_u32(first_high), d24_max));
            vst1q_f32(reinterpret_cast<float*>(second_row),
                      vdivq_f32(vcvtq_f32_u32(second_low), d24_max));
            vst1q_f32(reinterpret_cast<float*>(second_row + 16),
                      vdivq_f32(vcvtq_f32_u32(second_high), d24_max));
        } else {
            const uint32x4_t first_low = vcvtq_u32_f32(
                vmulq_f32(vld1q_f32(reinterpret_cast<const float*>(first_row)), d24_max));
            const uint32x4_t first_high = vcvtq_u32_f32(
                vmulq_f32(vld1q_f32(reinterpret_cast<const float*>(first_row + 16)), d24_max));
            const uint32x4_t second_low = vcvtq_u32_f32(
                vmulq_f32(vld1q_f32(reinterpret_cast<const float*>(second_row)), d24_max));
            const uint32x4_t second_high = vcvtq_u32_f32(
                vmulq_f32(vld1q_f32(reinterpret_cast<const float*>(second_row + 16)), d24_max));

            const uint8x16_t low =
                RowsToMorton24A64(NarrowD24ComponentA64<0>(first_low, first_high),
                                  NarrowD24ComponentA64<0>(second_low, second_high));
            const uint8x16_t middle =
                RowsToMorton24A64(NarrowD24ComponentA64<8>(first_low, first_high),
                                  NarrowD24ComponentA64<8>(second_low, second_high));
            const uint8x16_t high =
                RowsToMorton24A64(NarrowD24ComponentA64<16>(first_low, first_high),
                                  NarrowD24ComponentA64<16>(second_low, second_high));

            StoreRGB8RowA64(tile_buffer + tile_offset, vget_low_u8(low), vget_low_u8(middle),
                            vget_low_u8(high));
            StoreRGB8RowA64(tile_buffer + tile_offset + 16 * encoded_bytes_per_pixel,
                            vget_high_u8(low), vget_high_u8(middle), vget_high_u8(high));
        }
    }
}

template <PixelFormat format>
inline uint8x8x4_t ExpandTexturePixelsA64(uint8x8_t low_component, uint8x8_t high_component) {
    const uint8x8_t zero = vdup_n_u8(0);
    const uint8x8_t opaque = vdup_n_u8(0xFF);
    uint8x8x4_t output;
    if constexpr (format == PixelFormat::IA8) {
        output.val[0] = high_component;
        output.val[1] = high_component;
        output.val[2] = high_component;
        output.val[3] = low_component;
    } else if constexpr (format == PixelFormat::RG8) {
        output.val[0] = high_component;
        output.val[1] = low_component;
        output.val[2] = zero;
        output.val[3] = opaque;
    } else if constexpr (format == PixelFormat::I8) {
        output.val[0] = low_component;
        output.val[1] = low_component;
        output.val[2] = low_component;
        output.val[3] = opaque;
    } else if constexpr (format == PixelFormat::A8) {
        output.val[0] = zero;
        output.val[1] = zero;
        output.val[2] = zero;
        output.val[3] = low_component;
    } else {
        static_assert(format == PixelFormat::IA4);
        const uint8x8_t intensity_high = vand_u8(low_component, vdup_n_u8(0xF0));
        const uint8x8_t alpha_low = vand_u8(low_component, vdup_n_u8(0x0F));
        const uint8x8_t intensity = vorr_u8(intensity_high, vshr_n_u8(intensity_high, 4));
        const uint8x8_t alpha = vorr_u8(alpha_low, vshl_n_u8(alpha_low, 4));
        output.val[0] = intensity;
        output.val[1] = intensity;
        output.val[2] = intensity;
        output.val[3] = alpha;
    }
    return output;
}

template <PixelFormat format>
inline void MortonCopyTile8To32A64(u32 stride, const u8* tile_buffer, u8* linear_buffer) {
    static_assert(format == PixelFormat::I8 || format == PixelFormat::A8 ||
                  format == PixelFormat::IA4);
    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y);
        const uint16x4_t left = vld1_u16(reinterpret_cast<const u16*>(tile_buffer + tile_offset));
        const uint16x4_t right =
            vld1_u16(reinterpret_cast<const u16*>(tile_buffer + tile_offset + 16));
        const uint16x4x2_t rows = vuzp_u16(left, right);
        u8* const first_row = linear_buffer + (7 - y) * stride * sizeof(u32);
        u8* const second_row = first_row - stride * sizeof(u32);
        const uint8x8x4_t first =
            ExpandTexturePixelsA64<format>(vreinterpret_u8_u16(rows.val[0]), {});
        const uint8x8x4_t second =
            ExpandTexturePixelsA64<format>(vreinterpret_u8_u16(rows.val[1]), {});
        StoreRGBA8RowsA64(first_row, second_row, vcombine_u8(first.val[0], second.val[0]),
                          vcombine_u8(first.val[1], second.val[1]),
                          vcombine_u8(first.val[2], second.val[2]),
                          vcombine_u8(first.val[3], second.val[3]));
    }
}

template <PixelFormat format>
inline void MortonCopyTile16To32A64(u32 stride, const u8* tile_buffer, u8* linear_buffer) {
    static_assert(format == PixelFormat::IA8 || format == PixelFormat::RG8);
    for (u32 y = 0; y < 8; y += 2) {
        const u32 tile_offset = VideoCore::MortonInterleave(0, y) * sizeof(u16);
        const uint32x2x2_t left = vld2_u32(reinterpret_cast<const u32*>(tile_buffer + tile_offset));
        const uint32x2x2_t right =
            vld2_u32(reinterpret_cast<const u32*>(tile_buffer + tile_offset + 32));
        const uint8x16_t first = vreinterpretq_u8_u32(vcombine_u32(left.val[0], right.val[0]));
        const uint8x16_t second = vreinterpretq_u8_u32(vcombine_u32(left.val[1], right.val[1]));
        const uint8x8x2_t first_components = vuzp_u8(vget_low_u8(first), vget_high_u8(first));
        const uint8x8x2_t second_components = vuzp_u8(vget_low_u8(second), vget_high_u8(second));
        u8* const first_row = linear_buffer + (7 - y) * stride * sizeof(u32);
        u8* const second_row = first_row - stride * sizeof(u32);
        const uint8x8x4_t first_pixels =
            ExpandTexturePixelsA64<format>(first_components.val[0], first_components.val[1]);
        const uint8x8x4_t second_pixels =
            ExpandTexturePixelsA64<format>(second_components.val[0], second_components.val[1]);
        StoreRGBA8RowsA64(first_row, second_row,
                          vcombine_u8(first_pixels.val[0], second_pixels.val[0]),
                          vcombine_u8(first_pixels.val[1], second_pixels.val[1]),
                          vcombine_u8(first_pixels.val[2], second_pixels.val[2]),
                          vcombine_u8(first_pixels.val[3], second_pixels.val[3]));
    }
}

template <bool decode, u32 block>
consteval std::array<u8, 16> MakeLinearRGB8ShuffleA64() {
    std::array<u8, 16> indices{};
    for (u32 lane = 0; lane < indices.size(); ++lane) {
        const u32 output = block * 16 + lane;
        if constexpr (decode) {
            const u32 component = output % 4;
            const u32 pixel = output / 4;
            // The fourth table vector is opaque alpha.
            indices[lane] = component == 3 ? 48 : static_cast<u8>(pixel * 3 + 2 - component);
        } else {
            const u32 component = output % 3;
            const u32 pixel = output / 3;
            indices[lane] = static_cast<u8>(pixel * 4 + 2 - component);
        }
    }
    return indices;
}

inline constexpr std::array<std::array<u8, 16>, 4> LINEAR_RGB8_DECODE_SHUFFLES_A64 = {
    MakeLinearRGB8ShuffleA64<true, 0>(),
    MakeLinearRGB8ShuffleA64<true, 1>(),
    MakeLinearRGB8ShuffleA64<true, 2>(),
    MakeLinearRGB8ShuffleA64<true, 3>(),
};
inline constexpr std::array<std::array<u8, 16>, 3> LINEAR_RGB8_ENCODE_SHUFFLES_A64 = {
    MakeLinearRGB8ShuffleA64<false, 0>(),
    MakeLinearRGB8ShuffleA64<false, 1>(),
    MakeLinearRGB8ShuffleA64<false, 2>(),
};

template <bool decode, PixelFormat format>
inline std::size_t LinearCopyConvertedA64(const u8* src, u8* dst, std::size_t pixel_count) {
    static_assert(format == PixelFormat::RGBA8 || format == PixelFormat::RGB8 ||
                  format == PixelFormat::RGB5A1 || format == PixelFormat::RGB565 ||
                  format == PixelFormat::RGBA4);
    std::size_t pixel = 0;

    if constexpr (format == PixelFormat::RGBA8) {
        for (; pixel + 16 <= pixel_count; pixel += 16) {
            const u8* const src_block = src + pixel * 4;
            u8* const dst_block = dst + pixel * 4;
            const uint8x16_t pixels_0 = vrev32q_u8(vld1q_u8(src_block));
            const uint8x16_t pixels_1 = vrev32q_u8(vld1q_u8(src_block + 16));
            const uint8x16_t pixels_2 = vrev32q_u8(vld1q_u8(src_block + 32));
            const uint8x16_t pixels_3 = vrev32q_u8(vld1q_u8(src_block + 48));
            vst1q_u8(dst_block, pixels_0);
            vst1q_u8(dst_block + 16, pixels_1);
            vst1q_u8(dst_block + 32, pixels_2);
            vst1q_u8(dst_block + 48, pixels_3);
        }
    } else if constexpr (format == PixelFormat::RGB8) {
        if constexpr (decode) {
            const uint8x16_t shuffle_0 = vld1q_u8(LINEAR_RGB8_DECODE_SHUFFLES_A64[0].data());
            const uint8x16_t shuffle_1 = vld1q_u8(LINEAR_RGB8_DECODE_SHUFFLES_A64[1].data());
            const uint8x16_t shuffle_2 = vld1q_u8(LINEAR_RGB8_DECODE_SHUFFLES_A64[2].data());
            const uint8x16_t shuffle_3 = vld1q_u8(LINEAR_RGB8_DECODE_SHUFFLES_A64[3].data());
            const uint8x16_t opaque = vdupq_n_u8(0xFF);
            for (; pixel + 16 <= pixel_count; pixel += 16) {
                const u8* const src_block = src + pixel * 3;
                u8* const dst_block = dst + pixel * 4;
                const uint8x16x4_t table = {vld1q_u8(src_block), vld1q_u8(src_block + 16),
                                            vld1q_u8(src_block + 32), opaque};
                vst1q_u8(dst_block, vqtbl4q_u8(table, shuffle_0));
                vst1q_u8(dst_block + 16, vqtbl4q_u8(table, shuffle_1));
                vst1q_u8(dst_block + 32, vqtbl4q_u8(table, shuffle_2));
                vst1q_u8(dst_block + 48, vqtbl4q_u8(table, shuffle_3));
            }
        } else {
            const uint8x16_t shuffle_0 = vld1q_u8(LINEAR_RGB8_ENCODE_SHUFFLES_A64[0].data());
            const uint8x16_t shuffle_1 = vld1q_u8(LINEAR_RGB8_ENCODE_SHUFFLES_A64[1].data());
            const uint8x16_t shuffle_2 = vld1q_u8(LINEAR_RGB8_ENCODE_SHUFFLES_A64[2].data());
            for (; pixel + 16 <= pixel_count; pixel += 16) {
                const u8* const src_block = src + pixel * 4;
                u8* const dst_block = dst + pixel * 3;
                const uint8x16x4_t table = {vld1q_u8(src_block), vld1q_u8(src_block + 16),
                                            vld1q_u8(src_block + 32), vld1q_u8(src_block + 48)};
                vst1q_u8(dst_block, vqtbl4q_u8(table, shuffle_0));
                vst1q_u8(dst_block + 16, vqtbl4q_u8(table, shuffle_1));
                vst1q_u8(dst_block + 32, vqtbl4q_u8(table, shuffle_2));
            }
        }
    } else if constexpr (decode) {
        for (; pixel + 16 <= pixel_count; pixel += 16) {
            const u8* const src_block = src + pixel * 2;
            u8* const dst_block = dst + pixel * 4;
            const uint8x8x4_t first =
                DecodePixels16A64<format>(vld1q_u16(reinterpret_cast<const u16*>(src_block)));
            const uint8x8x4_t second =
                DecodePixels16A64<format>(vld1q_u16(reinterpret_cast<const u16*>(src_block + 16)));
            StoreRGBA8RowsA64(dst_block, dst_block + 32, vcombine_u8(first.val[0], second.val[0]),
                              vcombine_u8(first.val[1], second.val[1]),
                              vcombine_u8(first.val[2], second.val[2]),
                              vcombine_u8(first.val[3], second.val[3]));
        }
    } else {
        for (; pixel + 16 <= pixel_count; pixel += 16) {
            const u8* const src_block = src + pixel * 4;
            u8* const dst_block = dst + pixel * 2;
            const uint16x8_t first = EncodePixels16A64<format>(vld4_u8(src_block));
            const uint16x8_t second = EncodePixels16A64<format>(vld4_u8(src_block + 32));
            vst1q_u16(reinterpret_cast<u16*>(dst_block), first);
            vst1q_u16(reinterpret_cast<u16*>(dst_block + 16), second);
        }
    }

    return pixel;
}

#endif

template <bool morton_to_linear, PixelFormat format, bool converted>
constexpr void MortonCopyTile(u32 stride, std::span<u8> tile_buffer, std::span<u8> linear_buffer) {
    constexpr u32 bytes_per_pixel = GetFormatBpp(format) / 8;
    constexpr u32 linear_bytes_per_pixel = converted ? 4 : GetFormatBytesPerPixel(format);
    constexpr bool is_compressed = format == PixelFormat::ETC1 || format == PixelFormat::ETC1A4;
    constexpr bool is_4bit = format == PixelFormat::I4 || format == PixelFormat::A4;

    if constexpr (morton_to_linear && is_compressed) {
        MortonCopyTileETC1<format>(stride, tile_buffer.data(), linear_buffer.data());
        return;
    }

#if CITRA_ARCH(arm64)
    if constexpr (morton_to_linear && !converted &&
                  (format == PixelFormat::I8 || format == PixelFormat::A8 ||
                   format == PixelFormat::IA4)) {
        MortonCopyTile8To32A64<format>(stride, tile_buffer.data(), linear_buffer.data());
        return;
    } else if constexpr (morton_to_linear && !converted &&
                         (format == PixelFormat::IA8 || format == PixelFormat::RG8)) {
        MortonCopyTile16To32A64<format>(stride, tile_buffer.data(), linear_buffer.data());
        return;
    } else if constexpr (morton_to_linear && !converted &&
                         (format == PixelFormat::I4 || format == PixelFormat::A4)) {
        MortonCopyTile4To32A64<format>(stride, tile_buffer.data(), linear_buffer.data());
        return;
    } else if constexpr (format == PixelFormat::RGBA8) {
        constexpr Pixel32TransformA64 transform =
            converted ? Pixel32TransformA64::ReverseBytes : Pixel32TransformA64::None;
        MortonCopyTile32A64<morton_to_linear, transform>(stride, tile_buffer.data(),
                                                         linear_buffer.data());
        return;
    } else if constexpr (format == PixelFormat::D24S8) {
        MortonCopyTile32A64<morton_to_linear, Pixel32TransformA64::D24S8>(
            stride, tile_buffer.data(), linear_buffer.data());
        return;
    } else if constexpr (format == PixelFormat::D24 && converted) {
        MortonCopyTileD24ConvertedA64<morton_to_linear>(stride, tile_buffer.data(),
                                                        linear_buffer.data());
        return;
    } else if constexpr (format == PixelFormat::RGB8 ||
                         (format == PixelFormat::D24 && !converted)) {
        MortonCopyTile24A64<morton_to_linear, converted>(stride, tile_buffer.data(),
                                                         linear_buffer.data());
        return;
    } else if constexpr (converted &&
                         (format == PixelFormat::RGB5A1 || format == PixelFormat::RGB565 ||
                          format == PixelFormat::RGBA4)) {
        MortonCopyTile16ConvertedA64<morton_to_linear, format>(stride, tile_buffer.data(),
                                                               linear_buffer.data());
        return;
    } else if constexpr (!converted && bytes_per_pixel == 2 && linear_bytes_per_pixel == 2) {
        MortonCopyTile16A64<morton_to_linear>(stride, tile_buffer.data(), linear_buffer.data());
        return;
    }
#endif

    for (u32 y = 0; y < 8; y++) {
        for (u32 x = 0; x < 8; x++) {
            const auto tiled_pixel = tile_buffer.subspan(
                VideoCore::MortonInterleave(x, y) * bytes_per_pixel, bytes_per_pixel);
            const auto linear_pixel = linear_buffer.subspan(
                ((7 - y) * stride + x) * linear_bytes_per_pixel, linear_bytes_per_pixel);
            if constexpr (morton_to_linear) {
                if constexpr (is_4bit) {
                    DecodePixel4<format>(x, y, tile_buffer.data(), linear_pixel.data());
                } else {
                    DecodePixel<format, converted>(tiled_pixel.data(), linear_pixel.data());
                }
            } else {
                if constexpr (is_4bit) {
                    EncodePixel4<format>(x, y, linear_pixel.data(), tile_buffer.data());
                } else {
                    EncodePixel<format, converted>(linear_pixel.data(), tiled_pixel.data());
                }
            }
        }
    }
}

/**
 * @brief Performs morton to/from linear convertions on the provided pixel data
 * @param converted If true performs RGBA8 to/from convertion to all color formats
 * @param width, height The dimentions of the rectangular region of pixels in linear_buffer
 * @param start_offset The number of bytes from the start of the first tile to the start of
 * tiled_buffer
 * @param end_offset The number of bytes from the start of the first tile to the end of tiled_buffer
 * @param linear_buffer The linear pixel data
 * @param tiled_buffer The tiled pixel data
 *
 * The MortonCopy is at the heart of the PICA texture implementation, as it's responsible for
 * converting between linear and morton tiled layouts. The function handles both convertions but
 * there are slightly different paths and inputs for each:
 *
 * Morton to Linear:
 * During uploads, tiled_buffer is always aligned to the tile or scanline boundary depending if the
 * linear rectangle spans multiple vertical tiles. linear_buffer does not reference the entire
 * texture area, but rather the specific rectangle affected by the upload.
 *
 * Linear to Morton:
 * This is similar to the other convertion but with some differences. In this case tiled_buffer is
 * not required to be aligned to any specific boundary which requires special care.
 * start_offset/end_offset are useful here as they tell us exactly where the data should be placed
 * in the linear_buffer.
 */
template <bool morton_to_linear, PixelFormat format, bool converted = false>
static constexpr void MortonCopy(u32 width, u32 height, u32 start_offset, u32 end_offset,
                                 std::span<u8> linear_buffer, std::span<u8> tiled_buffer) {
    constexpr u32 bytes_per_pixel = GetFormatBpp(format) / 8;
    constexpr u32 aligned_bytes_per_pixel = converted ? 4 : GetFormatBytesPerPixel(format);
    constexpr u32 tile_size = GetFormatBpp(format) * 64 / 8;
    static_assert(aligned_bytes_per_pixel >= bytes_per_pixel, "");

    const u32 linear_tile_stride = (7 * width + 8) * aligned_bytes_per_pixel;
    const u32 aligned_down_start_offset = Common::AlignDown(start_offset, tile_size);
    const u32 aligned_start_offset = Common::AlignUp(start_offset, tile_size);
    const u32 aligned_end_offset = Common::AlignDown(end_offset, tile_size);
    const u32 begin_pixel_index = aligned_down_start_offset * 8 / GetFormatBpp(format);

    ASSERT(!morton_to_linear ||
           (aligned_start_offset == start_offset && aligned_end_offset == end_offset));

    // In OpenGL the texture origin is in the bottom left corner as opposed to other
    // APIs that have it at the top left. To avoid flipping texture coordinates in
    // the shader we read/write the linear buffer from the bottom up
    u32 x = (begin_pixel_index % (width * 8)) / 8;
    u32 y = (begin_pixel_index / (width * 8)) * 8;
    u32 linear_offset = ((height - 8 - y) * width + x) * aligned_bytes_per_pixel;
    u32 tiled_offset = 0;

    const auto linear_next_tile = [&] {
        x = (x + 8) % width;
        linear_offset += 8 * aligned_bytes_per_pixel;
        if (!x) {
            y = (y + 8) % height;
            if (!y) {
                return;
            }

            linear_offset -= width * 9 * aligned_bytes_per_pixel;
        }
    };

    // If during a texture download the start coordinate is not tile aligned, swizzle
    // the tile affected to a temporary buffer and copy the part we are interested in
    if (start_offset < aligned_start_offset && !morton_to_linear) {
        std::array<u8, tile_size> tmp_buf;
        auto linear_data = linear_buffer.subspan(linear_offset, linear_tile_stride);
        MortonCopyTile<morton_to_linear, format, converted>(width, tmp_buf, linear_data);

        std::memcpy(tiled_buffer.data(), tmp_buf.data() + start_offset - aligned_down_start_offset,
                    std::min(aligned_start_offset, end_offset) - start_offset);

        tiled_offset += aligned_start_offset - start_offset;
        linear_next_tile();
    }

    // If the copy spans multiple tiles, copy the fully aligned tiles in between.
    if (aligned_start_offset < aligned_end_offset) {
        const u32 tile_buffer_size = static_cast<u32>(tiled_buffer.size());
        const u32 buffer_end =
            std::min(tiled_offset + aligned_end_offset - aligned_start_offset, tile_buffer_size);
        while (tiled_offset < buffer_end) {
            auto linear_data = linear_buffer.subspan(linear_offset, linear_tile_stride);
            auto tiled_data = tiled_buffer.subspan(tiled_offset, tile_size);
            MortonCopyTile<morton_to_linear, format, converted>(width, tiled_data, linear_data);
            tiled_offset += tile_size;
            linear_next_tile();
        }
    }

    // If during a texture download the end coordinate is not tile aligned, swizzle
    // the tile affected to a temporary buffer and copy the part we are interested in
    if (end_offset > std::max(aligned_start_offset, aligned_end_offset) && !morton_to_linear) {
        std::array<u8, tile_size> tmp_buf;
        auto linear_data = linear_buffer.subspan(linear_offset, linear_tile_stride);
        MortonCopyTile<morton_to_linear, format, converted>(width, tmp_buf, linear_data);
        std::memcpy(tiled_buffer.data() + tiled_offset, tmp_buf.data(),
                    end_offset - aligned_end_offset);
    }
}

/**
 * Performs a linear copy, converting pixel formats if required.
 * @tparam decode If true, decodes the texture if needed. Otherwise, encodes if needed.
 * @tparam format Pixel format to copy.
 * @tparam converted If true, converts the texture to/from the appropriate format.
 * @param src_buffer The source pixel data
 * @param dst_buffer The destination pixel data
 * @return
 */
template <bool decode, PixelFormat format, bool converted = false>
static constexpr void LinearCopy(std::span<u8> src_buffer, std::span<u8> dst_buffer) {
    std::size_t src_size = src_buffer.size();
    std::size_t dst_size = dst_buffer.size();

    if constexpr (converted) {
        constexpr u32 encoded_bytes_per_pixel = GetFormatBpp(format) / 8;
        constexpr u32 decoded_bytes_per_pixel = 4;
        constexpr u32 src_bytes_per_pixel =
            decode ? encoded_bytes_per_pixel : decoded_bytes_per_pixel;
        constexpr u32 dst_bytes_per_pixel =
            decode ? decoded_bytes_per_pixel : encoded_bytes_per_pixel;

        src_size = Common::AlignDown(src_size, src_bytes_per_pixel);
        dst_size = Common::AlignDown(dst_size, dst_bytes_per_pixel);

        std::size_t first_scalar_pixel = 0;
#if CITRA_ARCH(arm64)
        if constexpr (format == PixelFormat::RGBA8 || format == PixelFormat::RGB8 ||
                      format == PixelFormat::RGB5A1 || format == PixelFormat::RGB565 ||
                      format == PixelFormat::RGBA4) {
            const std::size_t pixel_count =
                std::min(src_size / src_bytes_per_pixel, dst_size / dst_bytes_per_pixel);
            first_scalar_pixel = LinearCopyConvertedA64<decode, format>(
                src_buffer.data(), dst_buffer.data(), pixel_count);
        }
#endif

#if CITRA_ARCH(arm64) && defined(__clang__)
#pragma clang loop vectorize(disable)
#endif
        for (std::size_t src_index = first_scalar_pixel * src_bytes_per_pixel,
                         dst_index = first_scalar_pixel * dst_bytes_per_pixel;
             src_index < src_size && dst_index < dst_size;
             src_index += src_bytes_per_pixel, dst_index += dst_bytes_per_pixel) {
            const auto src_pixel = src_buffer.subspan(src_index, src_bytes_per_pixel);
            const auto dst_pixel = dst_buffer.subspan(dst_index, dst_bytes_per_pixel);
            if constexpr (decode) {
                DecodePixel<format, converted>(src_pixel.data(), dst_pixel.data());
            } else {
                EncodePixel<format, converted>(src_pixel.data(), dst_pixel.data());
            }
        }
    } else {
        std::memcpy(dst_buffer.data(), src_buffer.data(), std::min(src_size, dst_size));
    }
}

using MortonFunc = void (*)(u32, u32, u32, u32, std::span<u8>, std::span<u8>);

static constexpr std::array<MortonFunc, 18> UNSWIZZLE_TABLE = {
    MortonCopy<true, PixelFormat::RGBA8>,  // 0
    MortonCopy<true, PixelFormat::RGB8>,   // 1
    MortonCopy<true, PixelFormat::RGB5A1>, // 2
    MortonCopy<true, PixelFormat::RGB565>, // 3
    MortonCopy<true, PixelFormat::RGBA4>,  // 4
    MortonCopy<true, PixelFormat::IA8>,    // 5
    MortonCopy<true, PixelFormat::RG8>,    // 6
    MortonCopy<true, PixelFormat::I8>,     // 7
    MortonCopy<true, PixelFormat::A8>,     // 8
    MortonCopy<true, PixelFormat::IA4>,    // 9
    MortonCopy<true, PixelFormat::I4>,     // 10
    MortonCopy<true, PixelFormat::A4>,     // 11
    MortonCopy<true, PixelFormat::ETC1>,   // 12
    MortonCopy<true, PixelFormat::ETC1A4>, // 13
    MortonCopy<true, PixelFormat::D16>,    // 14
    nullptr,                               // 15
    MortonCopy<true, PixelFormat::D24>,    // 16
    MortonCopy<true, PixelFormat::D24S8>,  // 17
};

static constexpr std::array<MortonFunc, 18> UNSWIZZLE_TABLE_CONVERTED = {
    MortonCopy<true, PixelFormat::RGBA8, true>,  // 0
    MortonCopy<true, PixelFormat::RGB8, true>,   // 1
    MortonCopy<true, PixelFormat::RGB5A1, true>, // 2
    MortonCopy<true, PixelFormat::RGB565, true>, // 3
    MortonCopy<true, PixelFormat::RGBA4, true>,  // 4
    // The following formats are implicitly converted to RGBA regardless, so ignore them.
    nullptr,                                  // 5
    nullptr,                                  // 6
    nullptr,                                  // 7
    nullptr,                                  // 8
    nullptr,                                  // 9
    nullptr,                                  // 10
    nullptr,                                  // 11
    nullptr,                                  // 12
    nullptr,                                  // 13
    MortonCopy<true, PixelFormat::D16, true>, // 14
    nullptr,                                  // 15
    MortonCopy<true, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special deinterleaving conversion elsewhere.
    nullptr, // 17
};

static constexpr std::array<MortonFunc, 18> SWIZZLE_TABLE = {
    MortonCopy<false, PixelFormat::RGBA8>,  // 0
    MortonCopy<false, PixelFormat::RGB8>,   // 1
    MortonCopy<false, PixelFormat::RGB5A1>, // 2
    MortonCopy<false, PixelFormat::RGB565>, // 3
    MortonCopy<false, PixelFormat::RGBA4>,  // 4
    MortonCopy<false, PixelFormat::IA8>,    // 5
    MortonCopy<false, PixelFormat::RG8>,    // 6
    MortonCopy<false, PixelFormat::I8>,     // 7
    MortonCopy<false, PixelFormat::A8>,     // 8
    MortonCopy<false, PixelFormat::IA4>,    // 9
    MortonCopy<false, PixelFormat::I4>,     // 10
    MortonCopy<false, PixelFormat::A4>,     // 11
    nullptr,                                // 12
    nullptr,                                // 13
    MortonCopy<false, PixelFormat::D16>,    // 14
    nullptr,                                // 15
    MortonCopy<false, PixelFormat::D24>,    // 16
    MortonCopy<false, PixelFormat::D24S8>,  // 17
};

static constexpr std::array<MortonFunc, 18> SWIZZLE_TABLE_CONVERTED = {
    MortonCopy<false, PixelFormat::RGBA8, true>,  // 0
    MortonCopy<false, PixelFormat::RGB8, true>,   // 1
    MortonCopy<false, PixelFormat::RGB5A1, true>, // 2
    MortonCopy<false, PixelFormat::RGB565, true>, // 3
    MortonCopy<false, PixelFormat::RGBA4, true>,  // 4
    // The following formats are implicitly converted from RGBA regardless, so ignore them.
    nullptr,                                   // 5
    nullptr,                                   // 6
    nullptr,                                   // 7
    nullptr,                                   // 8
    nullptr,                                   // 9
    nullptr,                                   // 10
    nullptr,                                   // 11
    nullptr,                                   // 12
    nullptr,                                   // 13
    MortonCopy<false, PixelFormat::D16, true>, // 14
    nullptr,                                   // 15
    MortonCopy<false, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special interleaving conversion elsewhere.
    nullptr, // 17
};

using LinearFunc = void (*)(std::span<u8>, std::span<u8>);

static constexpr std::array<LinearFunc, 18> LINEAR_DECODE_TABLE = {
    LinearCopy<true, PixelFormat::RGBA8>,  // 0
    LinearCopy<true, PixelFormat::RGB8>,   // 1
    LinearCopy<true, PixelFormat::RGB5A1>, // 2
    LinearCopy<true, PixelFormat::RGB565>, // 3
    LinearCopy<true, PixelFormat::RGBA4>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                              // 5
    nullptr,                              // 6
    nullptr,                              // 7
    nullptr,                              // 8
    nullptr,                              // 9
    nullptr,                              // 10
    nullptr,                              // 11
    nullptr,                              // 12
    nullptr,                              // 13
    LinearCopy<true, PixelFormat::D16>,   // 14
    nullptr,                              // 15
    LinearCopy<true, PixelFormat::D24>,   // 16
    LinearCopy<true, PixelFormat::D24S8>, // 17
};

static constexpr std::array<LinearFunc, 18> LINEAR_DECODE_TABLE_CONVERTED = {
    LinearCopy<true, PixelFormat::RGBA8, true>,  // 0
    LinearCopy<true, PixelFormat::RGB8, true>,   // 1
    LinearCopy<true, PixelFormat::RGB5A1, true>, // 2
    LinearCopy<true, PixelFormat::RGB565, true>, // 3
    LinearCopy<true, PixelFormat::RGBA4, true>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                                  // 5
    nullptr,                                  // 6
    nullptr,                                  // 7
    nullptr,                                  // 8
    nullptr,                                  // 9
    nullptr,                                  // 10
    nullptr,                                  // 11
    nullptr,                                  // 12
    nullptr,                                  // 13
    LinearCopy<true, PixelFormat::D16, true>, // 14
    nullptr,                                  // 15
    LinearCopy<true, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special deinterleaving conversion elsewhere.
    nullptr, // 17
};

static constexpr std::array<LinearFunc, 18> LINEAR_ENCODE_TABLE = {
    LinearCopy<false, PixelFormat::RGBA8>,  // 0
    LinearCopy<false, PixelFormat::RGB8>,   // 1
    LinearCopy<false, PixelFormat::RGB5A1>, // 2
    LinearCopy<false, PixelFormat::RGB565>, // 3
    LinearCopy<false, PixelFormat::RGBA4>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                               // 5
    nullptr,                               // 6
    nullptr,                               // 7
    nullptr,                               // 8
    nullptr,                               // 9
    nullptr,                               // 10
    nullptr,                               // 11
    nullptr,                               // 12
    nullptr,                               // 13
    LinearCopy<false, PixelFormat::D16>,   // 14
    nullptr,                               // 15
    LinearCopy<false, PixelFormat::D24>,   // 16
    LinearCopy<false, PixelFormat::D24S8>, // 17
};

static constexpr std::array<LinearFunc, 18> LINEAR_ENCODE_TABLE_CONVERTED = {
    LinearCopy<false, PixelFormat::RGBA8, true>,  // 0
    LinearCopy<false, PixelFormat::RGB8, true>,   // 1
    LinearCopy<false, PixelFormat::RGB5A1, true>, // 2
    LinearCopy<false, PixelFormat::RGB565, true>, // 3
    LinearCopy<false, PixelFormat::RGBA4, true>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                                   // 5
    nullptr,                                   // 6
    nullptr,                                   // 7
    nullptr,                                   // 8
    nullptr,                                   // 9
    nullptr,                                   // 10
    nullptr,                                   // 11
    nullptr,                                   // 12
    nullptr,                                   // 13
    LinearCopy<false, PixelFormat::D16, true>, // 14
    nullptr,                                   // 15
    LinearCopy<false, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special interleaving conversion elsewhere.
    nullptr, // 17
};

} // namespace VideoCore
