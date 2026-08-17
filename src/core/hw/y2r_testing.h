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

} // namespace HW::Y2R::Testing
