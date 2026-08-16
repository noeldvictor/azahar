// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include "common/common_types.h"
#include "common/vector_math.h"

namespace Pica::Texture {

Common::Vec3<u8> SampleETC1Subtile(u64 value, unsigned int x, unsigned int y);

/// Decode a complete 4x4 ETC1 block to RGBA8. The output stride may be negative.
void DecodeETC1Subtile(u64 value, u8* output, std::ptrdiff_t output_stride);

/// Decode a complete 4x4 ETC1A4 block to RGBA8. The output stride may be negative.
void DecodeETC1A4Subtile(u64 value, u64 alpha, u8* output, std::ptrdiff_t output_stride);

} // namespace Pica::Texture
