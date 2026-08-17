// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <catch2/catch_test_macros.hpp>
#include "common/math_util.h"

namespace {

template <typename T, size_t Size>
void CheckFindMinMaxPrefixes(const std::array<T, Size>& data) {
    for (size_t count = 0; count <= data.size(); ++count) {
        INFO("count = " << count);
        const std::span<const T> prefix{data.data(), count};
        const auto result = Common::FindMinMax(prefix);

        if (prefix.empty()) {
            REQUIRE(result.first == std::numeric_limits<T>::max());
            REQUIRE(result.second == 0);
            continue;
        }

        const auto [expected_min, expected_max] = std::minmax_element(prefix.begin(), prefix.end());
        REQUIRE(result.first == *expected_min);
        REQUIRE(result.second == *expected_max);
    }
}

} // Anonymous namespace

TEST_CASE("FindMinMax handles scalar, SIMD, and tail sizes", "[common]") {
    std::array<u8, 145> byte_indices{};
    for (size_t i = 0; i < byte_indices.size(); ++i) {
        byte_indices[i] = static_cast<u8>((i * 73 + 19) & 0xFF);
    }
    byte_indices[63] = std::numeric_limits<u8>::max();
    byte_indices[64] = std::numeric_limits<u8>::min();
    byte_indices[127] = std::numeric_limits<u8>::min();
    byte_indices[128] = std::numeric_limits<u8>::max();
    CheckFindMinMaxPrefixes(byte_indices);

    std::array<u16, 73> short_indices{};
    for (size_t i = 0; i < short_indices.size(); ++i) {
        short_indices[i] = static_cast<u16>((i * 7919 + 1009) & 0xFFFF);
    }
    short_indices[31] = std::numeric_limits<u16>::max();
    short_indices[32] = std::numeric_limits<u16>::min();
    short_indices[63] = std::numeric_limits<u16>::min();
    short_indices[64] = std::numeric_limits<u16>::max();
    CheckFindMinMaxPrefixes(short_indices);
}
