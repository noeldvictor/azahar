// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <string>
#include <string_view>
#include <catch2/catch_test_macros.hpp>
#include "core/cheats/gateway_cheat.h"

TEST_CASE("Gateway cheat validation accepts every supported opcode family", "[core][cheats]") {
    constexpr std::array valid_lines{
        std::string_view{"00100000 12345678"}, std::string_view{"C0000000 00000001"},
        std::string_view{"D0000000 00000000"}, std::string_view{"DD000000 00000001"},
        std::string_view{"E0100000 00000004"},
    };

    for (const std::string_view line : valid_lines) {
        INFO(line);
        CHECK(Cheats::GatewayCheat::CheatLine(std::string{line}).valid);
    }
}

TEST_CASE("Gateway cheat validation rejects unsupported and malformed lines", "[core][cheats]") {
    CHECK_FALSE(Cheats::GatewayCheat::CheatLine("F0100000 00000000").valid);
    CHECK_FALSE(Cheats::GatewayCheat::CheatLine("DE000000 00000000").valid);
    CHECK_FALSE(Cheats::GatewayCheat::CheatLine("00100000 NOTHEX00").valid);
    CHECK_FALSE(Cheats::GatewayCheat::CheatLine("00100000 0000000").valid);
}

TEST_CASE("Gateway cheats with invalid lines cannot be enabled", "[core][cheats]") {
    Cheats::GatewayCheat cheat{"Invalid test", "F0100000 00000000", "test only"};
    cheat.SetEnabled(true);
    CHECK_FALSE(cheat.IsEnabled());
}
