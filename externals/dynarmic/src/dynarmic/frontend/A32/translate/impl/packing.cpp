/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"

namespace Dynarmic::A32 {

// PKHBT<c> <Rd>, <Rn>, <Rm>{, LSL #<imm>}
bool TranslatorVisitor::arm_PKHBT(Cond cond, Reg n, Reg d, Imm<5> imm5, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    ir.SetRegister(d, ir.PackHalfwordBottom(ir.GetRegister(n), ir.GetRegister(m), imm5.ZeroExtend<u8>()));
    return true;
}

// PKHTB<c> <Rd>, <Rn>, <Rm>{, ASR #<imm>}
bool TranslatorVisitor::arm_PKHTB(Cond cond, Reg n, Reg d, Imm<5> imm5, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ArmConditionPassed(cond)) {
        return true;
    }

    const u8 shift_amount = imm5 == 0 ? 32 : imm5.ZeroExtend<u8>();
    ir.SetRegister(d, ir.PackHalfwordTop(ir.GetRegister(n), ir.GetRegister(m), shift_amount));
    return true;
}

}  // namespace Dynarmic::A32
