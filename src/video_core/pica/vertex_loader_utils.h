// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <type_traits>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "common/common_types.h"
#include "common/vector_math.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica_types.h"

namespace Pica::VertexLoaderUtils {

enum class AttributeLoader : u8 {
    Invalid,
    Byte1,
    Byte2,
    Byte3,
    Byte4,
    UByte1,
    UByte2,
    UByte3,
    UByte4,
    Short1,
    Short2,
    Short3,
    Short4,
    Float1,
    Float2,
    Float3,
    Float4,
};

constexpr AttributeLoader Decode(PipelineRegs::VertexAttributeFormat format, u32 elements) {
    if (elements < 1 || elements > 4) {
        return AttributeLoader::Invalid;
    }
    const u32 key = static_cast<u32>(format) * 4 + elements;
    return static_cast<AttributeLoader>(key);
}

template <typename T, u32 Elements>
[[gnu::always_inline]] inline void LoadTyped(const void* source, Common::Vec4<f24>& output) {
    const T* data = reinterpret_cast<const T*>(source);
    output[0] = f24::FromFloat32(static_cast<f32>(data[0]));
    if constexpr (Elements >= 2) {
        output[1] = f24::FromFloat32(static_cast<f32>(data[1]));
    } else {
        output[1] = f24::Zero();
    }
    if constexpr (Elements >= 3) {
        output[2] = f24::FromFloat32(static_cast<f32>(data[2]));
    } else {
        output[2] = f24::Zero();
    }
    if constexpr (Elements >= 4) {
        output[3] = f24::FromFloat32(static_cast<f32>(data[3]));
    } else {
        output[3] = f24::One();
    }
}

#if defined(__aarch64__)
template <>
[[gnu::always_inline]] inline void LoadTyped<s8, 4>(const void* source, Common::Vec4<f24>& output) {
    u32 packed;
    std::memcpy(&packed, source, sizeof(packed));
    const int8x8_t bytes = vreinterpret_s8_u64(vcreate_u64(packed));
    const int16x8_t halves = vmovl_s8(bytes);
    const int32x4_t words = vmovl_s16(vget_low_s16(halves));
    const float32x4_t values = vcvtq_f32_s32(words);
    static_assert(sizeof(values) == sizeof(output));
    static_assert(std::is_trivially_copyable_v<Common::Vec4<f24>>);
    std::memcpy(&output, &values, sizeof(values));
}

template <>
[[gnu::always_inline]] inline void LoadTyped<u8, 4>(const void* source, Common::Vec4<f24>& output) {
    u32 packed;
    std::memcpy(&packed, source, sizeof(packed));
    const uint8x8_t bytes = vreinterpret_u8_u64(vcreate_u64(packed));
    const uint16x8_t halves = vmovl_u8(bytes);
    const uint32x4_t words = vmovl_u16(vget_low_u16(halves));
    const float32x4_t values = vcvtq_f32_u32(words);
    static_assert(sizeof(values) == sizeof(output));
    static_assert(std::is_trivially_copyable_v<Common::Vec4<f24>>);
    std::memcpy(&output, &values, sizeof(values));
}

template <>
[[gnu::always_inline]] inline void LoadTyped<s16, 4>(const void* source,
                                                     Common::Vec4<f24>& output) {
    int16x4_t halves;
    std::memcpy(&halves, source, sizeof(halves));
    const int32x4_t words = vmovl_s16(halves);
    const float32x4_t values = vcvtq_f32_s32(words);
    static_assert(sizeof(values) == sizeof(output));
    static_assert(std::is_trivially_copyable_v<Common::Vec4<f24>>);
    std::memcpy(&output, &values, sizeof(values));
}
#endif

template <>
[[gnu::always_inline]] inline void LoadTyped<f32, 4>(const void* source,
                                                     Common::Vec4<f24>& output) {
    static_assert(sizeof(f24) == sizeof(f32));
    static_assert(std::is_trivially_copyable_v<Common::Vec4<f24>>);
    std::memcpy(&output, source, sizeof(output));
}

[[gnu::always_inline]] inline void LoadAttribute(AttributeLoader loader, const void* source,
                                                 Common::Vec4<f24>& output) {
    switch (loader) {
    case AttributeLoader::Byte1:
        return LoadTyped<s8, 1>(source, output);
    case AttributeLoader::Byte2:
        return LoadTyped<s8, 2>(source, output);
    case AttributeLoader::Byte3:
        return LoadTyped<s8, 3>(source, output);
    case AttributeLoader::Byte4:
        return LoadTyped<s8, 4>(source, output);
    case AttributeLoader::UByte1:
        return LoadTyped<u8, 1>(source, output);
    case AttributeLoader::UByte2:
        return LoadTyped<u8, 2>(source, output);
    case AttributeLoader::UByte3:
        return LoadTyped<u8, 3>(source, output);
    case AttributeLoader::UByte4:
        return LoadTyped<u8, 4>(source, output);
    case AttributeLoader::Short1:
        return LoadTyped<s16, 1>(source, output);
    case AttributeLoader::Short2:
        return LoadTyped<s16, 2>(source, output);
    case AttributeLoader::Short3:
        return LoadTyped<s16, 3>(source, output);
    case AttributeLoader::Short4:
        return LoadTyped<s16, 4>(source, output);
    case AttributeLoader::Float1:
        return LoadTyped<f32, 1>(source, output);
    case AttributeLoader::Float2:
        return LoadTyped<f32, 2>(source, output);
    case AttributeLoader::Float3:
        return LoadTyped<f32, 3>(source, output);
    case AttributeLoader::Float4:
        return LoadTyped<f32, 4>(source, output);
    case AttributeLoader::Invalid:
        return;
    }
}

} // namespace Pica::VertexLoaderUtils
