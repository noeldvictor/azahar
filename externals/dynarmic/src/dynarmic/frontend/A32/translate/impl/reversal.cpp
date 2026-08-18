/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"

namespace Dynarmic::A32 {

// RBIT<c> <Rd>, <Rm>
bool TranslatorVisitor::arm_RBIT(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    ir.SetRegister(d, ir.ReverseBits(ir.GetRegister(m)));
    return true;
}

// REV<c> <Rd>, <Rm>
bool TranslatorVisitor::arm_REV(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const auto result = ir.ByteReverseWord(ir.GetRegister(m));
    ir.SetRegister(d, result);
    return true;
}

// REV16<c> <Rd>, <Rm>
bool TranslatorVisitor::arm_REV16(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    ir.SetRegister(d, ir.ByteReverseHalfwords(ir.GetRegister(m)));
    return true;
}

// REVSH<c> <Rd>, <Rm>
bool TranslatorVisitor::arm_REVSH(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const auto rev_half = ir.ByteReverseHalf(ir.LeastSignificantHalf(ir.GetRegister(m)));
    ir.SetRegister(d, ir.SignExtendHalfToWord(rev_half));
    return true;
}

}  // namespace Dynarmic::A32
