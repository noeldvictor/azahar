// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/memory.h"
#include "video_core/pica/vertex_loader.h"
#include "video_core/pica/vertex_loader_utils.h"

namespace {

u32 NextRandom(u32& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

template <typename T>
void LoadReference(const void* source, u32 elements, Common::Vec4<Pica::f24>& output) {
    const auto* bytes = static_cast<const u8*>(source);
    for (u32 component = 0; component < elements; ++component) {
        T value;
        std::memcpy(&value, bytes + component * sizeof(T), sizeof(T));
        output[component] = Pica::f24::FromFloat32(static_cast<f32>(value));
    }
    for (u32 component = elements; component < 4; ++component) {
        output[component] = component == 3 ? Pica::f24::One() : Pica::f24::Zero();
    }
}

void LoadReference(Pica::PipelineRegs::VertexAttributeFormat format, const void* source,
                   u32 elements, Common::Vec4<Pica::f24>& output) {
    using Format = Pica::PipelineRegs::VertexAttributeFormat;
    switch (format) {
    case Format::BYTE:
        return LoadReference<s8>(source, elements, output);
    case Format::UBYTE:
        return LoadReference<u8>(source, elements, output);
    case Format::SHORT:
        return LoadReference<s16>(source, elements, output);
    case Format::FLOAT:
        return LoadReference<f32>(source, elements, output);
    }
}

void RequireSame(const Common::Vec4<Pica::f24>& expected, const Common::Vec4<Pica::f24>& actual,
                 u32 format, u32 elements, u32 sample) {
    if (std::memcmp(&expected, &actual, sizeof(actual)) != 0) {
        CAPTURE(format, elements, sample);
        FAIL("Predecoded vertex attribute loader changed values or default components");
    }
}

} // Anonymous namespace

TEST_CASE("PICA predecoded vertex loaders match scalar semantics", "[video_core][pica]") {
    using Format = Pica::PipelineRegs::VertexAttributeFormat;
    constexpr std::array formats = {Format::BYTE, Format::UBYTE, Format::SHORT, Format::FLOAT};

    alignas(16) std::array<u8, 16> source{};
    u32 state = 0x243F6A88U;
    for (u32 format_index = 0; format_index < formats.size(); ++format_index) {
        const auto format = formats[format_index];
        for (u32 elements = 1; elements <= 4; ++elements) {
            const auto loader = Pica::VertexLoaderUtils::Decode(format, elements);
            REQUIRE(loader != Pica::VertexLoaderUtils::AttributeLoader::Invalid);

            for (u32 sample = 0; sample < 4096; ++sample) {
                for (u8& byte : source) {
                    byte = static_cast<u8>(NextRandom(state));
                }

                Common::Vec4<Pica::f24> expected;
                Common::Vec4<Pica::f24> actual;
                std::memset(&expected, 0xA5, sizeof(expected));
                std::memset(&actual, 0x5A, sizeof(actual));
                LoadReference(format, source.data(), elements, expected);
                Pica::VertexLoaderUtils::LoadAttribute(loader, source.data(), actual);
                RequireSame(expected, actual, format_index, elements, sample);
            }
        }
    }

    REQUIRE(Pica::VertexLoaderUtils::Decode(Format::BYTE, 0) ==
            Pica::VertexLoaderUtils::AttributeLoader::Invalid);
    REQUIRE(Pica::VertexLoaderUtils::Decode(Format::FLOAT, 5) ==
            Pica::VertexLoaderUtils::AttributeLoader::Invalid);
}

TEST_CASE("PICA vertex loader retains draw-lifetime physical streams", "[video_core][pica]") {
    using Format = Pica::PipelineRegs::VertexAttributeFormat;

    Core::System system;
    Memory::MemorySystem memory{system};
    Pica::PipelineRegs regs{};
    regs.vertex_attributes.base_address.Assign(Memory::FCRAM_PADDR / 16);
    regs.vertex_attributes.format0.Assign(Format::BYTE);
    regs.vertex_attributes.size0.Assign(3);
    regs.vertex_attributes.format1.Assign(Format::SHORT);
    regs.vertex_attributes.size1.Assign(2);
    regs.vertex_attributes.attribute_mask.Assign(1U << 2);
    regs.vertex_attributes.max_attribute_index.Assign(2);

    constexpr u32 data_offset = 0x1000;
    auto& config = regs.vertex_attributes.attribute_loaders[0];
    config.data_offset.Assign(data_offset);
    config.comp0.Assign(0);
    config.comp1.Assign(1);
    config.byte_count.Assign(10);
    config.component_count.Assign(2);

    std::array<u8, 20> source = {
        0x80, 0xFF, 0x00, 0x7F, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x7F,
        0x7E, 0x01, 0xFE, 0x81, 0x01, 0x80, 0x34, 0x12, 0xFF, 0x7F,
    };
    std::memcpy(memory.GetFCRAMPointer(data_offset), source.data(), source.size());

    const Pica::VertexLoader loader{memory, regs};
    REQUIRE(loader.GetNumTotalAttributes() == 3);

    Pica::AttributeBuffer defaults{};
    defaults[2] = {Pica::f24::FromFloat32(3.0f), Pica::f24::FromFloat32(-4.0f),
                   Pica::f24::FromFloat32(5.0f), Pica::f24::FromFloat32(-6.0f)};

    for (u32 vertex = 0; vertex < 2; ++vertex) {
        if (vertex == 1) {
            // The cached host pointer must still observe guest writes made after construction.
            source[10] = 0x81;
            source[14] = 0x02;
            std::memcpy(memory.GetFCRAMPointer(data_offset + 10), source.data() + 10, 10);
        }

        Pica::AttributeBuffer input{};
        loader.LoadVertex(vertex, input, defaults);

        Common::Vec4<Pica::f24> expected_byte;
        Common::Vec4<Pica::f24> expected_short;
        LoadReference<s8>(source.data() + vertex * 10, 4, expected_byte);
        LoadReference<s16>(source.data() + vertex * 10 + 4, 3, expected_short);
        RequireSame(expected_byte, input[0], 0, 4, vertex);
        RequireSame(expected_short, input[1], 2, 3, vertex);
        REQUIRE(std::memcmp(&defaults[2], &input[2], sizeof(input[2])) == 0);
    }
}
