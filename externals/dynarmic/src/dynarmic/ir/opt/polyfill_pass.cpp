/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/ir_emitter.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"
#include "dynarmic/ir/opt/passes.h"

namespace Dynarmic::Optimization {

namespace {

void PolyfillSHA256MessageSchedule0(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 x = (IR::U128)inst.GetArg(0);
    const IR::U128 y = (IR::U128)inst.GetArg(1);

    const IR::U128 t = ir.VectorExtract(x, y, 32);

    IR::U128 result = ir.ZeroVector();
    for (size_t i = 0; i < 4; i++) {
        const IR::U32 modified_element = [&] {
            const IR::U32 element = ir.VectorGetElement(32, t, i);
            const IR::U32 tmp1 = ir.RotateRight(element, ir.Imm8(7));
            const IR::U32 tmp2 = ir.RotateRight(element, ir.Imm8(18));
            const IR::U32 tmp3 = ir.LogicalShiftRight(element, ir.Imm8(3));

            return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
        }();

        result = ir.VectorSetElement(32, result, i, modified_element);
    }
    result = ir.VectorAdd(32, result, x);

    inst.ReplaceUsesWith(result);
}

void PolyfillSHA256MessageSchedule1(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 x = (IR::U128)inst.GetArg(0);
    const IR::U128 y = (IR::U128)inst.GetArg(1);
    const IR::U128 z = (IR::U128)inst.GetArg(2);

    const IR::U128 T0 = ir.VectorExtract(y, z, 32);

    const IR::U128 lower_half = [&] {
        const IR::U128 T = ir.VectorRotateWholeVectorRight(z, 64);
        const IR::U128 tmp1 = ir.VectorRotateRight(32, T, 17);
        const IR::U128 tmp2 = ir.VectorRotateRight(32, T, 19);
        const IR::U128 tmp3 = ir.VectorLogicalShiftRight(32, T, 10);
        const IR::U128 tmp4 = ir.VectorEor(tmp1, ir.VectorEor(tmp2, tmp3));
        const IR::U128 tmp5 = ir.VectorAdd(32, tmp4, ir.VectorAdd(32, x, T0));
        return ir.VectorZeroUpper(tmp5);
    }();

    const IR::U64 upper_half = [&] {
        const IR::U128 tmp1 = ir.VectorRotateRight(32, lower_half, 17);
        const IR::U128 tmp2 = ir.VectorRotateRight(32, lower_half, 19);
        const IR::U128 tmp3 = ir.VectorLogicalShiftRight(32, lower_half, 10);
        const IR::U128 tmp4 = ir.VectorEor(tmp1, ir.VectorEor(tmp2, tmp3));

        // Shuffle the top two 32-bit elements downwards [3, 2, 1, 0] -> [1, 0, 3, 2]
        const IR::U128 shuffled_d = ir.VectorRotateWholeVectorRight(x, 64);
        const IR::U128 shuffled_T0 = ir.VectorRotateWholeVectorRight(T0, 64);

        const IR::U128 tmp5 = ir.VectorAdd(32, tmp4, ir.VectorAdd(32, shuffled_d, shuffled_T0));
        return ir.VectorGetElement(64, tmp5, 0);
    }();

    const IR::U128 result = ir.VectorSetElement(64, lower_half, 1, upper_half);

    inst.ReplaceUsesWith(result);
}

IR::U32 SHAchoose(IR::IREmitter& ir, IR::U32 x, IR::U32 y, IR::U32 z) {
    return ir.Eor(ir.And(ir.Eor(y, z), x), z);
}

IR::U32 SHAmajority(IR::IREmitter& ir, IR::U32 x, IR::U32 y, IR::U32 z) {
    return ir.Or(ir.And(x, y), ir.And(ir.Or(x, y), z));
}

IR::U32 SHAhashSIGMA0(IR::IREmitter& ir, IR::U32 x) {
    const IR::U32 tmp1 = ir.RotateRight(x, ir.Imm8(2));
    const IR::U32 tmp2 = ir.RotateRight(x, ir.Imm8(13));
    const IR::U32 tmp3 = ir.RotateRight(x, ir.Imm8(22));

    return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
}

IR::U32 SHAhashSIGMA1(IR::IREmitter& ir, IR::U32 x) {
    const IR::U32 tmp1 = ir.RotateRight(x, ir.Imm8(6));
    const IR::U32 tmp2 = ir.RotateRight(x, ir.Imm8(11));
    const IR::U32 tmp3 = ir.RotateRight(x, ir.Imm8(25));

    return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
}

void PolyfillSHA256Hash(IR::IREmitter& ir, IR::Inst& inst) {
    IR::U128 x = (IR::U128)inst.GetArg(0);
    IR::U128 y = (IR::U128)inst.GetArg(1);
    const IR::U128 w = (IR::U128)inst.GetArg(2);
    const bool part1 = inst.GetArg(3).GetU1();

    for (size_t i = 0; i < 4; i++) {
        const IR::U32 low_x = ir.VectorGetElement(32, x, 0);
        const IR::U32 after_low_x = ir.VectorGetElement(32, x, 1);
        const IR::U32 before_high_x = ir.VectorGetElement(32, x, 2);
        const IR::U32 high_x = ir.VectorGetElement(32, x, 3);

        const IR::U32 low_y = ir.VectorGetElement(32, y, 0);
        const IR::U32 after_low_y = ir.VectorGetElement(32, y, 1);
        const IR::U32 before_high_y = ir.VectorGetElement(32, y, 2);
        const IR::U32 high_y = ir.VectorGetElement(32, y, 3);

        const IR::U32 choice = SHAchoose(ir, low_y, after_low_y, before_high_y);
        const IR::U32 majority = SHAmajority(ir, low_x, after_low_x, before_high_x);

        const IR::U32 t = [&] {
            const IR::U32 w_element = ir.VectorGetElement(32, w, i);
            const IR::U32 sig = SHAhashSIGMA1(ir, low_y);

            return ir.Add(high_y, ir.Add(sig, ir.Add(choice, w_element)));
        }();

        const IR::U32 new_low_x = ir.Add(t, ir.Add(SHAhashSIGMA0(ir, low_x), majority));
        const IR::U32 new_low_y = ir.Add(t, high_x);

        // Shuffle all words left by 1 element: [3, 2, 1, 0] -> [2, 1, 0, 3]
        const IR::U128 shuffled_x = ir.VectorRotateWholeVectorRight(x, 96);
        const IR::U128 shuffled_y = ir.VectorRotateWholeVectorRight(y, 96);

        x = ir.VectorSetElement(32, shuffled_x, 0, new_low_x);
        y = ir.VectorSetElement(32, shuffled_y, 0, new_low_y);
    }

    inst.ReplaceUsesWith(part1 ? x : y);
}

template<size_t esize, bool is_signed>
void PolyfillVectorAbsoluteDifferenceWiden(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 n = (IR::U128)inst.GetArg(0);
    const IR::U128 m = (IR::U128)inst.GetArg(1);

    const IR::U128 difference = is_signed ? ir.VectorSignedAbsoluteDifference(esize, n, m)
                                          : ir.VectorUnsignedAbsoluteDifference(esize, n, m);
    const IR::U128 result = ir.VectorZeroExtend(esize, difference);

    inst.ReplaceUsesWith(result);
}

template<size_t esize, bool is_signed, bool is_wide>
void PolyfillVectorAddSubWiden(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 n = (IR::U128)inst.GetArg(0);
    const IR::U128 m = (IR::U128)inst.GetArg(1);
    const IR::U128 wide_n = is_wide ? n
                                    : (is_signed ? ir.VectorSignExtend(esize, n) : ir.VectorZeroExtend(esize, n));
    const IR::U128 wide_m = is_signed ? ir.VectorSignExtend(esize, m) : ir.VectorZeroExtend(esize, m);
    const bool subtract = inst.GetArg(2).GetU1();
    const IR::U128 result = subtract ? ir.VectorSub(esize * 2, wide_n, wide_m)
                                     : ir.VectorAdd(esize * 2, wide_n, wide_m);

    inst.ReplaceUsesWith(result);
}

template<size_t esize, bool is_signed>
void PolyfillVectorMultiplyWiden(IR::IREmitter& ir, IR::Inst& inst) {
    IR::U128 n = (IR::U128)inst.GetArg(0);
    IR::U128 m = (IR::U128)inst.GetArg(1);

    const IR::U128 wide_n = is_signed ? ir.VectorSignExtend(esize, n) : ir.VectorZeroExtend(esize, n);
    const IR::U128 wide_m = is_signed ? ir.VectorSignExtend(esize, m) : ir.VectorZeroExtend(esize, m);

    const IR::U128 result = ir.VectorMultiply(esize * 2, wide_n, wide_m);

    inst.ReplaceUsesWith(result);
}

template<size_t esize, bool is_signed>
void PolyfillVectorMultiplyAccumulateWiden(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 accumulator = (IR::U128)inst.GetArg(0);
    const IR::U128 n = (IR::U128)inst.GetArg(1);
    const IR::U128 m = (IR::U128)inst.GetArg(2);
    const IR::U128 wide_n = is_signed ? ir.VectorSignExtend(esize, n) : ir.VectorZeroExtend(esize, n);
    const IR::U128 wide_m = is_signed ? ir.VectorSignExtend(esize, m) : ir.VectorZeroExtend(esize, m);
    const IR::U128 product = ir.VectorMultiply(esize * 2, wide_n, wide_m);
    const bool subtract = inst.GetArg(3).GetU1();
    const IR::U128 result = subtract ? ir.VectorSub(esize * 2, accumulator, product)
                                     : ir.VectorAdd(esize * 2, accumulator, product);

    inst.ReplaceUsesWith(result);
}

enum class RoundingNarrowKind {
    Truncate,
    SignedSaturatedToSigned,
    SignedSaturatedToUnsigned,
    UnsignedSaturated,
};

template<size_t source_size, RoundingNarrowKind kind>
void PolyfillVectorRoundingNarrow(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 operand = (IR::U128)inst.GetArg(0);
    const u8 shift_amount = inst.GetArg(1).GetU8();
    const IR::U128 shifted = [&] {
        if constexpr (kind == RoundingNarrowKind::SignedSaturatedToSigned || kind == RoundingNarrowKind::SignedSaturatedToUnsigned) {
            return ir.VectorArithmeticShiftRight(source_size, operand, shift_amount);
        } else {
            return ir.VectorLogicalShiftRight(source_size, operand, shift_amount);
        }
    }();
    const IR::U128 round_const = [&] {
        const u64 value = 1ULL << (shift_amount - 1);
        if constexpr (source_size == 16) {
            return ir.VectorBroadcast(16, ir.Imm16(static_cast<u16>(value)));
        } else if constexpr (source_size == 32) {
            return ir.VectorBroadcast(32, ir.Imm32(static_cast<u32>(value)));
        } else {
            return ir.VectorBroadcast(64, ir.Imm64(value));
        }
    }();
    const IR::U128 round_correction = ir.VectorEqual(source_size, ir.VectorAnd(operand, round_const), round_const);
    const IR::U128 rounded = ir.VectorSub(source_size, shifted, round_correction);
    const IR::U128 result = [&] {
        if constexpr (kind == RoundingNarrowKind::Truncate) {
            return ir.VectorNarrow(source_size, rounded);
        } else if constexpr (kind == RoundingNarrowKind::SignedSaturatedToSigned) {
            return ir.VectorSignedSaturatedNarrowToSigned(source_size, rounded);
        } else if constexpr (kind == RoundingNarrowKind::SignedSaturatedToUnsigned) {
            return ir.VectorSignedSaturatedNarrowToUnsigned(source_size, rounded);
        } else {
            return ir.VectorUnsignedSaturatedNarrow(source_size, rounded);
        }
    }();

    inst.ReplaceUsesWith(result);
}

template<size_t esize, bool is_signed, bool accumulate>
void PolyfillVectorRoundingShiftRight(IR::IREmitter& ir, IR::Inst& inst) {
    const size_t operand_index = accumulate ? 1 : 0;
    const size_t shift_index = accumulate ? 2 : 1;
    const IR::U128 operand = (IR::U128)inst.GetArg(operand_index);
    const u8 shift_amount = inst.GetArg(shift_index).GetU8();
    const IR::U128 shifted = is_signed ? ir.VectorArithmeticShiftRight(esize, operand, shift_amount)
                                       : ir.VectorLogicalShiftRight(esize, operand, shift_amount);
    const IR::U128 round_const = [&] {
        const u64 value = 1ULL << (shift_amount - 1);
        if constexpr (esize == 8) {
            return ir.VectorBroadcast(8, ir.Imm8(static_cast<u8>(value)));
        } else if constexpr (esize == 16) {
            return ir.VectorBroadcast(16, ir.Imm16(static_cast<u16>(value)));
        } else if constexpr (esize == 32) {
            return ir.VectorBroadcast(32, ir.Imm32(static_cast<u32>(value)));
        } else {
            return ir.VectorBroadcast(64, ir.Imm64(value));
        }
    }();
    const IR::U128 round_correction = ir.VectorEqual(esize, ir.VectorAnd(operand, round_const), round_const);
    const IR::U128 rounded = ir.VectorSub(esize, shifted, round_correction);
    const IR::U128 result = [&] {
        if constexpr (accumulate) {
            const IR::U128 accumulator = (IR::U128)inst.GetArg(0);
            return ir.VectorAdd(esize, accumulator, rounded);
        } else {
            return rounded;
        }
    }();

    inst.ReplaceUsesWith(result);
}

template<size_t esize, bool shift_left>
void PolyfillVectorShiftInsert(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 destination = (IR::U128)inst.GetArg(0);
    const IR::U128 source = (IR::U128)inst.GetArg(1);
    const u8 shift_amount = inst.GetArg(2).GetU8();
    constexpr u64 element_mask = ~u64{0} >> (64 - esize);
    const u64 mask = [&] {
        if constexpr (shift_left) {
            return element_mask << shift_amount;
        } else {
            return shift_amount == esize ? u64{0} : element_mask >> shift_amount;
        }
    }();
    const IR::U128 shifted = shift_left ? ir.VectorLogicalShiftLeft(esize, source, shift_amount)
                                        : ir.VectorLogicalShiftRight(esize, source, shift_amount);
    const IR::U128 mask_vector = [&] {
        if constexpr (esize == 8) {
            return ir.VectorBroadcast(8, ir.Imm8(static_cast<u8>(mask)));
        } else if constexpr (esize == 16) {
            return ir.VectorBroadcast(16, ir.Imm16(static_cast<u16>(mask)));
        } else if constexpr (esize == 32) {
            return ir.VectorBroadcast(32, ir.Imm32(static_cast<u32>(mask)));
        } else {
            return ir.VectorBroadcast(64, ir.Imm64(mask));
        }
    }();
    const IR::U128 result = ir.VectorOr(ir.VectorAndNot(destination, mask_vector), shifted);

    inst.ReplaceUsesWith(result);
}

}  // namespace

void PolyfillPass(IR::Block& block, const PolyfillOptions& polyfill) {
    if (polyfill == PolyfillOptions{}) {
        return;
    }

    IR::IREmitter ir{block};

    for (auto& inst : block) {
        ir.SetInsertionPointBefore(&inst);

        switch (inst.GetOpcode()) {
        case IR::Opcode::SHA256MessageSchedule0:
            if (polyfill.sha256) {
                PolyfillSHA256MessageSchedule0(ir, inst);
            }
            break;
        case IR::Opcode::SHA256MessageSchedule1:
            if (polyfill.sha256) {
                PolyfillSHA256MessageSchedule1(ir, inst);
            }
            break;
        case IR::Opcode::SHA256Hash:
            if (polyfill.sha256) {
                PolyfillSHA256Hash(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAbsoluteDifferenceWiden8:
            if (polyfill.vector_absolute_difference_widen) {
                PolyfillVectorAbsoluteDifferenceWiden<8, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAbsoluteDifferenceWiden16:
            if (polyfill.vector_absolute_difference_widen) {
                PolyfillVectorAbsoluteDifferenceWiden<16, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAbsoluteDifferenceWiden32:
            if (polyfill.vector_absolute_difference_widen) {
                PolyfillVectorAbsoluteDifferenceWiden<32, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAbsoluteDifferenceWiden8:
            if (polyfill.vector_absolute_difference_widen) {
                PolyfillVectorAbsoluteDifferenceWiden<8, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAbsoluteDifferenceWiden16:
            if (polyfill.vector_absolute_difference_widen) {
                PolyfillVectorAbsoluteDifferenceWiden<16, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAbsoluteDifferenceWiden32:
            if (polyfill.vector_absolute_difference_widen) {
                PolyfillVectorAbsoluteDifferenceWiden<32, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAddSubWide8:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<8, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAddSubWide16:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<16, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAddSubWide32:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<32, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAddSubWiden8:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<8, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAddSubWiden16:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<16, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedAddSubWiden32:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<32, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAddSubWide8:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<8, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAddSubWide16:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<16, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAddSubWide32:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<32, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAddSubWiden8:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<8, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAddSubWiden16:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<16, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedAddSubWiden32:
            if (polyfill.vector_add_sub_widen) {
                PolyfillVectorAddSubWiden<32, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplySignedWiden8:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<8, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplySignedWiden16:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<16, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplySignedWiden32:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<32, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplyUnsignedWiden8:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<8, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplyUnsignedWiden16:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<16, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplyUnsignedWiden32:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<32, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedMultiplyAccumulateWiden8:
            if (polyfill.vector_multiply_accumulate_widen) {
                PolyfillVectorMultiplyAccumulateWiden<8, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedMultiplyAccumulateWiden16:
            if (polyfill.vector_multiply_accumulate_widen) {
                PolyfillVectorMultiplyAccumulateWiden<16, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedMultiplyAccumulateWiden32:
            if (polyfill.vector_multiply_accumulate_widen) {
                PolyfillVectorMultiplyAccumulateWiden<32, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingNarrow16:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<16, RoundingNarrowKind::Truncate>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingNarrow32:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<32, RoundingNarrowKind::Truncate>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingNarrow64:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<64, RoundingNarrowKind::Truncate>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightS8:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<8, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightS16:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<16, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightS32:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<32, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightS64:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<64, true, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightU8:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<8, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightU16:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<16, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightU32:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<32, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightU64:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<64, false, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateS8:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<8, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateS16:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<16, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateS32:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<32, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateS64:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<64, true, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateU8:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<8, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateU16:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<16, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateU32:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<32, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorRoundingShiftRightAccumulateU64:
            if (polyfill.vector_rounding_shift_right) {
                PolyfillVectorRoundingShiftRight<64, false, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftLeftInsert8:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<8, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftLeftInsert16:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<16, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftLeftInsert32:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<32, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftLeftInsert64:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<64, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftRightInsert8:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<8, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftRightInsert16:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<16, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftRightInsert32:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<32, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorShiftRightInsert64:
            if (polyfill.vector_shift_insert) {
                PolyfillVectorShiftInsert<64, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedSaturatedRoundingNarrowToSigned16:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<16, RoundingNarrowKind::SignedSaturatedToSigned>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedSaturatedRoundingNarrowToSigned32:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<32, RoundingNarrowKind::SignedSaturatedToSigned>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedSaturatedRoundingNarrowToSigned64:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<64, RoundingNarrowKind::SignedSaturatedToSigned>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedSaturatedRoundingNarrowToUnsigned16:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<16, RoundingNarrowKind::SignedSaturatedToUnsigned>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedSaturatedRoundingNarrowToUnsigned32:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<32, RoundingNarrowKind::SignedSaturatedToUnsigned>(ir, inst);
            }
            break;
        case IR::Opcode::VectorSignedSaturatedRoundingNarrowToUnsigned64:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<64, RoundingNarrowKind::SignedSaturatedToUnsigned>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedSaturatedRoundingNarrow16:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<16, RoundingNarrowKind::UnsignedSaturated>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedSaturatedRoundingNarrow32:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<32, RoundingNarrowKind::UnsignedSaturated>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedSaturatedRoundingNarrow64:
            if (polyfill.vector_rounding_narrow) {
                PolyfillVectorRoundingNarrow<64, RoundingNarrowKind::UnsignedSaturated>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedMultiplyAccumulateWiden8:
            if (polyfill.vector_multiply_accumulate_widen) {
                PolyfillVectorMultiplyAccumulateWiden<8, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedMultiplyAccumulateWiden16:
            if (polyfill.vector_multiply_accumulate_widen) {
                PolyfillVectorMultiplyAccumulateWiden<16, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorUnsignedMultiplyAccumulateWiden32:
            if (polyfill.vector_multiply_accumulate_widen) {
                PolyfillVectorMultiplyAccumulateWiden<32, false>(ir, inst);
            }
            break;
        default:
            break;
        }
    }
}

}  // namespace Dynarmic::Optimization
