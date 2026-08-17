// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "common/bit_field.h"
#include "common/color.h"
#include "common/common_types.h"
#include "common/vector_math.h"
#include "video_core/texture/etc1.h"

namespace Pica::Texture {

namespace {

constexpr std::array<std::array<u8, 2>, 8> etc1_modifier_table = {{
    {2, 8},
    {5, 17},
    {9, 29},
    {13, 42},
    {18, 60},
    {24, 80},
    {33, 106},
    {47, 183},
}};

#if defined(__aarch64__)

// ETC1 stores selector bits down columns, while the decoded image is written across rows. These
// signed shifts gather two rows of four pixels into consecutive AdvSIMD lanes.
constexpr std::array<s16, 8> etc1_selector_shifts_01 = {0, -4, -8, -12, -1, -5, -9, -13};
constexpr std::array<s16, 8> etc1_selector_shifts_23 = {-2, -6, -10, -14, -3, -7, -11, -15};
constexpr std::array<u16, 8> etc1_horizontal_subblocks = {0, 0, 0xFFFF, 0xFFFF,
                                                          0, 0, 0xFFFF, 0xFFFF};
constexpr std::array<u8, 16> etc1_column_to_row = {0, 4, 8,  12, 1, 5, 9,  13,
                                                   2, 6, 10, 14, 3, 7, 11, 15};

void StoreETC1RowPair(u8* first_row, std::ptrdiff_t output_stride, uint8x8_t red, uint8x8_t green,
                      uint8x8_t blue, uint8x8_t alpha) {
    const uint8x8x2_t red_green = vzip_u8(red, green);
    const uint8x8x2_t blue_alpha = vzip_u8(blue, alpha);

    const uint16x4x2_t first =
        vzip_u16(vreinterpret_u16_u8(red_green.val[0]), vreinterpret_u16_u8(blue_alpha.val[0]));
    const uint16x4x2_t second =
        vzip_u16(vreinterpret_u16_u8(red_green.val[1]), vreinterpret_u16_u8(blue_alpha.val[1]));

    vst1q_u8(first_row,
             vcombine_u8(vreinterpret_u8_u16(first.val[0]), vreinterpret_u8_u16(first.val[1])));
    vst1q_u8(first_row + output_stride,
             vcombine_u8(vreinterpret_u8_u16(second.val[0]), vreinterpret_u8_u16(second.val[1])));
}

uint8x16_t ExpandETC1Alpha(u64 alpha) {
    const uint8x8_t packed = vreinterpret_u8_u64(vcreate_u64(alpha));
    uint8x8_t low = vand_u8(packed, vdup_n_u8(0x0F));
    uint8x8_t high = vshr_n_u8(packed, 4);
    const uint8x8x2_t nibbles = vzip_u8(low, high);
    const uint8x16_t column_major = vcombine_u8(nibbles.val[0], nibbles.val[1]);
    uint8x16_t row_major = vqtbl1q_u8(column_major, vld1q_u8(etc1_column_to_row.data()));
    row_major = vsliq_n_u8(row_major, row_major, 4);
    return row_major;
}

#endif

union ETC1Tile {
    u64 raw;

    // Each of these two is a collection of 16 bits (one per lookup value)
    BitField<0, 16, u64> table_subindexes;
    BitField<16, 16, u64> negation_flags;

    unsigned GetTableSubIndex(unsigned index) const {
        return (table_subindexes >> index) & 1;
    }

    bool GetNegationFlag(unsigned index) const {
        return ((negation_flags >> index) & 1) == 1;
    }

    BitField<32, 1, u64> flip;
    BitField<33, 1, u64> differential_mode;

    BitField<34, 3, u64> table_index_2;
    BitField<37, 3, u64> table_index_1;

    union {
        // delta value + base value
        BitField<40, 3, s64> db;
        BitField<43, 5, u64> b;

        BitField<48, 3, s64> dg;
        BitField<51, 5, u64> g;

        BitField<56, 3, s64> dr;
        BitField<59, 5, u64> r;
    } differential;

    union {
        BitField<40, 4, u64> b2;
        BitField<44, 4, u64> b1;

        BitField<48, 4, u64> g2;
        BitField<52, 4, u64> g1;

        BitField<56, 4, u64> r2;
        BitField<60, 4, u64> r1;
    } separate;

    const Common::Vec3<u8> GetRGB(unsigned int x, unsigned int y) const {
        int texel = 4 * x + y;

        if (flip)
            std::swap(x, y);

        // Lookup base value
        Common::Vec3<int> ret;
        if (differential_mode) {
            ret.r() = static_cast<int>(differential.r);
            ret.g() = static_cast<int>(differential.g);
            ret.b() = static_cast<int>(differential.b);
            if (x >= 2) {
                ret.r() += static_cast<int>(differential.dr);
                ret.g() += static_cast<int>(differential.dg);
                ret.b() += static_cast<int>(differential.db);
            }
            ret.r() = Common::Color::Convert5To8(ret.r());
            ret.g() = Common::Color::Convert5To8(ret.g());
            ret.b() = Common::Color::Convert5To8(ret.b());
        } else {
            if (x < 2) {
                ret.r() = Common::Color::Convert4To8(static_cast<u8>(separate.r1));
                ret.g() = Common::Color::Convert4To8(static_cast<u8>(separate.g1));
                ret.b() = Common::Color::Convert4To8(static_cast<u8>(separate.b1));
            } else {
                ret.r() = Common::Color::Convert4To8(static_cast<u8>(separate.r2));
                ret.g() = Common::Color::Convert4To8(static_cast<u8>(separate.g2));
                ret.b() = Common::Color::Convert4To8(static_cast<u8>(separate.b2));
            }
        }

        // Add modifier
        unsigned table_index =
            static_cast<int>((x < 2) ? table_index_1.Value() : table_index_2.Value());

        int modifier = etc1_modifier_table[table_index][GetTableSubIndex(texel)];
        if (GetNegationFlag(texel))
            modifier *= -1;

        ret.r() = std::clamp(ret.r() + modifier, 0, 255);
        ret.g() = std::clamp(ret.g() + modifier, 0, 255);
        ret.b() = std::clamp(ret.b() + modifier, 0, 255);

        return ret.Cast<u8>();
    }
};

class ETC1Decoder {
public:
    explicit ETC1Decoder(u64 value) : tile{value}, flip{tile.flip != 0} {
        table_indices[0] = static_cast<u8>(tile.table_index_1.Value());
        table_indices[1] = static_cast<u8>(tile.table_index_2.Value());

        if (tile.differential_mode) {
            const int red = static_cast<int>(tile.differential.r);
            const int green = static_cast<int>(tile.differential.g);
            const int blue = static_cast<int>(tile.differential.b);
            base_colors[0] = ConvertDifferential(red, green, blue);
            base_colors[1] = ConvertDifferential(red + static_cast<int>(tile.differential.dr),
                                                 green + static_cast<int>(tile.differential.dg),
                                                 blue + static_cast<int>(tile.differential.db));
        } else {
            base_colors[0] = {
                Common::Color::Convert4To8(static_cast<u8>(tile.separate.r1)),
                Common::Color::Convert4To8(static_cast<u8>(tile.separate.g1)),
                Common::Color::Convert4To8(static_cast<u8>(tile.separate.b1)),
            };
            base_colors[1] = {
                Common::Color::Convert4To8(static_cast<u8>(tile.separate.r2)),
                Common::Color::Convert4To8(static_cast<u8>(tile.separate.g2)),
                Common::Color::Convert4To8(static_cast<u8>(tile.separate.b2)),
            };
        }
    }

    Common::Vec3<u8> GetRGB(unsigned int x, unsigned int y) const {
        const unsigned int texel = 4 * x + y;
        const unsigned int subblock = flip ? y / 2 : x / 2;
        Common::Vec3<int> color = base_colors[subblock];

        int modifier = etc1_modifier_table[table_indices[subblock]][tile.GetTableSubIndex(texel)];
        if (tile.GetNegationFlag(texel)) {
            modifier = -modifier;
        }

        color.r() = std::clamp(color.r() + modifier, 0, 255);
        color.g() = std::clamp(color.g() + modifier, 0, 255);
        color.b() = std::clamp(color.b() + modifier, 0, 255);
        return color.Cast<u8>();
    }

#if defined(__aarch64__)
    template <bool has_alpha>
    void Decode(u64 alpha, u8* output, std::ptrdiff_t output_stride) const {
        const uint16x8_t selector_bits =
            vdupq_n_u16(static_cast<u16>(tile.table_subindexes.Value()));
        const uint16x8_t negation_bits = vdupq_n_u16(static_cast<u16>(tile.negation_flags.Value()));
        const uint16x8_t one = vdupq_n_u16(1);
        const uint16x8_t horizontal_subblocks = vld1q_u16(etc1_horizontal_subblocks.data());
        const uint16x8_t flip_mask = vdupq_n_u16(static_cast<u16>(-static_cast<s16>(flip)));
        const uint16x8_t first_subblocks = vbicq_u16(horizontal_subblocks, flip_mask);
        const uint16x8_t second_subblocks = vorrq_u16(horizontal_subblocks, flip_mask);

        const int16x8_t red_0 = vdupq_n_s16(static_cast<s16>(base_colors[0].r()));
        const int16x8_t green_0 = vdupq_n_s16(static_cast<s16>(base_colors[0].g()));
        const int16x8_t blue_0 = vdupq_n_s16(static_cast<s16>(base_colors[0].b()));
        const int16x8_t red_1 = vdupq_n_s16(static_cast<s16>(base_colors[1].r()));
        const int16x8_t green_1 = vdupq_n_s16(static_cast<s16>(base_colors[1].g()));
        const int16x8_t blue_1 = vdupq_n_s16(static_cast<s16>(base_colors[1].b()));

        const auto& modifier_0 = etc1_modifier_table[table_indices[0]];
        const auto& modifier_1 = etc1_modifier_table[table_indices[1]];
        const int16x8_t modifier_0_low = vdupq_n_s16(modifier_0[0]);
        const int16x8_t modifier_0_high = vdupq_n_s16(modifier_0[1]);
        const int16x8_t modifier_1_low = vdupq_n_s16(modifier_1[0]);
        const int16x8_t modifier_1_high = vdupq_n_s16(modifier_1[1]);

        uint8x16_t alpha_pixels;
        if constexpr (has_alpha) {
            alpha_pixels = ExpandETC1Alpha(alpha);
        } else {
            alpha_pixels = vdupq_n_u8(0xFF);
        }

        const auto decode_band = [&]<u32 Band>() {
            static_assert(Band < 2);
            constexpr const auto& shift_values =
                Band == 0 ? etc1_selector_shifts_01 : etc1_selector_shifts_23;
            const int16x8_t shifts = vld1q_s16(shift_values.data());
            const uint16x8_t selector_mask =
                vceqq_u16(vandq_u16(vshlq_u16(selector_bits, shifts), one), one);
            const uint16x8_t negation_mask =
                vceqq_u16(vandq_u16(vshlq_u16(negation_bits, shifts), one), one);
            const uint16x8_t subblock_mask = Band == 0 ? first_subblocks : second_subblocks;

            const int16x8_t modifier_low = vbslq_s16(subblock_mask, modifier_1_low, modifier_0_low);
            const int16x8_t modifier_high =
                vbslq_s16(subblock_mask, modifier_1_high, modifier_0_high);
            int16x8_t modifier = vbslq_s16(selector_mask, modifier_high, modifier_low);
            modifier = vbslq_s16(negation_mask, vnegq_s16(modifier), modifier);

            const int16x8_t red_base = vbslq_s16(subblock_mask, red_1, red_0);
            const int16x8_t green_base = vbslq_s16(subblock_mask, green_1, green_0);
            const int16x8_t blue_base = vbslq_s16(subblock_mask, blue_1, blue_0);
            const uint8x8_t red = vqmovun_s16(vaddq_s16(red_base, modifier));
            const uint8x8_t green = vqmovun_s16(vaddq_s16(green_base, modifier));
            const uint8x8_t blue = vqmovun_s16(vaddq_s16(blue_base, modifier));
            const uint8x8_t alpha_band =
                Band == 0 ? vget_low_u8(alpha_pixels) : vget_high_u8(alpha_pixels);

            StoreETC1RowPair(output + Band * 2 * output_stride, output_stride, red, green, blue,
                             alpha_band);
        };
        decode_band.template operator()<0>();
        decode_band.template operator()<1>();
    }
#endif

private:
    static Common::Vec3<int> ConvertDifferential(int red, int green, int blue) {
        return {
            Common::Color::Convert5To8(static_cast<u8>(red)),
            Common::Color::Convert5To8(static_cast<u8>(green)),
            Common::Color::Convert5To8(static_cast<u8>(blue)),
        };
    }

    ETC1Tile tile;
    std::array<Common::Vec3<int>, 2> base_colors{};
    std::array<u8, 2> table_indices{};
    bool flip{};
};

template <bool has_alpha>
void DecodeETC1SubtileImpl(u64 value, u64 alpha, u8* output, std::ptrdiff_t output_stride) {
    const ETC1Decoder decoder{value};
#if defined(__aarch64__)
    decoder.Decode<has_alpha>(alpha, output, output_stride);
#else
    u8* row = output;
    for (unsigned int y = 0; y < 4; ++y) {
        for (unsigned int x = 0; x < 4; ++x) {
            const Common::Vec3<u8> rgb = decoder.GetRGB(x, y);
            u8* const pixel = row + x * 4;
            pixel[0] = rgb.r();
            pixel[1] = rgb.g();
            pixel[2] = rgb.b();
            if constexpr (has_alpha) {
                const u8 alpha4 = static_cast<u8>((alpha >> (4 * (x * 4 + y))) & 0xF);
                pixel[3] = Common::Color::Convert4To8(alpha4);
            } else {
                pixel[3] = 0xFF;
            }
        }
        row += output_stride;
    }
#endif
}

} // anonymous namespace

Common::Vec3<u8> SampleETC1Subtile(u64 value, unsigned int x, unsigned int y) {
    ETC1Tile tile{value};
    return tile.GetRGB(x, y);
}

void DecodeETC1Subtile(u64 value, u8* output, std::ptrdiff_t output_stride) {
    DecodeETC1SubtileImpl<false>(value, 0, output, output_stride);
}

void DecodeETC1A4Subtile(u64 value, u64 alpha, u8* output, std::ptrdiff_t output_stride) {
    DecodeETC1SubtileImpl<true>(value, alpha, output, output_stride);
}

} // namespace Pica::Texture
