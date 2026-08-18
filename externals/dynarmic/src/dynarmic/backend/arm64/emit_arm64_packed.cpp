/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<typename EmitFn>
static void EmitPackedOp(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    emit(Vresult, Va, Vb);
}

template<typename EmitFn>
static void EmitSaturatedPackedOp(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);
    ctx.fpsr.Spill();

    emit(Vresult, Va, Vb);
}

template<>
void EmitIR<IR::Opcode::PackedAddU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.ADD(Vresult->B8(), Va->B8(), Vb->B8());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.CMHI(Vge->B8(), Va->B8(), Vresult->B8());
    }
}

template<>
void EmitIR<IR::Opcode::PackedAddS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.ADD(Vresult->B8(), Va->B8(), Vb->B8());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.SHADD(Vge->B8(), Va->B8(), Vb->B8());
        code.CMGE(Vge->B8(), Vge->B8(), 0);
    }
}

template<>
void EmitIR<IR::Opcode::PackedSubU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.SUB(Vresult->B8(), Va->B8(), Vb->B8());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.UHSUB(Vge->B8(), Va->B8(), Vb->B8());
        code.CMGE(Vge->B8(), Vge->B8(), 0);
    }
}

template<>
void EmitIR<IR::Opcode::PackedSubS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.SUB(Vresult->B8(), Va->B8(), Vb->B8());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.SHSUB(Vge->B8(), Va->B8(), Vb->B8());
        code.CMGE(Vge->B8(), Vge->B8(), 0);
    }
}

template<>
void EmitIR<IR::Opcode::PackedAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.ADD(Vresult->H4(), Va->H4(), Vb->H4());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.CMHI(Vge->H4(), Va->H4(), Vresult->H4());
    }
}

template<>
void EmitIR<IR::Opcode::PackedAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.ADD(Vresult->H4(), Va->H4(), Vb->H4());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.SHADD(Vge->H4(), Va->H4(), Vb->H4());
        code.CMGE(Vge->H4(), Vge->H4(), 0);
    }
}

template<>
void EmitIR<IR::Opcode::PackedSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.SUB(Vresult->H4(), Va->H4(), Vb->H4());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.UHSUB(Vge->H4(), Va->H4(), Vb->H4());
        code.CMGE(Vge->H4(), Vge->H4(), 0);
    }
}

template<>
void EmitIR<IR::Opcode::PackedSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.SUB(Vresult->H4(), Va->H4(), Vb->H4());

    if (ge_inst) {
        auto Vge = ctx.reg_alloc.WriteD(ge_inst);
        RegAlloc::Realize(Vge);

        code.SHSUB(Vge->H4(), Va->H4(), Vb->H4());
        code.CMGE(Vge->H4(), Vge->H4(), 0);
    }
}

template<bool add_is_hi, bool is_signed, bool is_halving>
static void EmitPackedAddSub(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    const auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);

    if constexpr (is_halving) {
        ASSERT(!ge_inst);
        RegAlloc::Realize(Vresult, Va, Vb);

        // ASX/SAX exchange the two source halfwords before applying opposite operations. Native
        // halving add/sub already has the exact signed rounding and unsigned underflow behavior;
        // compute both two-lane candidates and replace only the low lane. The IR result is U32, so
        // lanes 2-3 are deliberately irrelevant.
        code.REV32(V0.H4(), Vb->H4());
        if constexpr (add_is_hi) {
            if constexpr (is_signed) {
                code.SHADD(Vresult->H4(), Va->H4(), V0.H4());
                code.SHSUB(V1.H4(), Va->H4(), V0.H4());
            } else {
                code.UHADD(Vresult->H4(), Va->H4(), V0.H4());
                code.UHSUB(V1.H4(), Va->H4(), V0.H4());
            }
        } else {
            if constexpr (is_signed) {
                code.SHSUB(Vresult->H4(), Va->H4(), V0.H4());
                code.SHADD(V1.H4(), Va->H4(), V0.H4());
            } else {
                code.UHSUB(Vresult->H4(), Va->H4(), V0.H4());
                code.UHADD(V1.H4(), Va->H4(), V0.H4());
            }
        }
        code.MOV(Vresult->Helem()[0], V1.H()[0]);
        return;
    }

    if (!ge_inst) {
        RegAlloc::Realize(Vresult, Va, Vb);

        code.REV32(V0.H4(), Vb->H4());
        if constexpr (add_is_hi) {
            code.SUB(V1.H4(), Va->H4(), V0.H4());
            code.ADD(Vresult->H4(), Va->H4(), V0.H4());
        } else {
            code.ADD(V1.H4(), Va->H4(), V0.H4());
            code.SUB(Vresult->H4(), Va->H4(), V0.H4());
        }
        code.MOV(Vresult->Helem()[0], V1.H()[0]);
        return;
    }

    auto Vge = ctx.reg_alloc.WriteD(ge_inst);
    RegAlloc::Realize(Vresult, Va, Vb, Vge);

    // Compute the wrapped result directly in halfword lanes. Signed GE uses the sign of native
    // halving add/sub, which exactly matches the sign of the full-width result. Unsigned addition
    // detects carry by comparing the first operand with the wrapped result, while UHSUB's sign bit
    // distinguishes subtraction no-borrow. This keeps the recurring result path free of widening
    // and narrowing while preserving all four GE bits.
    code.REV32(V0.H4(), Vb->H4());
    if constexpr (add_is_hi) {
        code.SUB(V1.H4(), Va->H4(), V0.H4());
        code.ADD(Vresult->H4(), Va->H4(), V0.H4());
    } else {
        code.ADD(V1.H4(), Va->H4(), V0.H4());
        code.SUB(Vresult->H4(), Va->H4(), V0.H4());
    }
    code.MOV(Vresult->Helem()[0], V1.H()[0]);

    if constexpr (is_signed) {
        if constexpr (add_is_hi) {
            code.SHSUB(V1.H4(), Va->H4(), V0.H4());
            code.SHADD(Vge->H4(), Va->H4(), V0.H4());
        } else {
            code.SHADD(V1.H4(), Va->H4(), V0.H4());
            code.SHSUB(Vge->H4(), Va->H4(), V0.H4());
        }
        code.MOV(Vge->Helem()[0], V1.H()[0]);
        code.CMGE(Vge->H4(), Vge->H4(), 0);
    } else if constexpr (add_is_hi) {
        code.CMHI(Vge->H4(), Va->H4(), Vresult->H4());
        code.UHSUB(V1.H4(), Va->H4(), V0.H4());
        code.CMGE(V1.H4(), V1.H4(), 0);
        code.MOV(Vge->Helem()[0], V1.H()[0]);
    } else {
        code.CMHI(V1.H4(), Va->H4(), V1.H4());
        code.UHSUB(Vge->H4(), Va->H4(), V0.H4());
        code.CMGE(Vge->H4(), Vge->H4(), 0);
        code.MOV(Vge->Helem()[0], V1.H()[0]);
    }
}

template<>
void EmitIR<IR::Opcode::PackedAddSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<true, false, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedAddSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<true, true, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedSubAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<false, false, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedSubAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<false, true, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UHADD(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SHADD(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UHSUB(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SHSUB(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UHADD(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SHADD(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UHSUB(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SHSUB(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<true, false, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<true, true, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<false, false, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedAddSub<false, true, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UQADD(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SQADD(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UQSUB(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubS8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SQSUB(Vresult->B8(), Va->B8(), Vb->B8()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UQADD(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SQADD(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.UQSUB(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitSaturatedPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) { code.SQSUB(Vresult->H4(), Va->H4(), Vb->H4()); });
}

template<bool add_is_hi, bool is_signed>
static void EmitPackedSaturatedAddSub(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Vresult = ctx.reg_alloc.WriteD(inst);
    auto Va = ctx.reg_alloc.ReadD(args[0]);
    auto Vb = ctx.reg_alloc.ReadD(args[1]);
    RegAlloc::Realize(Vresult, Va, Vb);
    ctx.fpsr.Spill();

    // ASX/SAX exchange source halfwords before applying opposite saturating operations. Compute
    // both native two-lane candidates, then replace only the low lane; lanes 2-3 are irrelevant
    // because the IR result is U32.
    code.REV32(V0.H4(), Vb->H4());
    if constexpr (add_is_hi) {
        if constexpr (is_signed) {
            code.SQADD(Vresult->H4(), Va->H4(), V0.H4());
            code.SQSUB(V1.H4(), Va->H4(), V0.H4());
        } else {
            code.UQADD(Vresult->H4(), Va->H4(), V0.H4());
            code.UQSUB(V1.H4(), Va->H4(), V0.H4());
        }
    } else {
        if constexpr (is_signed) {
            code.SQSUB(Vresult->H4(), Va->H4(), V0.H4());
            code.SQADD(V1.H4(), Va->H4(), V0.H4());
        } else {
            code.UQSUB(Vresult->H4(), Va->H4(), V0.H4());
            code.UQADD(V1.H4(), Va->H4(), V0.H4());
        }
    }
    code.MOV(Vresult->Helem()[0], V1.H()[0]);
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddSubU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedSaturatedAddSub<true, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddSubS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedSaturatedAddSub<true, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubAddU16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedSaturatedAddSub<false, false>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubAddS16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedSaturatedAddSub<false, true>(code, ctx, inst);
}

template<>
void EmitIR<IR::Opcode::PackedAbsDiffSumU8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitPackedOp(code, ctx, inst, [&](auto& Vresult, auto& Va, auto& Vb) {
        // USAD8 consumes exactly the low four bytes. Widening all eight byte differences places
        // those four lanes in the low 4H arrangement, so the following reduction ignores any
        // undefined upper word in the packed operands without materializing and applying a mask.
        code.UABDL(Vresult->toQ().H8(), Va->B8(), Vb->B8());
        code.UADDLV(Vresult->toS(), Vresult->H4());
    });
}

template<>
void EmitIR<IR::Opcode::PackedSelect>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Vresult = ctx.reg_alloc.ReadWriteD(args[0], inst);
    auto Va = ctx.reg_alloc.ReadD(args[1]);
    auto Vb = ctx.reg_alloc.ReadD(args[2]);
    RegAlloc::Realize(Vresult, Va, Vb);

    code.BSL(Vresult->B8(), Vb->B8(), Va->B8());
}

template<>
void EmitIR<IR::Opcode::PackHalfwordBottom>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const u8 shift_amount = inst->GetArg(2).GetU8();

    if (shift_amount == 16) {
        auto Wresult = ctx.reg_alloc.ReadWriteW(args[0], inst);
        auto Wm = ctx.reg_alloc.ReadW(args[1]);
        RegAlloc::Realize(Wresult, Wm);

        code.BFI(*Wresult, *Wm, 16, 16);
        return;
    }

    auto Wn = ctx.reg_alloc.ReadW(args[0]);
    auto Wresult = ctx.reg_alloc.ReadWriteW(args[1], inst);
    RegAlloc::Realize(Wn, Wresult);

    if (shift_amount != 0) {
        code.LSL(*Wresult, *Wresult, shift_amount);
    }
    code.BFXIL(*Wresult, *Wn, 0, 16);
}

template<>
void EmitIR<IR::Opcode::PackHalfwordTop>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const u8 shift_amount = inst->GetArg(2).GetU8();

    auto Wresult = ctx.reg_alloc.ReadWriteW(args[0], inst);
    auto Wm = ctx.reg_alloc.ReadW(args[1]);
    RegAlloc::Realize(Wresult, Wm);

    if (shift_amount <= 16) {
        code.BFXIL(*Wresult, *Wm, shift_amount, 16);
        return;
    }

    code.ASR(Wscratch0, *Wm, shift_amount <= 31 ? shift_amount : 31);
    code.BFXIL(*Wresult, Wscratch0, 0, 16);
}

}  // namespace Dynarmic::Backend::Arm64
