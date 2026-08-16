/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <array>

#include "dynarmic/backend/arm64/address_space.h"
#include "dynarmic/backend/block_range_information.h"
#include "dynarmic/interface/A32/config.h"

namespace Dynarmic::Backend::Arm64 {

struct EmittedBlockInfo;

class A32AddressSpace final : public AddressSpace {
public:
    explicit A32AddressSpace(const A32::UserConfig& conf);

    IR::Block GenerateIR(IR::LocationDescriptor) const override;

    void ClearCache() override;
    void InvalidateCacheRanges(const boost::icl::interval_set<u32>& ranges);

protected:
    friend class A32Core;

    void EmitPrelude();
    EmitConfig GetEmitConfig() override;
    void RegisterNewBasicBlock(const IR::Block& block, const EmittedBlockInfo& block_info) override;

    struct FastDispatchEntry {
        u64 location_descriptor = ~u64{};
        CodePtr code_ptr = nullptr;
    };
    static_assert(sizeof(FastDispatchEntry) == 16);
    static constexpr size_t fast_dispatch_table_size = 1 << 16;

    static size_t FastDispatchIndex(IR::LocationDescriptor descriptor) {
        const u64 value = descriptor.Value();
        return static_cast<size_t>(((value ^ (value >> 32)) >> 1) &
                                   (fast_dispatch_table_size - 1));
    }

    void ClearFastDispatchTable();
    FastDispatchEntry& GetFastDispatchEntry(IR::LocationDescriptor descriptor) {
        return fast_dispatch_table[FastDispatchIndex(descriptor)];
    }

    const A32::UserConfig conf;
    BlockRangeInformation<u32> block_ranges;
    std::array<FastDispatchEntry, fast_dispatch_table_size> fast_dispatch_table;
};

}  // namespace Dynarmic::Backend::Arm64
