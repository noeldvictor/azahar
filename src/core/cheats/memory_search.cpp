// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include "core/cheats/memory_search.h"

namespace Cheats {
namespace {

u64 ReadLittleEndian(std::span<const u8> bytes, std::size_t offset, std::size_t value_bytes) {
    u64 value{};
    for (std::size_t byte = 0; byte < value_bytes; ++byte) {
        value |= static_cast<u64>(bytes[offset + byte]) << (byte * 8);
    }
    return value;
}

bool Matches(MemorySearchComparison comparison, u64 previous, u64 current,
             std::optional<u64> exact_value) {
    switch (comparison) {
    case MemorySearchComparison::Exact:
        return exact_value.has_value() && current == *exact_value;
    case MemorySearchComparison::Changed:
        return current != previous;
    case MemorySearchComparison::Unchanged:
        return current == previous;
    case MemorySearchComparison::Increased:
        return current > previous;
    case MemorySearchComparison::Decreased:
        return current < previous;
    }
    return false;
}

} // namespace

std::size_t MemorySearch::ValueBytes(MemorySearchValueSize value_size) {
    switch (value_size) {
    case MemorySearchValueSize::Uint8:
        return 1;
    case MemorySearchValueSize::Uint16:
        return 2;
    case MemorySearchValueSize::Uint32:
        return 4;
    }
    return 0;
}

u64 MemorySearch::ValueMask(MemorySearchValueSize value_size) {
    const std::size_t value_bytes = ValueBytes(value_size);
    if (value_bytes == 0) {
        return 0;
    }
    return (u64{1} << (value_bytes * 8)) - 1;
}

bool MemorySearch::Begin(MemorySearchValueSize new_value_size, u64 exact_value) {
    Reset();
    const u64 mask = ValueMask(new_value_size);
    if (mask == 0 || exact_value > mask) {
        return false;
    }

    value_size = new_value_size;
    initial_value = exact_value;
    active = true;
    return true;
}

MemorySearch::ScanResult MemorySearch::ScanRegion(VAddr base, std::span<const u8> bytes,
                                                   std::size_t candidate_limit) {
    if (!active || candidate_limit == 0) {
        return ScanResult::InvalidValue;
    }

    const std::size_t value_bytes = ValueBytes(value_size);
    const std::size_t first_offset = (value_bytes - (base % value_bytes)) % value_bytes;
    for (std::size_t offset = first_offset; offset + value_bytes <= bytes.size();
         offset += value_bytes) {
        if (ReadLittleEndian(bytes, offset, value_bytes) != initial_value) {
            continue;
        }
        if (candidates.size() == candidate_limit) {
            Reset();
            return ScanResult::TooManyCandidates;
        }
        candidates.push_back(
            {.address = base + static_cast<VAddr>(offset), .value = initial_value});
    }
    return ScanResult::Success;
}

std::size_t MemorySearch::Refine(MemorySearchComparison comparison,
                                 std::optional<u64> exact_value, const Reader& reader) {
    if (!active || !reader) {
        return 0;
    }

    const u64 mask = ValueMask(value_size);
    if (comparison == MemorySearchComparison::Exact &&
        (!exact_value.has_value() || *exact_value > mask)) {
        return candidates.size();
    }

    auto output = candidates.begin();
    for (const MemorySearchCandidate& candidate : candidates) {
        const std::optional<u64> current_value = reader(candidate.address, value_size);
        if (!current_value.has_value()) {
            continue;
        }
        const u64 current = *current_value & mask;
        if (!Matches(comparison, candidate.value, current, exact_value)) {
            continue;
        }
        *output++ = {.address = candidate.address, .value = current};
    }
    candidates.erase(output, candidates.end());
    return candidates.size();
}

void MemorySearch::Reset() {
    candidates.clear();
    active = false;
    initial_value = 0;
}

} // namespace Cheats
