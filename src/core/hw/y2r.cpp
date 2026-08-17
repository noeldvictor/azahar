// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include "common/arch.h"
#include "common/assert.h"
#include "common/color.h"
#include "common/common_types.h"
#include "common/microprofile.h"
#include "common/vector_math.h"
#include "core/core.h"
#include "core/hle/service/cam/y2r_u.h"
#include "core/hw/y2r.h"
#include "core/hw/y2r_testing.h"
#include "core/memory.h"

#if CITRA_ARCH(arm64)
#include <arm_neon.h>
#endif

namespace HW::Y2R {

using namespace Service::Y2R;

static const std::size_t MAX_TILES = 1024 / 8;
static const std::size_t TILE_SIZE = 8 * 8;
using ImageTile = Testing::ImageTile;

/// Converts a image strip from the source YUV format into individual 8x8 RGB32 tiles.
template <InputFormat input_format>
static void ConvertYUVToRGBScalar(const u8* input_Y, const u8* input_U, const u8* input_V,
                                  ImageTile output[], unsigned int width, unsigned int height,
                                  const CoefficientSet& coefficients) {

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            s32 Y;
            s32 U;
            s32 V;
            if constexpr (input_format == InputFormat::YUV422_Indiv8 ||
                          input_format == InputFormat::YUV422_Indiv16) {
                Y = input_Y[y * width + x];
                U = input_U[(y * width + x) / 2];
                V = input_V[(y * width + x) / 2];
            } else if constexpr (input_format == InputFormat::YUV420_Indiv8 ||
                                 input_format == InputFormat::YUV420_Indiv16) {
                Y = input_Y[y * width + x];
                U = input_U[((y / 2) * width + x) / 2];
                V = input_V[((y / 2) * width + x) / 2];
            } else if constexpr (input_format == InputFormat::YUYV422_Interleaved) {
                Y = input_Y[(y * width + x) * 2];
                U = input_Y[(y * width + (x / 2) * 2) * 2 + 1];
                V = input_Y[(y * width + (x / 2) * 2) * 2 + 3];
            } else {
                UNREACHABLE_MSG("Unknown Y2R input format {}", input_format);
                return;
            }

            // This conversion process is bit-exact with hardware, as far as could be tested.
            auto& c = coefficients;
            s32 cY = c[0] * Y;

            s32 r = cY + c[1] * V;
            s32 g = cY - c[2] * V - c[3] * U;
            s32 b = cY + c[4] * U;

            const s32 rounding_offset = 0x18;
            r = (r >> 3) + c[5] + rounding_offset;
            g = (g >> 3) + c[6] + rounding_offset;
            b = (b >> 3) + c[7] + rounding_offset;

            unsigned int tile = x / 8;
            unsigned int tile_x = x % 8;
            u32* out = &output[tile][y * 8 + tile_x];
            *out = ((u32)std::clamp(r >> 5, 0, 0xFF) << 24) |
                   ((u32)std::clamp(g >> 5, 0, 0xFF) << 16) |
                   ((u32)std::clamp(b >> 5, 0, 0xFF) << 8);
        }
    }
}

#if CITRA_ARCH(arm64)

static uint8x8_t DuplicateLowFourBytes(uint8x8_t values) {
    return vzip_u8(values, values).val[0];
}

static uint8x8_t LoadAndDuplicateChroma(const u8* input) {
    u32 packed;
    std::memcpy(&packed, input, sizeof(packed));
    return DuplicateLowFourBytes(vreinterpret_u8_u32(vdup_n_u32(packed)));
}

static uint8x8_t NarrowYUVChannel(int32x4_t low, int32x4_t high, s16 offset) {
    const int32x4_t rounding = vdupq_n_s32(static_cast<s32>(offset) + 0x18);
    low = vaddq_s32(vshrq_n_s32(low, 3), rounding);
    high = vaddq_s32(vshrq_n_s32(high, 3), rounding);

    const uint16x4_t low_u16 = vqmovun_s32(vshrq_n_s32(low, 5));
    const uint16x4_t high_u16 = vqmovun_s32(vshrq_n_s32(high, 5));
    return vqmovn_u16(vcombine_u16(low_u16, high_u16));
}

static void ConvertYUVBandA64(uint8x8_t y_bytes, uint8x8_t u_bytes, uint8x8_t v_bytes, u32* output,
                              const CoefficientSet& coefficients) {
    const int16x8_t y = vreinterpretq_s16_u16(vmovl_u8(y_bytes));
    const int16x8_t u = vreinterpretq_s16_u16(vmovl_u8(u_bytes));
    const int16x8_t v = vreinterpretq_s16_u16(vmovl_u8(v_bytes));

    const int16x4_t y_low = vget_low_s16(y);
    const int16x4_t y_high = vget_high_s16(y);
    const int16x4_t u_low = vget_low_s16(u);
    const int16x4_t u_high = vget_high_s16(u);
    const int16x4_t v_low = vget_low_s16(v);
    const int16x4_t v_high = vget_high_s16(v);

    const int32x4_t cy_low = vmull_n_s16(y_low, coefficients[0]);
    const int32x4_t cy_high = vmull_n_s16(y_high, coefficients[0]);

    const int32x4_t red_low = vmlal_n_s16(cy_low, v_low, coefficients[1]);
    const int32x4_t red_high = vmlal_n_s16(cy_high, v_high, coefficients[1]);

    int32x4_t green_low = vmlsl_n_s16(cy_low, v_low, coefficients[2]);
    int32x4_t green_high = vmlsl_n_s16(cy_high, v_high, coefficients[2]);
    green_low = vmlsl_n_s16(green_low, u_low, coefficients[3]);
    green_high = vmlsl_n_s16(green_high, u_high, coefficients[3]);

    const int32x4_t blue_low = vmlal_n_s16(cy_low, u_low, coefficients[4]);
    const int32x4_t blue_high = vmlal_n_s16(cy_high, u_high, coefficients[4]);

    const uint8x8_t red = NarrowYUVChannel(red_low, red_high, coefficients[5]);
    const uint8x8_t green = NarrowYUVChannel(green_low, green_high, coefficients[6]);
    const uint8x8_t blue = NarrowYUVChannel(blue_low, blue_high, coefficients[7]);

    // ImageTile stores numeric 0xRRGGBB00 words. Arrange their little-endian bytes as
    // [0, B, G, R] with ZIPs and contiguous vector stores, avoiding D-form byte ST4 on A510.
    const uint8x8_t zero = vdup_n_u8(0);
    const uint8x8x2_t zero_blue = vzip_u8(zero, blue);
    const uint8x8x2_t green_red = vzip_u8(green, red);
    const uint8x16_t zero_blue_q = vcombine_u8(zero_blue.val[0], zero_blue.val[1]);
    const uint8x16_t green_red_q = vcombine_u8(green_red.val[0], green_red.val[1]);
    const uint16x8x2_t pixels =
        vzipq_u16(vreinterpretq_u16_u8(zero_blue_q), vreinterpretq_u16_u8(green_red_q));
    vst1q_u32(output, vreinterpretq_u32_u16(pixels.val[0]));
    vst1q_u32(output + 4, vreinterpretq_u32_u16(pixels.val[1]));
}

template <InputFormat input_format>
static void ConvertYUVToRGBOnAArch64(const u8* input_Y, const u8* input_U, const u8* input_V,
                                     ImageTile output[], unsigned int width, unsigned int height,
                                     const CoefficientSet& coefficients) {
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; x += 8) {
            uint8x8_t y_bytes;
            uint8x8_t u_bytes;
            uint8x8_t v_bytes;
            if constexpr (input_format == InputFormat::YUV422_Indiv8 ||
                          input_format == InputFormat::YUV422_Indiv16) {
                y_bytes = vld1_u8(input_Y + y * width + x);
                const std::size_t chroma_offset = y * (width / 2) + x / 2;
                u_bytes = LoadAndDuplicateChroma(input_U + chroma_offset);
                v_bytes = LoadAndDuplicateChroma(input_V + chroma_offset);
            } else if constexpr (input_format == InputFormat::YUV420_Indiv8 ||
                                 input_format == InputFormat::YUV420_Indiv16) {
                y_bytes = vld1_u8(input_Y + y * width + x);
                const std::size_t chroma_offset = (y / 2) * (width / 2) + x / 2;
                u_bytes = LoadAndDuplicateChroma(input_U + chroma_offset);
                v_bytes = LoadAndDuplicateChroma(input_V + chroma_offset);
            } else if constexpr (input_format == InputFormat::YUYV422_Interleaved) {
                const uint8x8x2_t y_chroma = vld2_u8(input_Y + (y * width + x) * 2);
                y_bytes = y_chroma.val[0];
                const uint8x8x2_t u_v = vuzp_u8(y_chroma.val[1], y_chroma.val[1]);
                u_bytes = DuplicateLowFourBytes(u_v.val[0]);
                v_bytes = DuplicateLowFourBytes(u_v.val[1]);
            } else {
                UNREACHABLE_MSG("Unknown Y2R input format {}", input_format);
                return;
            }

            ConvertYUVBandA64(y_bytes, u_bytes, v_bytes, output[x / 8].data() + y * 8,
                              coefficients);
        }
    }
}

#endif

template <InputFormat input_format>
static void ConvertYUVToRGB(const u8* input_Y, const u8* input_U, const u8* input_V,
                            ImageTile output[], unsigned int width, unsigned int height,
                            const CoefficientSet& coefficients) {
#if CITRA_ARCH(arm64)
    ConvertYUVToRGBOnAArch64<input_format>(input_Y, input_U, input_V, output, width, height,
                                           coefficients);
#else
    ConvertYUVToRGBScalar<input_format>(input_Y, input_U, input_V, output, width, height,
                                        coefficients);
#endif
}

void Testing::ConvertYUVToRGB(InputFormat input_format, const u8* input_Y, const u8* input_U,
                              const u8* input_V, ImageTile output[], unsigned int width,
                              unsigned int height, const CoefficientSet& coefficients) {
    switch (input_format) {
    case InputFormat::YUV422_Indiv8:
        ::HW::Y2R::ConvertYUVToRGB<InputFormat::YUV422_Indiv8>(input_Y, input_U, input_V, output,
                                                               width, height, coefficients);
        break;
    case InputFormat::YUV420_Indiv8:
        ::HW::Y2R::ConvertYUVToRGB<InputFormat::YUV420_Indiv8>(input_Y, input_U, input_V, output,
                                                               width, height, coefficients);
        break;
    case InputFormat::YUV422_Indiv16:
        ::HW::Y2R::ConvertYUVToRGB<InputFormat::YUV422_Indiv16>(input_Y, input_U, input_V, output,
                                                                width, height, coefficients);
        break;
    case InputFormat::YUV420_Indiv16:
        ::HW::Y2R::ConvertYUVToRGB<InputFormat::YUV420_Indiv16>(input_Y, input_U, input_V, output,
                                                                width, height, coefficients);
        break;
    case InputFormat::YUYV422_Interleaved:
        ::HW::Y2R::ConvertYUVToRGB<InputFormat::YUYV422_Interleaved>(
            input_Y, input_U, input_V, output, width, height, coefficients);
        break;
    }
}

// An 8-bit transfer with no inter-unit gap is already the exact compact byte stream consumed by
// the converter. Borrow it in place while preserving the CDMA-visible buffer progression. Gapped
// input still has to be compacted into the internal strip buffer.
CITRA_NO_INLINE static const u8* PrepareInputData8(const u8* input, u8* compact_output,
                                                   ConversionBuffer& buf,
                                                   std::size_t amount_of_data) {
    if (buf.gap == 0) {
        const u32 transferred = static_cast<u32>(amount_of_data);
        buf.address += transferred;
        buf.image_size -= transferred;
        return input;
    }

    u8* const compact_begin = compact_output;
    while (amount_of_data > 0) {
        for (std::size_t i = 0; i < buf.transfer_unit; ++i) {
            compact_output[i] = input[i];
        }

        compact_output += buf.transfer_unit;
        input += buf.transfer_unit + buf.gap;
        buf.address += buf.transfer_unit + buf.gap;
        buf.image_size -= buf.transfer_unit;
        amount_of_data -= buf.transfer_unit;
    }
    return compact_begin;
}

const u8* Testing::PrepareInputData8(const u8* input, u8* compact_output, ConversionBuffer& buf,
                                     std::size_t amount_of_data) {
    return ::HW::Y2R::PrepareInputData8(input, compact_output, buf, amount_of_data);
}

static const u8* ReceiveData8(Memory::MemorySystem& memory, u8* compact_output,
                              ConversionBuffer& buf, std::size_t amount_of_data) {
    return PrepareInputData8(memory.GetPointer(buf.address), compact_output, buf, amount_of_data);
}

/// Simulates an incoming 16-bit CDMA transfer and keeps only each sample's low byte.
template <std::size_t N>
static void ReceiveData(Memory::MemorySystem& memory, u8* output, ConversionBuffer& buf,
                        std::size_t amount_of_data) {
    static_assert(N == 2);
    const u8* input = memory.GetPointer(buf.address);

    std::size_t output_unit = buf.transfer_unit / N;
    ASSERT(amount_of_data % output_unit == 0);

    while (amount_of_data > 0) {
        for (std::size_t i = 0; i < output_unit; ++i) {
            output[i] = input[i * N];
        }

        output += output_unit;
        input += buf.transfer_unit + buf.gap;

        buf.address += buf.transfer_unit + buf.gap;
        buf.image_size -= buf.transfer_unit;
        amount_of_data -= output_unit;
    }
}

/// Convert intermediate RGB32 format to the final output format while simulating an outgoing CDMA
/// transfer.
template <OutputFormat output_format>
static void EncodeRGBToOutputScalar(const u32* input, u8* output, std::size_t pixel_count,
                                    u8 alpha) {
#if CITRA_ARCH(arm64)
// The explicit sixteen-pixel AdvSIMD body has already consumed every full band. Keep the at-most-
// fifteen-pixel remainder compact instead of letting the auto-vectorizer add alias checks and a
// second partial SIMD implementation.
#pragma clang loop vectorize(disable) interleave(disable)
#endif
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const u32 color = input[pixel];
        const Common::Vec4<u8> col_vec{static_cast<u8>(color >> 24), static_cast<u8>(color >> 16),
                                       static_cast<u8>(color >> 8), alpha};

        if constexpr (output_format == OutputFormat::RGBA8) {
            Common::Color::EncodeRGBA8(col_vec, output + pixel * 4);
        } else if constexpr (output_format == OutputFormat::RGB8) {
            Common::Color::EncodeRGB8(col_vec, output + pixel * 3);
        } else if constexpr (output_format == OutputFormat::RGB5A1) {
            Common::Color::EncodeRGB5A1(col_vec, output + pixel * 2);
        } else if constexpr (output_format == OutputFormat::RGB565) {
            Common::Color::EncodeRGB565(col_vec, output + pixel * 2);
        } else {
            UNREACHABLE_MSG("Unknown Y2R output format {}", output_format);
        }
    }
}

#if CITRA_ARCH(arm64)

// Each 16-byte output band needs bytes from only two adjacent 16-byte input vectors. Keep all
// indices below 32 so RGB8 packing lowers to three TBL2 operations and ordinary Q stores instead
// of the scalar shift/store loop or a throughput-limited structured store.
template <u32 block>
consteval std::array<u8, 16> MakeRGB8OutputShuffleA64() {
    static_assert(block < 3);
    std::array<u8, 16> indices{};
    for (u32 lane = 0; lane < indices.size(); ++lane) {
        const u32 output_byte = block * 16 + lane;
        const u32 pixel = output_byte / 3;
        const u32 component = output_byte % 3;
        const u32 source_byte = pixel * 4 + component + 1;
        indices[lane] = static_cast<u8>(source_byte - block * 16);
    }
    return indices;
}

inline constexpr std::array<std::array<u8, 16>, 3> RGB8_OUTPUT_SHUFFLES_A64 = {
    MakeRGB8OutputShuffleA64<0>(),
    MakeRGB8OutputShuffleA64<1>(),
    MakeRGB8OutputShuffleA64<2>(),
};

static_assert([] {
    for (u32 output_byte = 0; output_byte < 48; ++output_byte) {
        const u32 pixel = output_byte / 3;
        const u32 component = output_byte % 3;
        const u32 block = output_byte / 16;
        const u8 local_index = RGB8_OUTPUT_SHUFFLES_A64[block][output_byte % 16];
        if (local_index >= 32 || block * 16 + local_index != pixel * 4 + component + 1) {
            return false;
        }
    }
    return true;
}());

CITRA_NO_INLINE static std::size_t EncodeRGB8ToOutputA64(const u32* input, u8* output,
                                                         std::size_t pixel_count) {
    const uint8x16_t shuffle_0 = vld1q_u8(RGB8_OUTPUT_SHUFFLES_A64[0].data());
    const uint8x16_t shuffle_1 = vld1q_u8(RGB8_OUTPUT_SHUFFLES_A64[1].data());
    const uint8x16_t shuffle_2 = vld1q_u8(RGB8_OUTPUT_SHUFFLES_A64[2].data());
    std::size_t pixel = 0;
    for (; pixel + 16 <= pixel_count; pixel += 16) {
        const u8* const src = reinterpret_cast<const u8*>(input + pixel);
        const uint8x16_t input_0 = vld1q_u8(src);
        const uint8x16_t input_1 = vld1q_u8(src + 16);
        const uint8x16_t input_2 = vld1q_u8(src + 32);
        const uint8x16_t input_3 = vld1q_u8(src + 48);
        const uint8x16x2_t table_0 = {input_0, input_1};
        const uint8x16x2_t table_1 = {input_1, input_2};
        const uint8x16x2_t table_2 = {input_2, input_3};
        u8* const dst = output + pixel * 3;
        vst1q_u8(dst, vqtbl2q_u8(table_0, shuffle_0));
        vst1q_u8(dst + 16, vqtbl2q_u8(table_1, shuffle_1));
        vst1q_u8(dst + 32, vqtbl2q_u8(table_2, shuffle_2));
    }
    return pixel;
}

template <OutputFormat output_format>
static std::size_t EncodeRGBToOutputA64(const u32* input, u8* output, std::size_t pixel_count,
                                        u8 alpha) {
    static_assert(output_format == OutputFormat::RGBA8 || output_format == OutputFormat::RGB8 ||
                  output_format == OutputFormat::RGB5A1 || output_format == OutputFormat::RGB565);

    if constexpr (output_format == OutputFormat::RGB8) {
        return EncodeRGB8ToOutputA64(input, output, pixel_count);
    }

    std::size_t pixel = 0;
    if constexpr (output_format == OutputFormat::RGBA8) {
        const uint32x4_t alpha_words = vdupq_n_u32(alpha);
        for (; pixel + 16 <= pixel_count; pixel += 16) {
            // Y2R's intermediate words are numeric 0xRRGGBB00, so on little-endian AArch64 the
            // unused low byte is exactly the output alpha lane. Four ordinary loads/stores avoid
            // the auto-vectorizer's shift/TBL/ST4 sequence.
            u8* const dst = output + pixel * 4;
            vst1q_u8(dst, vreinterpretq_u8_u32(vorrq_u32(vld1q_u32(input + pixel), alpha_words)));
            vst1q_u8(dst + 16,
                     vreinterpretq_u8_u32(vorrq_u32(vld1q_u32(input + pixel + 4), alpha_words)));
            vst1q_u8(dst + 32,
                     vreinterpretq_u8_u32(vorrq_u32(vld1q_u32(input + pixel + 8), alpha_words)));
            vst1q_u8(dst + 48,
                     vreinterpretq_u8_u32(vorrq_u32(vld1q_u32(input + pixel + 12), alpha_words)));
        }
    } else {
        const uint8x16_t alpha_bytes = vdupq_n_u8(alpha);
        for (; pixel + 16 <= pixel_count; pixel += 16) {
            // The input byte lanes are [0, B, G, R]. One Q-form LD4 extracts all sixteen pixels;
            // byte masks plus SHLL/SHLL2 build the exact little-endian 16-bit output words.
            const uint8x16x4_t components = vld4q_u8(reinterpret_cast<const u8*>(input + pixel));
            uint8x16_t red = vandq_u8(components.val[3], vdupq_n_u8(0xF8));
            uint8x16_t green;
            uint8x16_t blue_alpha;
            if constexpr (output_format == OutputFormat::RGB565) {
                green = vandq_u8(components.val[2], vdupq_n_u8(0xFC));
                blue_alpha = vshrq_n_u8(components.val[1], 3);
            } else {
                green = vandq_u8(components.val[2], vdupq_n_u8(0xF8));
                blue_alpha = vandq_u8(vshrq_n_u8(components.val[1], 2), vdupq_n_u8(0x3E));
                blue_alpha = vsraq_n_u8(blue_alpha, alpha_bytes, 7);
            }

            const uint16x8_t red_low = vshll_n_u8(vget_low_u8(red), 8);
            const uint16x8_t red_high = vshll_high_n_u8(red, 8);
            const uint16x8_t green_low = vshll_n_u8(vget_low_u8(green), 3);
            const uint16x8_t green_high = vshll_high_n_u8(green, 3);
            const uint16x8_t blue_alpha_low = vmovl_u8(vget_low_u8(blue_alpha));
            const uint16x8_t blue_alpha_high = vmovl_high_u8(blue_alpha);
            vst1q_u8(output + pixel * 2, vreinterpretq_u8_u16(vorrq_u16(
                                             vorrq_u16(red_low, green_low), blue_alpha_low)));
            vst1q_u8(
                output + pixel * 2 + 16,
                vreinterpretq_u8_u16(vorrq_u16(vorrq_u16(red_high, green_high), blue_alpha_high)));
        }
    }
    return pixel;
}

#endif

template <OutputFormat output_format>
static void EncodeRGBToOutput(const u32* input, u8* output, std::size_t pixel_count, u8 alpha) {
    std::size_t encoded = 0;
#if CITRA_ARCH(arm64)
    encoded = EncodeRGBToOutputA64<output_format>(input, output, pixel_count, alpha);
#endif
    if (encoded < pixel_count) {
        constexpr std::size_t bytes_per_pixel = output_format == OutputFormat::RGBA8  ? 4
                                                : output_format == OutputFormat::RGB8 ? 3
                                                                                      : 2;
        EncodeRGBToOutputScalar<output_format>(input + encoded, output + encoded * bytes_per_pixel,
                                               pixel_count - encoded, alpha);
    }
}

void Testing::EncodeRGBToOutput(OutputFormat output_format, const u32* input, u8* output,
                                std::size_t pixel_count, u8 alpha) {
    switch (output_format) {
    case OutputFormat::RGBA8:
        ::HW::Y2R::EncodeRGBToOutput<OutputFormat::RGBA8>(input, output, pixel_count, alpha);
        break;
    case OutputFormat::RGB8:
        ::HW::Y2R::EncodeRGBToOutput<OutputFormat::RGB8>(input, output, pixel_count, alpha);
        break;
    case OutputFormat::RGB5A1:
        ::HW::Y2R::EncodeRGBToOutput<OutputFormat::RGB5A1>(input, output, pixel_count, alpha);
        break;
    case OutputFormat::RGB565:
        ::HW::Y2R::EncodeRGBToOutput<OutputFormat::RGB565>(input, output, pixel_count, alpha);
        break;
    }
}

template <OutputFormat output_format>
static void SendData(Memory::MemorySystem& memory, const u32* input, ConversionBuffer& buf,
                     std::size_t amount_of_data, u8 alpha) {
    constexpr std::size_t bytes_per_pixel = output_format == OutputFormat::RGBA8  ? 4
                                            : output_format == OutputFormat::RGB8 ? 3
                                                                                  : 2;

    u8* output = memory.GetPointer(buf.address);
    const std::size_t output_unit = buf.transfer_unit / bytes_per_pixel;
    ASSERT(output_unit > 0 && buf.transfer_unit % bytes_per_pixel == 0);
    ASSERT(amount_of_data % output_unit == 0);

    while (amount_of_data > 0) {
        EncodeRGBToOutput<output_format>(input, output, output_unit, alpha);
        input += output_unit;
        output += buf.transfer_unit + buf.gap;
        buf.address += buf.transfer_unit + buf.gap;
        buf.image_size -= buf.transfer_unit;
        amount_of_data -= output_unit;
    }
}

static const u8 linear_lut[TILE_SIZE] = {
    // clang-format off
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63,
    // clang-format on
};

static const u8 morton_lut[TILE_SIZE] = {
    // clang-format off
     0,  1,  4,  5, 16, 17, 20, 21,
     2,  3,  6,  7, 18, 19, 22, 23,
     8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53,
    34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61,
    42, 43, 46, 47, 58, 59, 62, 63,
    // clang-format on
};

static void RotateTile0(const ImageTile& input, ImageTile& output, int height,
                        const u8 out_map[64]) {
    for (int i = 0; i < height * 8; ++i) {
        output[out_map[i]] = input[i];
    }
}

static void RotateTile90(const ImageTile& input, ImageTile& output, int height,
                         const u8 out_map[64]) {
    int out_i = 0;
    for (int x = 0; x < 8; ++x) {
        for (int y = height - 1; y >= 0; --y) {
            output[out_map[out_i++]] = input[y * 8 + x];
        }
    }
}

static void RotateTile180(const ImageTile& input, ImageTile& output, int height,
                          const u8 out_map[64]) {
    int out_i = 0;
    for (int i = height * 8 - 1; i >= 0; --i) {
        output[out_map[out_i++]] = input[i];
    }
}

static void RotateTile270(const ImageTile& input, ImageTile& output, int height,
                          const u8 out_map[64]) {
    int out_i = 0;
    for (int x = 8 - 1; x >= 0; --x) {
        for (int y = 0; y < height; ++y) {
            output[out_map[out_i++]] = input[y * 8 + x];
        }
    }
}

static void WriteTileToOutput(u32* output, const ImageTile& tile, int height, int line_stride) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < 8; ++x) {
            output[y * line_stride + x] = tile[y * 8 + x];
        }
    }
}

// Rotation::None with linear output has no remapping to perform: linear_lut is the identity.
// Write complete tile rows directly into their final strip positions instead of first copying the
// pixels through tmp_tile. Keeping this outlined also prevents the eight-row copy body from being
// duplicated into PerformConversion's already-large format dispatcher.
CITRA_NO_INLINE static void WriteUnrotatedLinearTiles(u32* output, const ImageTile tiles[],
                                                      std::size_t num_tiles, unsigned int height,
                                                      unsigned int line_stride) {
    if (num_tiles == 0) {
        return;
    }
    for (unsigned int y = 0; y < height; ++y) {
        const ImageTile* input_tile = tiles;
        u32* output_row = output + y * line_stride;
        for (std::size_t tile = 0; tile < num_tiles; ++tile) {
            std::memcpy(output_row, input_tile->data() + y * 8, 8 * sizeof(u32));
            ++input_tile;
            output_row += 8;
        }
    }
}

void Testing::WriteUnrotatedLinearTiles(u32* output, const ImageTile tiles[], std::size_t num_tiles,
                                        unsigned int height, unsigned int line_stride) {
    ::HW::Y2R::WriteUnrotatedLinearTiles(output, tiles, num_tiles, height, line_stride);
}

MICROPROFILE_DEFINE(Y2R_PerformConversion, "Y2R", "PerformConversion", MP_RGB(185, 66, 245));

/**
 * Performs a Y2R colorspace conversion.
 *
 * The Y2R hardware implements hardware-accelerated YUV to RGB colorspace conversions. It is most
 * commonly used for video playback or to display camera input to the screen.
 *
 * The conversion process is quite configurable, and can be divided in distinct steps. From
 * observation, it appears that the hardware buffers a single 8-pixel tall strip of image data
 * internally and converts it in one go before writing to the output and loading the next strip.
 *
 * The steps taken to convert one strip of image data are:
 *
 * - The hardware receives data via CDMA (http://3dbrew.org/wiki/Corelink_DMA_Engines), which is
 *   presumably stored in one or more internal buffers. This process can be done in several separate
 *   transfers, as long as they don't exceed the size of the internal image buffer. This allows
 *   flexibility in input strides.
 * - The input data is decoded into a YUV tuple. Several formats are suported, see the `InputFormat`
 *   enum.
 * - The YUV tuple is converted, using fixed point calculations, to RGB. This step can be configured
 *   using a set of coefficients to support different colorspace standards. See `CoefficientSet`.
 * - The strip can be optionally rotated 90, 180 or 270 degrees. Since each strip is processed
 *   independently, this notably rotates each *strip*, not the entire image. This means that for 90
 *   or 270 degree rotations, the output will be in terms of several 8 x height images, and for any
 *   non-zero rotation the strips will have to be re-arranged so that the parts of the image will
 *   not be shuffled together. This limitation makes this a feature of somewhat dubious utility. 90
 *   or 270 degree rotations in images with non-even height don't seem to work properly.
 * - The data is converted to the output RGB format. See the `OutputFormat` enum.
 * - The data can be output either linearly line-by-line or in the swizzled 8x8 tile format used by
 *   the PICA. This is decided by the `BlockAlignment` enum. If 8x8 alignment is used, then the
 *   image must have a height divisible by 8. The image width must always be divisible by 8.
 * - The final data is then CDMAed out to main memory and the next image strip is processed. This
 *   offers the same flexibility as the input stage.
 *
 * In this implementation, to avoid the combinatorial explosion of parameter combinations, common
 * intermediate formats are used and where possible tables or parameters are used instead of
 * diverging code paths to keep the amount of branches in check. Some steps are also merged to
 * increase efficiency.
 *
 * Output for all valid settings combinations matches hardware, however output in some edge-cases
 * differs:
 *
 * - `Block8x8` alignment with non-mod8 height produces different garbage patterns on the last
 *   strip, especially when combined with rotation.
 * - Hardware, when using `Linear` alignment with a non-even height and 90 or 270 degree rotation
 *   produces misaligned output on the last strip. This implmentation produces output with the
 *   correct "expected" alignment.
 *
 * Hardware behaves strangely (doesn't fire the completion interrupt, for example) in these cases,
 * so they are believed to be invalid configurations anyway.
 */
void PerformConversion(Memory::MemorySystem& memory, ConversionConfiguration cvt) {
    MICROPROFILE_SCOPE(Y2R_PerformConversion);

    ASSERT(cvt.input_line_width % 8 == 0);
    ASSERT(cvt.block_alignment != BlockAlignment::Block8x8 || cvt.input_lines % 8 == 0);
    // Tiles per row
    std::size_t num_tiles = cvt.input_line_width / 8;
    ASSERT(num_tiles <= MAX_TILES);

    // Buffer used as a CDMA source/target.
    std::unique_ptr<u8[]> data_buffer(new u8[cvt.input_line_width * 8 * 4]);
    // Intermediate storage for decoded 8x8 image tiles. Always stored as RGB32.
    std::unique_ptr<ImageTile[]> tiles(new ImageTile[num_tiles]);

    // LUT used to remap writes to a tile. Used to allow linear or swizzled output without
    // requiring two different code paths.
    const u8* tile_remap = nullptr;
    switch (cvt.block_alignment) {
    case BlockAlignment::Linear:
        tile_remap = linear_lut;
        break;
    case BlockAlignment::Block8x8:
        tile_remap = morton_lut;
        break;
    }

    for (unsigned int y = 0; y < cvt.input_lines; y += 8) {
        unsigned int row_height = std::min(cvt.input_lines - y, 8u);

        // Total size in pixels of incoming data required for this strip.
        const std::size_t row_data_size = row_height * cvt.input_line_width;

        u8* const compact_Y = data_buffer.get();
        u8* const compact_U = compact_Y + 8 * cvt.input_line_width;
        u8* const compact_V = compact_U + 8 * cvt.input_line_width / 2;
        const u8* input_Y = compact_Y;
        const u8* input_U = compact_U;
        const u8* input_V = compact_V;

        switch (cvt.input_format) {
        case InputFormat::YUV422_Indiv8:
            input_Y = ReceiveData8(memory, compact_Y, cvt.src_Y, row_data_size);
            input_U = ReceiveData8(memory, compact_U, cvt.src_U, row_data_size / 2);
            input_V = ReceiveData8(memory, compact_V, cvt.src_V, row_data_size / 2);
            ConvertYUVToRGB<InputFormat::YUV422_Indiv8>(input_Y, input_U, input_V, tiles.get(),
                                                        cvt.input_line_width, row_height,
                                                        cvt.coefficients);
            break;
        case InputFormat::YUV420_Indiv8:
            input_Y = ReceiveData8(memory, compact_Y, cvt.src_Y, row_data_size);
            input_U = ReceiveData8(memory, compact_U, cvt.src_U, row_data_size / 4);
            input_V = ReceiveData8(memory, compact_V, cvt.src_V, row_data_size / 4);
            ConvertYUVToRGB<InputFormat::YUV420_Indiv8>(input_Y, input_U, input_V, tiles.get(),
                                                        cvt.input_line_width, row_height,
                                                        cvt.coefficients);
            break;
        case InputFormat::YUV422_Indiv16:
            ReceiveData<2>(memory, compact_Y, cvt.src_Y, row_data_size);
            ReceiveData<2>(memory, compact_U, cvt.src_U, row_data_size / 2);
            ReceiveData<2>(memory, compact_V, cvt.src_V, row_data_size / 2);
            ConvertYUVToRGB<InputFormat::YUV422_Indiv16>(input_Y, input_U, input_V, tiles.get(),
                                                         cvt.input_line_width, row_height,
                                                         cvt.coefficients);
            break;
        case InputFormat::YUV420_Indiv16:
            ReceiveData<2>(memory, compact_Y, cvt.src_Y, row_data_size);
            ReceiveData<2>(memory, compact_U, cvt.src_U, row_data_size / 4);
            ReceiveData<2>(memory, compact_V, cvt.src_V, row_data_size / 4);
            ConvertYUVToRGB<InputFormat::YUV420_Indiv16>(input_Y, input_U, input_V, tiles.get(),
                                                         cvt.input_line_width, row_height,
                                                         cvt.coefficients);
            break;
        case InputFormat::YUYV422_Interleaved:
            input_U = nullptr;
            input_V = nullptr;
            input_Y = ReceiveData8(memory, compact_Y, cvt.src_YUYV, row_data_size * 2);
            ConvertYUVToRGB<InputFormat::YUYV422_Interleaved>(input_Y, input_U, input_V,
                                                              tiles.get(), cvt.input_line_width,
                                                              row_height, cvt.coefficients);
            break;
        default:
            UNREACHABLE_MSG("Unknown Y2R input format {}", cvt.input_format);
            return;
        }

        u32* output_buffer = reinterpret_cast<u32*>(data_buffer.get());

        if (cvt.rotation == Rotation::None && cvt.block_alignment == BlockAlignment::Linear) {
            WriteUnrotatedLinearTiles(output_buffer, tiles.get(), num_tiles, row_height,
                                      cvt.input_line_width);
        } else {
            ImageTile tmp_tile;
            for (std::size_t i = 0; i < num_tiles; ++i) {
                int image_strip_width = 0;
                int output_stride = 0;

                switch (cvt.rotation) {
                case Rotation::None:
                    RotateTile0(tiles[i], tmp_tile, row_height, tile_remap);
                    image_strip_width = cvt.input_line_width;
                    output_stride = 8;
                    break;
                case Rotation::Clockwise_90:
                    RotateTile90(tiles[i], tmp_tile, row_height, tile_remap);
                    image_strip_width = 8;
                    output_stride = 8 * row_height;
                    break;
                case Rotation::Clockwise_180:
                    // For 180 and 270 degree rotations we also invert the order of tiles in the
                    // strip, since the rotates are done individually on each tile.
                    RotateTile180(tiles[num_tiles - i - 1], tmp_tile, row_height, tile_remap);
                    image_strip_width = cvt.input_line_width;
                    output_stride = 8;
                    break;
                case Rotation::Clockwise_270:
                    RotateTile270(tiles[num_tiles - i - 1], tmp_tile, row_height, tile_remap);
                    image_strip_width = 8;
                    output_stride = 8 * row_height;
                    break;
                }

                switch (cvt.block_alignment) {
                case BlockAlignment::Linear:
                    WriteTileToOutput(output_buffer, tmp_tile, row_height, image_strip_width);
                    output_buffer += output_stride;
                    break;
                case BlockAlignment::Block8x8:
                    WriteTileToOutput(output_buffer, tmp_tile, 8, 8);
                    output_buffer += TILE_SIZE;
                    break;
                }
            }
        }

        switch (cvt.output_format) {
        case OutputFormat::RGBA8:
            SendData<OutputFormat::RGBA8>(memory, reinterpret_cast<u32*>(data_buffer.get()),
                                          cvt.dst, row_data_size, static_cast<u8>(cvt.alpha));
            break;
        case OutputFormat::RGB8:
            SendData<OutputFormat::RGB8>(memory, reinterpret_cast<u32*>(data_buffer.get()), cvt.dst,
                                         row_data_size, static_cast<u8>(cvt.alpha));
            break;
        case OutputFormat::RGB5A1:
            SendData<OutputFormat::RGB5A1>(memory, reinterpret_cast<u32*>(data_buffer.get()),
                                           cvt.dst, row_data_size, static_cast<u8>(cvt.alpha));
            break;
        case OutputFormat::RGB565:
            SendData<OutputFormat::RGB565>(memory, reinterpret_cast<u32*>(data_buffer.get()),
                                           cvt.dst, row_data_size, static_cast<u8>(cvt.alpha));
            break;
        default:
            UNREACHABLE_MSG("Unknown Y2R output format {}", cvt.output_format);
            return;
        }
    }
}
} // namespace HW::Y2R
