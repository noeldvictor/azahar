// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <functional>
#include <optional>
#include <span>
#include <vector>
#include "common/common_types.h"

namespace Cheats {

enum class MemorySearchValueSize : u8 {
    Uint8 = 1,
    Uint16 = 2,
    Uint32 = 4,
};

enum class MemorySearchComparison : u8 {
    Exact,
    Changed,
    Unchanged,
    Increased,
    Decreased,
};

struct MemorySearchCandidate {
    VAddr address{};
    u64 value{};
};

class MemorySearch {
public:
    enum class ScanResult {
        Success,
        InvalidValue,
        TooManyCandidates,
    };

    using Reader = std::function<std::optional<u64>(VAddr, MemorySearchValueSize)>;

    bool Begin(MemorySearchValueSize value_size, u64 exact_value);
    ScanResult ScanRegion(VAddr base, std::span<const u8> bytes, std::size_t candidate_limit);
    std::size_t Refine(MemorySearchComparison comparison, std::optional<u64> exact_value,
                       const Reader& reader);
    void Reset();

    [[nodiscard]] bool IsActive() const {
        return active;
    }

    [[nodiscard]] MemorySearchValueSize ValueSize() const {
        return value_size;
    }

    [[nodiscard]] std::span<const MemorySearchCandidate> Candidates() const {
        return candidates;
    }

    [[nodiscard]] static std::size_t ValueBytes(MemorySearchValueSize value_size);
    [[nodiscard]] static u64 ValueMask(MemorySearchValueSize value_size);

private:
    MemorySearchValueSize value_size{MemorySearchValueSize::Uint32};
    u64 initial_value{};
    std::vector<MemorySearchCandidate> candidates;
    bool active{};
};

} // namespace Cheats
