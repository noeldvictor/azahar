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
