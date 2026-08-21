// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <array>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>
#include "core/cheats/memory_search.h"

TEST_CASE("Memory search reports supported unsigned widths", "[core][cheats]") {
    CHECK(Cheats::MemorySearch::ValueBytes(Cheats::MemorySearchValueSize::Uint8) == 1);
    CHECK(Cheats::MemorySearch::ValueBytes(Cheats::MemorySearchValueSize::Uint16) == 2);
    CHECK(Cheats::MemorySearch::ValueBytes(Cheats::MemorySearchValueSize::Uint32) == 4);
    CHECK(Cheats::MemorySearch::ValueMask(Cheats::MemorySearchValueSize::Uint8) == 0xFF);
    CHECK(Cheats::MemorySearch::ValueMask(Cheats::MemorySearchValueSize::Uint16) == 0xFFFF);
    CHECK(Cheats::MemorySearch::ValueMask(Cheats::MemorySearchValueSize::Uint32) == 0xFFFFFFFF);

    const auto unsupported = static_cast<Cheats::MemorySearchValueSize>(3);
    CHECK(Cheats::MemorySearch::ValueBytes(unsupported) == 0);
    CHECK(Cheats::MemorySearch::ValueMask(unsupported) == 0);
}

TEST_CASE("Memory search finds aligned little-endian values", "[core][cheats]") {
    Cheats::MemorySearch search;
    const std::array<u8, 12> bytes{0x34, 0x12, 0x34, 0x12, 0x78, 0x56,
                                   0x34, 0x12, 0x34, 0x12, 0x00, 0x00};

    REQUIRE(search.Begin(Cheats::MemorySearchValueSize::Uint16, 0x1234));
    REQUIRE(search.ScanRegion(0x08000000, bytes, 16) ==
            Cheats::MemorySearch::ScanResult::Success);

    const auto candidates = search.Candidates();
    REQUIRE(candidates.size() == 4);
    CHECK(candidates[0].address == 0x08000000);
    CHECK(candidates[1].address == 0x08000002);
    CHECK(candidates[2].address == 0x08000006);
    CHECK(candidates[3].address == 0x08000008);
}

TEST_CASE("Memory search aligns candidates by guest address", "[core][cheats]") {
    Cheats::MemorySearch search;
    const std::array<u8, 6> bytes{0xFF, 0x34, 0x12, 0x34, 0x12, 0xFF};

    REQUIRE(search.Begin(Cheats::MemorySearchValueSize::Uint16, 0x1234));
    REQUIRE(search.ScanRegion(0x08000001, bytes, 16) ==
            Cheats::MemorySearch::ScanResult::Success);

    const auto candidates = search.Candidates();
    REQUIRE(candidates.size() == 2);
    CHECK(candidates[0].address == 0x08000002);
    CHECK(candidates[1].address == 0x08000004);
}

TEST_CASE("Memory search rejects invalid values and candidate overflow", "[core][cheats]") {
    Cheats::MemorySearch search;
    const std::array<u8, 4> bytes{7, 7, 7, 7};

    CHECK_FALSE(search.Begin(Cheats::MemorySearchValueSize::Uint8, 256));
    REQUIRE(search.Begin(Cheats::MemorySearchValueSize::Uint8, 7));
    CHECK(search.ScanRegion(0x08000000, bytes, 3) ==
          Cheats::MemorySearch::ScanResult::TooManyCandidates);
    CHECK_FALSE(search.IsActive());
    CHECK(search.Candidates().empty());
}

TEST_CASE("Memory search refinements compare against the last paused snapshot", "[core][cheats]") {
    Cheats::MemorySearch search;
    const std::array<u8, 16> bytes{10, 0, 0, 0, 20, 0, 0, 0,
                                  20, 0, 0, 0, 30, 0, 0, 0};
    REQUIRE(search.Begin(Cheats::MemorySearchValueSize::Uint32, 20));
    REQUIRE(search.ScanRegion(0x08000000, bytes, 16) ==
            Cheats::MemorySearch::ScanResult::Success);

    std::unordered_map<VAddr, u64> values{{0x08000004, 25}, {0x08000008, 20}};
    const auto reader = [&values](VAddr address, Cheats::MemorySearchValueSize) {
        const auto it = values.find(address);
        return it == values.end() ? std::optional<u64>{} : std::optional<u64>{it->second};
    };

    CHECK(search.Refine(Cheats::MemorySearchComparison::Increased, std::nullopt, reader) == 1);
    REQUIRE(search.Candidates().size() == 1);
    CHECK(search.Candidates()[0].address == 0x08000004);
    CHECK(search.Candidates()[0].value == 25);

    values[0x08000004] = 25;
    CHECK(search.Refine(Cheats::MemorySearchComparison::Unchanged, std::nullopt, reader) == 1);
    values[0x08000004] = 12;
    CHECK(search.Refine(Cheats::MemorySearchComparison::Exact, 12, reader) == 1);
    CHECK(search.Candidates()[0].value == 12);
}

TEST_CASE("Memory search drops unreadable candidates and advances surviving snapshots",
          "[core][cheats]") {
    Cheats::MemorySearch search;
    const std::array<u8, 3> bytes{5, 5, 5};
    REQUIRE(search.Begin(Cheats::MemorySearchValueSize::Uint8, 5));
    REQUIRE(search.ScanRegion(0x08000000, bytes, 16) ==
            Cheats::MemorySearch::ScanResult::Success);

    std::unordered_map<VAddr, u64> values{{0x08000000, 6}, {0x08000002, 4}};
    const auto reader = [&values](VAddr address, Cheats::MemorySearchValueSize) {
        const auto it = values.find(address);
        return it == values.end() ? std::optional<u64>{} : std::optional<u64>{it->second};
    };

    CHECK(search.Refine(Cheats::MemorySearchComparison::Changed, std::nullopt, reader) == 2);
    REQUIRE(search.Candidates().size() == 2);
    CHECK(search.Candidates()[0].value == 6);
    CHECK(search.Candidates()[1].value == 4);

    values[0x08000000] = 7;
    values[0x08000002] = 3;
    CHECK(search.Refine(Cheats::MemorySearchComparison::Increased, std::nullopt, reader) == 1);
    REQUIRE(search.Candidates().size() == 1);
    CHECK(search.Candidates()[0].address == 0x08000000);
    CHECK(search.Candidates()[0].value == 7);
}
