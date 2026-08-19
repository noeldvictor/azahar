// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "video_core/pica/output_vertex.h"
#include "video_core/pica/regs_rasterizer.h"

namespace {

Pica::AttributeBuffer MakeOutputAttributes() {
    Pica::AttributeBuffer output{};
    for (std::size_t attribute = 0; attribute < output.size(); ++attribute) {
        for (std::size_t component = 0; component < 4; ++component) {
            const float value = static_cast<float>(attribute * 4 + component) * 0.125f - 2.0f;
            output[attribute][component] = Pica::f24::FromFloat32(value);
        }
    }
    output[3][0] = Pica::f24::FromFloat32(-0.0f);
    output[3][1] = Pica::f24::FromFloat32(-0.5f);
    output[3][2] = Pica::f24::FromFloat32(0.75f);
    output[3][3] = Pica::f24::FromFloat32(2.0f);
    return output;
}

Pica::OutputVertex MakeReference(const Pica::RasterizerRegs& regs,
                                 const Pica::AttributeBuffer& output) {
    std::array<Pica::f24, 32> overflow;
    overflow.fill(Pica::f24::One());

    const u32 count = regs.vs_output_total & 7;
    for (std::size_t attribute = 0; attribute < count; ++attribute) {
        const auto map = regs.vs_output_attributes[attribute];
        overflow[map.map_x] = output[attribute][0];
        overflow[map.map_y] = output[attribute][1];
        overflow[map.map_z] = output[attribute][2];
        overflow[map.map_w] = output[attribute][3];
    }

    Pica::OutputVertex vertex;
    std::memcpy(&vertex, overflow.data(), sizeof(vertex));
    for (u32 component = 0; component < 4; ++component) {
        const f32 color = std::fabs(vertex.color[component].ToFloat32());
        vertex.color[component] = Pica::f24::FromFloat32(color < 1.0f ? color : 1.0f);
    }
    return vertex;
}

u32 NextRandom(u32& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

void RandomizeMaps(Pica::RasterizerRegs& regs, u32& state) {
    for (auto& map : regs.vs_output_attributes) {
        map.raw = 0;
        for (u32 component = 0; component < 4; ++component) {
            map.raw |= ((NextRandom(state) >> 27) & 31) << (component * 8);
        }
    }
}

void RequireSameVertex(const Pica::OutputVertex& expected, const Pica::OutputVertex& actual,
                       u32 count, u32 sample) {
    if (std::memcmp(&expected, &actual, sizeof(actual)) != 0) {
        CAPTURE(count, sample);
        FAIL("OutputVertex changed overflow-map, default-slot, or color-clamp semantics");
    }
}

} // Anonymous namespace

TEST_CASE("PICA output vertex matches 32-slot overflow semantics", "[video_core]") {
    const auto output = MakeOutputAttributes();
    u32 state = 0x12345678U;

    for (u32 count = 0; count <= 7; ++count) {
        for (u32 sample = 0; sample < 4096; ++sample) {
            Pica::RasterizerRegs regs{};
            regs.vs_output_total.Assign(count);
            RandomizeMaps(regs, state);

            const auto expected = MakeReference(regs, output);
            const Pica::OutputVertex actual(regs, output);
            RequireSameVertex(expected, actual, count, sample);
        }
    }
}

#if defined(__aarch64__)
TEST_CASE("PICA six-output AArch64 constructor matches generic bytes", "[video_core]") {
    const auto output = MakeOutputAttributes();
    u32 state = 0x6A09E667U;

    for (u32 sample = 0; sample < 100000; ++sample) {
        Pica::RasterizerRegs regs{};
        regs.vs_output_total.Assign(6);
        RandomizeMaps(regs, state);

        const Pica::OutputVertex expected(regs, output);
        const Pica::OutputVertex actual(Pica::OutputVertex::SixAttributes{}, regs, output);
        RequireSameVertex(expected, actual, 6, sample);
    }
}
#endif
