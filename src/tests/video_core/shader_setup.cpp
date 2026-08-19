// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_setup.h"

namespace {

constexpr u32 MaxTestCount = 24;

void CheckProgramRange(size_t offset, u32 count, u64 changed_mask) {
    std::array<u32, MaxTestCount> values{};
    for (u32 i = 0; i < count; ++i) {
        if ((changed_mask & (u64{1} << i)) != 0) {
            values[i] = 0x9E3779B9u ^ (i * 0x10204081u);
        }
    }

    Pica::ShaderSetup range_setup;
    Pica::ShaderSetup scalar_setup;
    range_setup.UpdateProgramCodeRange(offset, values.data(), count);
    for (u32 i = 0; i < count; ++i) {
        scalar_setup.UpdateProgramCode(offset + i, values[i]);
    }

    REQUIRE(range_setup.GetProgramCode() == scalar_setup.GetProgramCode());
    REQUIRE(range_setup.GetBiggestProgramSize() == scalar_setup.GetBiggestProgramSize());
    REQUIRE(range_setup.GetProgramCodeHash() == scalar_setup.GetProgramCodeHash());

    // Exercise the unchanged fast path after hashing has cleared the dirty flag.
    range_setup.UpdateProgramCodeRange(offset, values.data(), count);
    REQUIRE(range_setup.GetProgramCodeHash() == scalar_setup.GetProgramCodeHash());

    if (count != 0) {
        values[0] ^= 0xA5A5A5A5u;
        range_setup.UpdateProgramCodeRange(offset, values.data(), count);
        scalar_setup.UpdateProgramCode(offset, values[0]);
        REQUIRE(range_setup.GetProgramCode() == scalar_setup.GetProgramCode());
        REQUIRE(range_setup.GetBiggestProgramSize() == scalar_setup.GetBiggestProgramSize());
        REQUIRE(range_setup.GetProgramCodeHash() == scalar_setup.GetProgramCodeHash());
    }
}

void CheckSwizzleRange(size_t offset, u32 count, u64 changed_mask) {
    std::array<u32, MaxTestCount> values{};
    for (u32 i = 0; i < count; ++i) {
        if ((changed_mask & (u64{1} << i)) != 0) {
            values[i] = 0x7F4A7C15u ^ (i * 0x01020408u);
        }
    }

    Pica::ShaderSetup range_setup;
    Pica::ShaderSetup scalar_setup;
    range_setup.UpdateSwizzleDataRange(offset, values.data(), count);
    for (u32 i = 0; i < count; ++i) {
        scalar_setup.UpdateSwizzleData(offset + i, values[i]);
    }

    REQUIRE(range_setup.GetSwizzleData() == scalar_setup.GetSwizzleData());
    REQUIRE(range_setup.GetBiggestSwizzleSize() == scalar_setup.GetBiggestSwizzleSize());
    REQUIRE(range_setup.GetSwizzleDataHash() == scalar_setup.GetSwizzleDataHash());

    // Exercise the unchanged fast path after hashing has cleared the dirty flag.
    range_setup.UpdateSwizzleDataRange(offset, values.data(), count);
    REQUIRE(range_setup.GetSwizzleDataHash() == scalar_setup.GetSwizzleDataHash());

    if (count != 0) {
        values[0] ^= 0x5A5A5A5Au;
        range_setup.UpdateSwizzleDataRange(offset, values.data(), count);
        scalar_setup.UpdateSwizzleData(offset, values[0]);
        REQUIRE(range_setup.GetSwizzleData() == scalar_setup.GetSwizzleData());
        REQUIRE(range_setup.GetBiggestSwizzleSize() == scalar_setup.GetBiggestSwizzleSize());
        REQUIRE(range_setup.GetSwizzleDataHash() == scalar_setup.GetSwizzleDataHash());
    }
}

template <typename Function>
void CheckRangePatterns(Function check) {
    for (const size_t offset : {size_t{0}, size_t{3}}) {
        for (u32 count = 0; count <= MaxTestCount; ++count) {
            CAPTURE(offset, count);
            const u64 full_mask = count == 0 ? 0 : (u64{1} << count) - 1;
            check(offset, count, 0);
            check(offset, count, full_mask);
            check(offset, count, full_mask & 0xAAAAAAu);
            check(offset, count, full_mask & 0x555555u);
            for (u32 changed = 0; changed < count; ++changed) {
                CAPTURE(changed);
                check(offset, count, u64{1} << changed);
            }
        }
    }
}

constexpr std::array<u32, 16> FloatUniformWords{
    0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7F800000, 0xFF800000, 0x7FC00001, 0x7FA00001,
    0x00000001, 0x007FFFFF, 0x00800000, 0x7F7FFFFF, 0x12345678, 0x9ABCDEF0, 0x55555555, 0xAAAAAAAA,
};

void InitializeFloatUniforms(Pica::ShaderSetup& setup) {
    for (u32 index = 0; index < setup.uniforms.f.size(); ++index) {
        for (u32 component = 0; component < 4; ++component) {
            const u32 bits = 0x3E000000u + index * 0x10101u + component * 0x100000u;
            setup.uniforms.f[index][component] = Pica::f24::FromFloat32(std::bit_cast<float>(bits));
        }
    }
}

void CheckFloatUniformRange(bool is_float32, u32 start_index, u32 prefix, u32 count,
                            bool initially_dirty) {
    std::array<u32, 68> values{};
    for (u32 i = 0; i < values.size(); ++i) {
        values[i] = FloatUniformWords[i % FloatUniformWords.size()] ^ (i * 0x01020408u);
    }

    Pica::ShaderSetup range_setup;
    Pica::ShaderSetup scalar_setup;
    Pica::ShaderRegs range_regs{};
    Pica::ShaderRegs scalar_regs{};
    InitializeFloatUniforms(range_setup);
    InitializeFloatUniforms(scalar_setup);
    range_setup.uniforms_dirty = initially_dirty;
    scalar_setup.uniforms_dirty = initially_dirty;

    using Format = decltype(range_regs.uniform_setup)::Format;
    const auto format = is_float32 ? Format::Float32 : Format::Float24;
    range_regs.uniform_setup.format.Assign(format);
    scalar_regs.uniform_setup.format.Assign(format);
    range_regs.uniform_setup.index.Assign(start_index);
    scalar_regs.uniform_setup.index.Assign(start_index);

    for (u32 i = 0; i < prefix; ++i) {
        REQUIRE_FALSE(range_setup.uniform_queue.Push(values[i], is_float32));
        REQUIRE_FALSE(scalar_setup.uniform_queue.Push(values[i], is_float32));
    }

    const auto range =
        range_setup.WriteUniformFloatRegRange(range_regs, values.data() + prefix, count);
    std::optional<u32> scalar_first;
    u32 scalar_written = 0;
    for (u32 i = 0; i < count; ++i) {
        const auto index = scalar_setup.WriteUniformFloatReg(scalar_regs, values[prefix + i]);
        if (index) {
            scalar_first = scalar_first.value_or(*index);
            ++scalar_written;
        } else if (scalar_setup.uniform_queue.index == 0 &&
                   scalar_regs.uniform_setup.index >= scalar_setup.uniforms.f.size()) {
            break;
        }
    }

    REQUIRE(range.has_value() == scalar_first.has_value());
    if (range) {
        REQUIRE(range->first_index == *scalar_first);
        REQUIRE(range->count == scalar_written);
    }
    REQUIRE(range_setup.uniforms.f == scalar_setup.uniforms.f);
    REQUIRE(range_setup.uniform_queue.buffer == scalar_setup.uniform_queue.buffer);
    REQUIRE(range_setup.uniform_queue.index == scalar_setup.uniform_queue.index);
    REQUIRE(range_regs.uniform_setup.index == scalar_regs.uniform_setup.index);
    REQUIRE(range_setup.uniforms_dirty == scalar_setup.uniforms_dirty);
}

} // namespace

TEST_CASE("PICA program-code range updates match scalar writes", "[video_core][shader_setup]") {
    CheckRangePatterns(CheckProgramRange);
}

TEST_CASE("PICA swizzle range updates match scalar writes", "[video_core][shader_setup]") {
    CheckRangePatterns(CheckSwizzleRange);
}

TEST_CASE("PICA bool-uniform writes match the scalar bit mapping",
          "[video_core][shader_setup]") {
    Pica::ShaderSetup setup;
    std::array<bool, 16> expected{};

    for (u32 value = 0; value <= 0xFFFF; ++value) {
        CAPTURE(value);
        for (u32 bit = 0; bit < expected.size(); ++bit) {
            expected[bit] = (value & (u32{1} << bit)) != 0;
            setup.uniforms.b[bit] = !expected[bit];
        }

        setup.uniforms_dirty = false;
        setup.WriteUniformBoolReg(value);
        REQUIRE(setup.uniforms.b == expected);
        REQUIRE(setup.uniforms_dirty);

        setup.uniforms_dirty = false;
        setup.WriteUniformBoolReg(value);
        REQUIRE(setup.uniforms.b == expected);
        REQUIRE_FALSE(setup.uniforms_dirty);
    }
}

TEST_CASE("PICA float-uniform range writes match scalar queue writes",
          "[video_core][shader_setup]") {
    for (const bool is_float32 : {false, true}) {
        const u32 words_per_uniform = is_float32 ? 4 : 3;
        for (const u32 start_index : {0u, 1u, 94u, 95u, 96u, 127u}) {
            for (u32 prefix = 0; prefix < words_per_uniform; ++prefix) {
                for (const u32 count :
                     {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 63u}) {
                    for (const bool initially_dirty : {false, true}) {
                        CAPTURE(is_float32, start_index, prefix, count, initially_dirty);
                        CheckFloatUniformRange(is_float32, start_index, prefix, count,
                                               initially_dirty);
                    }
                }
            }
        }
    }
}

TEST_CASE("PICA float32 range rewrite keeps a clean uniform state", "[video_core][shader_setup]") {
    Pica::ShaderSetup setup;
    Pica::ShaderRegs regs{};
    using Format = decltype(regs.uniform_setup)::Format;
    regs.uniform_setup.format.Assign(Format::Float32);
    regs.uniform_setup.index.Assign(7);

    std::array<u32, 32> values{};
    for (u32 i = 0; i < values.size(); ++i) {
        values[i] = FloatUniformWords[i % FloatUniformWords.size()] ^ (i * 0x20408102u);
    }

    const auto first = setup.WriteUniformFloatRegRange(regs, values.data(), values.size());
    REQUIRE(first);
    REQUIRE(first->first_index == 7);
    REQUIRE(first->count == 8);
    REQUIRE(setup.uniforms_dirty);
    REQUIRE(setup.uniform_queue.index == 0);

    setup.uniforms_dirty = false;
    regs.uniform_setup.index.Assign(7);
    const auto second = setup.WriteUniformFloatRegRange(regs, values.data(), values.size());
    REQUIRE(second);
    REQUIRE(second->first_index == 7);
    REQUIRE(second->count == 8);
    REQUIRE_FALSE(setup.uniforms_dirty);
}
