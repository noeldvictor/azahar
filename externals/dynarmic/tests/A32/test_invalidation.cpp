/* This file is part of the dynarmic project.
 * Copyright (c) 2026 Azahar Thor Experiment contributors
 * SPDX-License-Identifier: 0BSD
 */

#include <catch2/catch_test_macros.hpp>

#include "./testenv.h"
#include "dynarmic/interface/A32/a32.h"

using namespace Dynarmic;

TEST_CASE("A32 fast dispatch entries are cleared on invalidation", "[arm][A32]") {
    ArmTestEnv env;

    A32::UserConfig conf;
    conf.callbacks = &env;
    REQUIRE(conf.HasOptimization(OptimizationFlag::FastDispatch));

    A32::Jit jit{conf};
    env.code_mem = {
        0xe3a00008,  // MOV R0, #8
        0xe12fff10,  // BX R0
        0xe3a0002a,  // MOV R0, #42
        0xeafffffe,  // B .
    };
    jit.SetCpsr(0x000001d0);  // User mode

    const auto run = [&] {
        jit.Regs()[0] = 0;
        jit.Regs()[15] = 0;
        env.ticks_left = 4;
        jit.Run();
        return jit.Regs()[0];
    };

    // The second run takes the fast-dispatch hit path.
    REQUIRE(run() == 42);
    REQUIRE(run() == 42);

    env.code_mem[2] = 0xe3a00045;  // MOV R0, #69

    // Compiled code remains active until the guest range is invalidated.
    REQUIRE(run() == 42);
    jit.InvalidateCacheRange(8, 4);

    // Invalidation must discard both the emitted block and its fast-dispatch entry.
    REQUIRE(run() == 69);
    REQUIRE(run() == 69);
}

TEST_CASE("A32 fast dispatch entries are cleared with the code cache", "[arm][A32]") {
    ArmTestEnv env;

    A32::UserConfig conf;
    conf.callbacks = &env;
    REQUIRE(conf.HasOptimization(OptimizationFlag::FastDispatch));

    A32::Jit jit{conf};
    env.code_mem = {
        0xe3a00008,  // MOV R0, #8
        0xe12fff10,  // BX R0
        0xe3a0002a,  // MOV R0, #42
        0xeafffffe,  // B .
    };
    jit.SetCpsr(0x000001d0);  // User mode

    const auto run = [&] {
        jit.Regs()[0] = 0;
        jit.Regs()[15] = 0;
        env.ticks_left = 4;
        jit.Run();
        return jit.Regs()[0];
    };

    REQUIRE(run() == 42);
    REQUIRE(run() == 42);

    env.code_mem[2] = 0xe3a00045;  // MOV R0, #69
    jit.ClearCache();

    REQUIRE(run() == 69);
    REQUIRE(run() == 69);
}
