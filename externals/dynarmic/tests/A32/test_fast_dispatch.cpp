/* This file is part of the dynarmic project.
 * Copyright (c) 2026 Azahar Thor Experiment contributors
 * SPDX-License-Identifier: 0BSD
 */

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "./testenv.h"
#include "dynarmic/interface/A32/a32.h"

using namespace Dynarmic;

namespace {

constexpr u64 ticks_per_sample = 3'000'000;

void ConfigureDispatchLoop(ArmTestEnv& env) {
    env.code_mem = {
        0xe2200010,  // EOR R0, R0, #16
        0xe2811001,  // ADD R1, R1, #1
        0xe12fff10,  // BX R0
        0xeafffffe,  // B .
        0xe2200010,  // EOR R0, R0, #16
        0xe2811001,  // ADD R1, R1, #1
        0xe12fff10,  // BX R0
        0xeafffffe,  // B .
    };
}

void ConfigureNZCVLoop(ArmTestEnv& env) {
    env.code_mem = {
        0xe2500001,  // SUBS R0, R0, #1
        0x1afffffd,  // BNE -#12
        0xeafffffe,  // B .
    };
}

u32 RunDispatchLoop(A32::Jit& jit, ArmTestEnv& env) {
    jit.Regs()[0] = 0;
    jit.Regs()[1] = 0;
    jit.Regs()[15] = 0;
    env.ticks_left = ticks_per_sample;
    jit.Run();
    return jit.Regs()[1];
}

u32 RunNZCVLoop(A32::Jit& jit, ArmTestEnv& env) {
    jit.Regs()[0] = ticks_per_sample / 2;
    jit.Regs()[15] = 0;
    env.ticks_left = ticks_per_sample;
    jit.Run();
    return jit.Regs()[0];
}

}  // namespace

TEST_CASE("A32 FastDispatch microbenchmark", "[.benchmark][A32][arm]") {
    ArmTestEnv slow_env;
    ConfigureDispatchLoop(slow_env);
    A32::UserConfig slow_conf;
    slow_conf.callbacks = &slow_env;
    slow_conf.optimizations &= ~OptimizationFlag::FastDispatch;
    A32::Jit slow_jit{slow_conf};
    slow_jit.SetCpsr(0x000001d0);  // User mode

    ArmTestEnv fast_env;
    ConfigureDispatchLoop(fast_env);
    A32::UserConfig fast_conf;
    fast_conf.callbacks = &fast_env;
    REQUIRE(fast_conf.HasOptimization(OptimizationFlag::FastDispatch));
    A32::Jit fast_jit{fast_conf};
    fast_jit.SetCpsr(0x000001d0);  // User mode

    // Compile and warm both paths before Catch2 starts timing them.
    REQUIRE(RunDispatchLoop(slow_jit, slow_env) == ticks_per_sample / 3);
    REQUIRE(RunDispatchLoop(fast_jit, fast_env) == ticks_per_sample / 3);

    BENCHMARK("A32 C++ dispatcher (first)") {
        return RunDispatchLoop(slow_jit, slow_env);
    };

    BENCHMARK("A32 ARM64 FastDispatch (second)") {
        return RunDispatchLoop(fast_jit, fast_env);
    };

    // Reverse the order to expose thermal or frequency-order bias in the result.
    BENCHMARK("A32 ARM64 FastDispatch (third)") {
        return RunDispatchLoop(fast_jit, fast_env);
    };

    BENCHMARK("A32 C++ dispatcher (fourth)") {
        return RunDispatchLoop(slow_jit, slow_env);
    };
}

TEST_CASE("A32 ARM64 NZCV cache microbenchmark", "[.benchmark][A32][arm]") {
    ArmTestEnv env;
    ConfigureNZCVLoop(env);
    A32::Jit jit{A32::UserConfig{&env}};
    jit.SetCpsr(0x000001d0);  // User mode

    // Each iteration writes NZCV in SUBS and immediately consumes it in BNE.
    REQUIRE(RunNZCVLoop(jit, env) == 0);

    BENCHMARK("A32 flag-producing conditional loop") {
        return RunNZCVLoop(jit, env);
    };
}
