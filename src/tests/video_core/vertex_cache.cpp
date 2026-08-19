// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/pica/vertex_cache.h"

namespace {

u32 FindVertexScalar(const u16* ids, u32 count, u16 vertex) {
    for (u32 index = 0; index < count; ++index) {
        if (ids[index] == vertex) {
            return index;
        }
    }
    return count;
}

} // Anonymous namespace

TEST_CASE("PICA vertex cache lookup matches scalar first-match semantics", "[video_core]") {
    std::array<u16, 64> ids{};
    for (u32 index = 0; index < ids.size(); ++index) {
        // An odd multiplier makes these IDs unique modulo 2^16 for this range.
        ids[index] = static_cast<u16>(index * 997u + 12345u);
    }

    for (u32 count = 0; count <= ids.size(); ++count) {
        for (u32 vertex = 0; vertex <= std::numeric_limits<u16>::max(); ++vertex) {
            const u16 id = static_cast<u16>(vertex);
            const u32 expected = FindVertexScalar(ids.data(), count, id);
            const u32 actual = Pica::VertexCacheUtils::FindVertex(ids.data(), count, id);
            if (actual != expected) {
                CAPTURE(count, vertex, actual, expected);
                FAIL("Vertex-cache lookup changed scalar match semantics");
            }
        }
    }

    constexpr std::array<u16, 20> duplicate_ids = {
        9, 4, 7, 11, 23, 42, 8, 13, 15, 42, 16, 21, 1, 42, 18, 25, 3, 42, 6, 12,
    };
    constexpr u32 duplicate_count = static_cast<u32>(duplicate_ids.size());
    REQUIRE(Pica::VertexCacheUtils::FindVertex(duplicate_ids.data(), duplicate_count, 42) == 5);
    REQUIRE(Pica::VertexCacheUtils::FindVertex(duplicate_ids.data(), duplicate_count, 99) ==
            duplicate_count);
}

TEST_CASE("PICA vertex cache direct-output gate preserves observable prefixes", "[video_core]") {
    Pica::AttributeBuffer source{};
    Pica::AttributeBuffer output_initial{};
    Pica::AttributeBuffer cache_initial{};
    auto* source_bytes = reinterpret_cast<u8*>(source.data());
    auto* output_initial_bytes = reinterpret_cast<u8*>(output_initial.data());
    auto* cache_initial_bytes = reinterpret_cast<u8*>(cache_initial.data());
    for (u32 byte = 0; byte < sizeof(source); ++byte) {
        source_bytes[byte] = static_cast<u8>(byte * 37 + 11);
        output_initial_bytes[byte] = static_cast<u8>(byte * 53 + 19);
        cache_initial_bytes[byte] = static_cast<u8>(byte * 71 + 29);
    }

    for (u32 produced = 0; produced <= source.size(); ++produced) {
        for (u32 consumed = 0; consumed <= source.size(); ++consumed) {
            CAPTURE(produced, consumed);
            REQUIRE(Pica::VertexCacheUtils::CanUseDirectVertexCache(true, produced, consumed) ==
                    (produced == consumed));
            REQUIRE_FALSE(
                Pica::VertexCacheUtils::CanUseDirectVertexCache(false, produced, consumed));
        }

        Pica::AttributeBuffer previous_output = output_initial;
        Pica::AttributeBuffer direct_cache = cache_initial;
        std::copy_n(source.begin(), produced, previous_output.begin());
        std::copy_n(source.begin(), produced, direct_cache.begin());

        CAPTURE(produced);
        REQUIRE(std::memcmp(previous_output.data(), direct_cache.data(),
                            produced * sizeof(source[0])) == 0);
        REQUIRE(std::memcmp(direct_cache.data() + produced, cache_initial.data() + produced,
                            (source.size() - produced) * sizeof(source[0])) == 0);
    }
}

TEST_CASE("PICA packed shader input map preserves register assignment semantics", "[video_core]") {
    constexpr u32 mapping_seeds = 32;

    Pica::AttributeBuffer source{};
    auto* source_bytes = reinterpret_cast<u8*>(source.data());
    for (u32 byte = 0; byte < sizeof(source); ++byte) {
        source_bytes[byte] = static_cast<u8>(byte * 43 + 17);
    }

    for (u32 count = 1; count <= source.size(); ++count) {
        for (u32 seed = 0; seed < mapping_seeds; ++seed) {
            Pica::ShaderRegs config{};
            config.max_input_attribute_index.Assign(count - 1);

            u64 packed_map{};
            u32 random = seed * 0x9E3779B9U + count;
            for (u32 attribute = 0; attribute < count; ++attribute) {
                random = random * 1664525U + 1013904223U;
                const u32 reg = seed == 0   ? attribute
                                : seed == 1 ? 15 - attribute
                                : seed == 2 ? 7
                                            : random >> 28;
                packed_map |= static_cast<u64>(reg) << (attribute * 4);
            }
            config.input_attribute_to_register_map_low = static_cast<u32>(packed_map);
            config.input_attribute_to_register_map_high = static_cast<u32>(packed_map >> 32);

            Pica::ShaderUnit reference;
            Pica::ShaderUnit packed;
            auto* reference_bytes = reinterpret_cast<u8*>(reference.input.data());
            auto* packed_bytes = reinterpret_cast<u8*>(packed.input.data());
            for (u32 byte = 0; byte < sizeof(reference.input); ++byte) {
                reference_bytes[byte] = static_cast<u8>(byte * 61 + seed * 13 + count);
                packed_bytes[byte] = reference_bytes[byte];
            }

            reference.LoadInput(config, source);
            const Pica::ShaderInputMap input_map{config};
            packed.LoadInput(input_map, source);

            CAPTURE(count, seed, packed_map);
            REQUIRE(std::memcmp(reference.input.data(), packed.input.data(),
                                sizeof(reference.input)) == 0);
        }
    }
}
