/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

namespace Dynarmic::A32 {
struct UserCallbacks;
}

namespace Dynarmic::A64 {
struct UserCallbacks;
struct UserConfig;
}  // namespace Dynarmic::A64

namespace Dynarmic::IR {
class Block;
}

namespace Dynarmic::Optimization {

struct PolyfillOptions {
    bool pack_halfword = false;
    bool sha256 = false;
    bool vector_absolute_difference_widen = false;
    bool vector_add_sub_widen = false;
    bool vector_multiply_accumulate_widen = false;
    bool vector_multiply_widen = false;
    bool vector_rounding_narrow = false;
    bool vector_rounding_shift_right = false;
    bool vector_shift_insert = false;

    bool operator==(const PolyfillOptions&) const = default;
};

struct A32GetSetEliminationOptions {
    bool convert_nzc_to_nz = false;
    bool convert_nz_to_nzc = false;
};

void PolyfillPass(IR::Block& block, const PolyfillOptions& opt);
void A32ConstantMemoryReads(IR::Block& block, A32::UserCallbacks* cb);
void A32GetSetElimination(IR::Block& block, A32GetSetEliminationOptions opt);
void A64CallbackConfigPass(IR::Block& block, const A64::UserConfig& conf);
void A64GetSetElimination(IR::Block& block);
void A64MergeInterpretBlocksPass(IR::Block& block, A64::UserCallbacks* cb);
void ConstantPropagation(IR::Block& block);
void DeadCodeElimination(IR::Block& block);
void IdentityRemovalPass(IR::Block& block);
void VerificationPass(const IR::Block& block);
void NamingPass(IR::Block& block);

}  // namespace Dynarmic::Optimization
