/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <mcl/bitsizeof.hpp>

#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"

namespace Dynarmic::A32 {

// BFC<c> <Rd>, #<lsb>, #<width>
bool TranslatorVisitor::arm_BFC(Cond cond, Imm<5> msb, Reg d, Imm<5> lsb) {
    if (d == Reg::PC) {
        return UnpredictableInstruction();
    }
    if (msb < lsb) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const u32 lsb_value = lsb.ZeroExtend();
    const u32 msb_value = msb.ZeroExtend();
    const u32 mask = ~(mcl::bit::ones<u32>(msb_value - lsb_value + 1) << lsb_value);
    const IR::U32 operand = ir.GetRegister(d);
    const IR::U32 result = ir.And(operand, ir.Imm32(mask));

    ir.SetRegister(d, result);
    return true;
}

// BFI<c> <Rd>, <Rn>, #<lsb>, #<width>
bool TranslatorVisitor::arm_BFI(Cond cond, Imm<5> msb, Reg d, Imm<5> lsb, Reg n) {
    if (d == Reg::PC) {
        return UnpredictableInstruction();
    }
    if (msb < lsb) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const u8 lsb_value = static_cast<u8>(lsb.ZeroExtend());
    const u8 width = static_cast<u8>(msb.ZeroExtend() - lsb_value + 1);
    const IR::U32 destination = ir.GetRegister(d);
    const IR::U32 result = d == n
                             ? ir.BitFieldInsertSelf(destination, lsb_value, width)
                             : ir.BitFieldInsert(destination, ir.GetRegister(n), lsb_value, width);

    ir.SetRegister(d, result);
    return true;
}

// CLZ<c> <Rd>, <Rm>
bool TranslatorVisitor::arm_CLZ(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    ir.SetRegister(d, ir.CountLeadingZeros(ir.GetRegister(m)));
    return true;
}

// MOVT<c> <Rd>, #<imm16>
bool TranslatorVisitor::arm_MOVT(Cond cond, Imm<4> imm4, Reg d, Imm<12> imm12) {
    if (d == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const IR::U32 imm16 = ir.Imm32(concatenate(imm4, imm12).ZeroExtend() << 16);
    const IR::U32 operand = ir.GetRegister(d);
    const IR::U32 result = ir.Or(ir.And(operand, ir.Imm32(0x0000FFFFU)), imm16);

    ir.SetRegister(d, result);
    return true;
}

bool TranslatorVisitor::arm_MOVW(Cond cond, Imm<4> imm4, Reg d, Imm<12> imm12) {
    if (d == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const IR::U32 imm = ir.Imm32(concatenate(imm4, imm12).ZeroExtend());

    ir.SetRegister(d, imm);
    return true;
}

// SBFX<c> <Rd>, <Rn>, #<lsb>, #<width>
bool TranslatorVisitor::arm_SBFX(Cond cond, Imm<5> widthm1, Reg d, Imm<5> lsb, Reg n) {
    if (d == Reg::PC || n == Reg::PC) {
        return UnpredictableInstruction();
    }

    const u32 lsb_value = lsb.ZeroExtend();
    const u32 widthm1_value = widthm1.ZeroExtend();
    const u32 msb = lsb_value + widthm1_value;
    if (msb >= mcl::bitsizeof<u32>) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const u8 width = static_cast<u8>(widthm1_value + 1);
    const IR::U32 operand = ir.GetRegister(n);
    const IR::U32 result = ir.SignedBitFieldExtract(operand, static_cast<u8>(lsb_value), width);

    ir.SetRegister(d, result);
    return true;
}

// SEL<c> <Rd>, <Rn>, <Rm>
bool TranslatorVisitor::arm_SEL(Cond cond, Reg n, Reg d, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const auto to = ir.GetRegister(m);
    const auto from = ir.GetRegister(n);
    const auto result = ir.PackedSelect(ir.GetGEFlags(), to, from);

    ir.SetRegister(d, result);
    return true;
}

// UBFX<c> <Rd>, <Rn>, #<lsb>, #<width>
bool TranslatorVisitor::arm_UBFX(Cond cond, Imm<5> widthm1, Reg d, Imm<5> lsb, Reg n) {
    if (d == Reg::PC || n == Reg::PC) {
        return UnpredictableInstruction();
    }

    const u32 lsb_value = lsb.ZeroExtend();
    const u32 widthm1_value = widthm1.ZeroExtend();
    const u32 msb = lsb_value + widthm1_value;
    if (msb >= mcl::bitsizeof<u32>) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const IR::U32 operand = ir.GetRegister(n);
    const IR::U32 result = ir.UnsignedBitFieldExtract(
        operand, static_cast<u8>(lsb_value), static_cast<u8>(widthm1_value + 1));

    ir.SetRegister(d, result);
    return true;
}

}  // namespace Dynarmic::A32
