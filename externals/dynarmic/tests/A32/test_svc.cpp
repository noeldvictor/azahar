/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "./testenv.h"

using namespace Dynarmic;

class ArmSvcTestEnv : public ArmTestEnv {
public:
    std::optional<u32> svc_called = std::nullopt;
    A32::Jit* jit = nullptr;
    u32 cpsr_seen = 0;

    void CallSVC(u32 swi) override {
        svc_called = swi;
        if (jit) {
            cpsr_seen = jit->Cpsr();
            jit->SetCpsr((cpsr_seen & 0x0fff'ffff) | 0x8000'0000);
        }
    }
};

TEST_CASE("arm: svc", "[arm][A32]") {
    ArmSvcTestEnv test_env;
    A32::Jit jit{A32::UserConfig{&test_env}};
    test_env.code_mem = {
        0xef0001ee,  // svc #0x1ee
        0xe30a0071,  // mov r0, #41073
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 3;
    jit.Run();

    REQUIRE(test_env.svc_called == 0x1ee);
    REQUIRE(jit.Regs()[15] == 0x00000008);
    REQUIRE(jit.Regs()[0] == 41073);
}

TEST_CASE("arm: svc synchronizes cpsr nzcv", "[arm][A32]") {
    ArmSvcTestEnv test_env;
    A32::Jit jit{A32::UserConfig{&test_env}};
    test_env.jit = &jit;
    test_env.code_mem = {
        0xe3500000,  // cmp r0, #0 (Z=1, C=1)
        0xef000123,  // svc #0x123 (callback replaces NZCV with N=1)
        0x43a01001,  // movmi r1, #1
        0x03a02001,  // moveq r2, #1
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x0000'01d0);  // User-mode

    test_env.ticks_left = 5;
    jit.Run();

    REQUIRE(test_env.svc_called == 0x123);
    REQUIRE((test_env.cpsr_seen & 0xf000'0000) == 0x6000'0000);
    REQUIRE(jit.Regs()[1] == 1);
    REQUIRE(jit.Regs()[2] == 0);
    REQUIRE((jit.Cpsr() & 0xf000'0000) == 0x8000'0000);
}
