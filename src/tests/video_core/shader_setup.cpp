// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <catch2/catch_test_macros.hpp>
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
