// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstddef>
#include "common/common_types.h"
#include "core/hle/service/cam/y2r_u.h"

namespace HW::Y2R::Testing {

using ImageTile = std::array<u32, 8 * 8>;

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
void ConvertYUVToRGB(Service::Y2R::InputFormat input_format, const u8* input_Y, const u8* input_U,
                     const u8* input_V, ImageTile output[], unsigned int width, unsigned int height,
                     const Service::Y2R::CoefficientSet& coefficients);

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
void EncodeRGBToOutput(Service::Y2R::OutputFormat output_format, const u32* input, u8* output,
                       std::size_t pixel_count, u8 alpha);

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
void WriteUnrotatedLinearTiles(u32* output, const ImageTile tiles[], std::size_t num_tiles,
                               unsigned int height, unsigned int line_stride);

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
const u8*
PrepareInputData8(const u8* input, u8* compact_output, Service::Y2R::ConversionBuffer& buffer,
                  std::size_t amount_of_data);

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
void SendUnrotatedLinearData(Service::Y2R::OutputFormat output_format, u8* output,
                             const ImageTile tiles[], std::size_t num_tiles, unsigned int height,
                             Service::Y2R::ConversionBuffer& buffer, u8 alpha);

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
bool CanBypassDataBuffer(const Service::Y2R::ConversionConfiguration& configuration);

} // namespace HW::Y2R::Testing
