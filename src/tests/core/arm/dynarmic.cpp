// Copyright Azahar Emulator Project / Azahar Thor Experiment
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <dynarmic/interface/A32/a32.h>

namespace {

class ArmTestCallbacks final : public Dynarmic::A32::UserCallbacks {
public:
    std::array<std::uint32_t, 6> code{
        0xe2900001,  // ADDS R0, R0, #1
        0x43a01001,  // MOVMI R1, #1
        0x63a02001,  // MOVVS R2, #1
        0x23a03001,  // MOVCS R3, #1
        0x03a04001,  // MOVEQ R4, #1
        0xeafffffe,  // B .
    };
    std::uint64_t ticks_left{};

    std::optional<std::uint32_t> MemoryReadCode(std::uint32_t address) override {
        if ((address & 3) == 0 && address / 4 < code.size()) {
            return code[address / 4];
        }
        return std::nullopt;
    }

    std::uint8_t MemoryRead8(std::uint32_t) override {
        return 0;
    }
    std::uint16_t MemoryRead16(std::uint32_t) override {
        return 0;
    }
    std::uint32_t MemoryRead32(std::uint32_t) override {
        return 0;
    }
    std::uint64_t MemoryRead64(std::uint32_t) override {
        return 0;
    }

    void MemoryWrite8(std::uint32_t, std::uint8_t) override {
        FAIL("unexpected A32 byte write");
    }
    void MemoryWrite16(std::uint32_t, std::uint16_t) override {
        FAIL("unexpected A32 halfword write");
    }
    void MemoryWrite32(std::uint32_t, std::uint32_t) override {
        FAIL("unexpected A32 word write");
    }
    void MemoryWrite64(std::uint32_t, std::uint64_t) override {
        FAIL("unexpected A32 doubleword write");
    }

    void InterpreterFallback(std::uint32_t, std::size_t) override {
        FAIL("unexpected A32 interpreter fallback");
    }
    void CallSVC(std::uint32_t) override {
        FAIL("unexpected A32 SVC");
    }
    void ExceptionRaised(std::uint32_t, Dynarmic::A32::Exception) override {
        FAIL("unexpected A32 exception");
    }

    void AddTicks(std::uint64_t ticks) override {
        ticks_left = ticks < ticks_left ? ticks_left - ticks : 0;
    }
    std::uint64_t GetTicksRemaining() override {
        return ticks_left;
    }
};

}  // namespace

TEST_CASE("Dynarmic preserves arithmetic NZCV across linked A32 blocks", "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    struct Case {
        std::uint32_t input;
        std::uint32_t result;
        std::uint32_t nzcv;
    };
    constexpr std::array cases{
        Case{0x7fffffff, 0x80000000, 0x90000000},  // N/V
        Case{0xffffffff, 0x00000000, 0x60000000},  // Z/C
        Case{0x00000000, 0x00000001, 0x00000000},  // no flags
        Case{0xfffffffe, 0xffffffff, 0x80000000},  // N only
    };

    for (const auto& test : cases) {
        CAPTURE(test.input);
        jit.Regs() = {};
        jit.SetCpsr(0x000001d0);  // User mode
        jit.Regs()[0] = test.input;
        callbacks.ticks_left = 5;
        jit.Run();

        CHECK(jit.Regs()[0] == test.result);
        CHECK(jit.Regs()[1] == ((test.nzcv & 0x80000000) != 0));  // MI / N
        CHECK(jit.Regs()[2] == ((test.nzcv & 0x10000000) != 0));  // VS / V
        CHECK(jit.Regs()[3] == ((test.nzcv & 0x20000000) != 0));  // CS / C
        CHECK(jit.Regs()[4] == ((test.nzcv & 0x40000000) != 0));  // EQ / Z
        CHECK((jit.Cpsr() & 0xf0000000) == test.nzcv);
    }
}

TEST_CASE("Dynarmic preserves VTBX defaults through ARM64 read-write coalescing",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf3f408e0,  // VTBX.8 D16, {D20}, D16
        0xf3f419e1,  // VTBX.8 D17, {D20, D21}, D17
        0xf3f42ae2,  // VTBX.8 D18, {D20, D21, D22}, D18
        0xf3f43be3,  // VTBX.8 D19, {D20, D21, D22, D23}, D19
        0xeafffffe,  // B .
        0xeafffffe,  // B .
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    for (std::size_t reg = 16; reg <= 19; ++reg) {
        jit.ExtRegs()[reg * 2 + 0] = 0x05'02'01'00;
        jit.ExtRegs()[reg * 2 + 1] = 0x20'1F'10'0F;
    }
    for (std::size_t reg = 20; reg <= 23; ++reg) {
        const auto first_byte = static_cast<std::uint32_t>((reg - 20) * 8);
        jit.ExtRegs()[reg * 2 + 0] = (first_byte + 3) << 24 | (first_byte + 2) << 16 |
                                      (first_byte + 1) << 8 | first_byte;
        jit.ExtRegs()[reg * 2 + 1] = (first_byte + 7) << 24 | (first_byte + 6) << 16 |
                                      (first_byte + 5) << 8 | (first_byte + 4);
    }

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    for (std::size_t reg = 16; reg <= 19; ++reg) {
        CAPTURE(reg);
        CHECK(jit.ExtRegs()[reg * 2 + 0] == 0x05'02'01'00);
        CHECK(jit.ExtRegs()[reg * 2 + 1] == 0x20'1F'10'0F);
    }
}

TEST_CASE("Dynarmic A32 long multiply preserves packed low and high words",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        bool accumulate;
        bool source_alias;
        bool thumb;
        bool update_flags;
    };
    constexpr std::array operations{
        Operation{0xe0a10392, true, false, false, false},   // ARM UMLAL R0, R1, R2, R3
        Operation{0xe0b10392, true, false, false, true},    // ARM UMLALS R0, R1, R2, R3
        Operation{0xe0a10190, true, true, false, false},    // ARM UMLAL R0, R1, R0, R1
        Operation{0xe0810392, false, false, false, false},  // ARM UMULL R0, R1, R2, R3
        Operation{0xe0910392, false, false, false, true},   // ARM UMULLS R0, R1, R2, R3
        Operation{0xe0810190, false, true, false, false},   // ARM UMULL R0, R1, R0, R1
        Operation{0x0103fbe2, true, false, true, false},   // Thumb UMLAL R0, R1, R2, R3
        Operation{0x0101fbe0, true, true, true, false},    // Thumb UMLAL R0, R1, R0, R1
        Operation{0x0103fba2, false, false, true, false},  // Thumb UMULL R0, R1, R2, R3
        Operation{0x0101fba0, false, true, true, false},   // Thumb UMULL R0, R1, R0, R1
    };
    struct Inputs {
        std::uint64_t addend;
        std::uint32_t n;
        std::uint32_t m;
    };
    constexpr std::array inputs{
        Inputs{0x0123456789abcdef, 0xfedcba98, 0x76543210},
        Inputs{0, 0xffffffff, 0xffffffff},
        Inputs{0xffffffffffffffff, 0xffffffff, 2},
        Inputs{0x8000000000000000, 0x80000000, 0x80000000},
        Inputs{0x7fffffffffffffff, 0x7fffffff, 0x7fffffff},
        Inputs{0xdeadbeef01234567, 0x00010002, 0xfffeffff},
    };

    for (const auto& operation : operations) {
        for (const auto& input : inputs) {
            CAPTURE(operation.instruction, input.addend, input.n, input.m);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                operation.instruction,
                operation.thumb ? 0xe7fee7fe : 0xeafffffe,  // B .
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};
            const std::uint32_t n = operation.source_alias
                                        ? static_cast<std::uint32_t>(input.addend)
                                        : input.n;
            const std::uint32_t m = operation.source_alias
                                        ? static_cast<std::uint32_t>(input.addend >> 32)
                                        : input.m;
            const std::uint64_t product =
                static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(m);
            const std::uint64_t expected = operation.accumulate ? product + input.addend : product;
            jit.Regs() = {};
            jit.Regs()[0] = static_cast<std::uint32_t>(input.addend);
            jit.Regs()[1] = static_cast<std::uint32_t>(input.addend >> 32);
            jit.Regs()[2] = input.n;
            jit.Regs()[3] = input.m;
            constexpr std::uint32_t initial_flags = 0x780f0000;  // ZCV/Q/GE
            const std::uint32_t initial_cpsr =
                initial_flags | 0x000001d0 | (operation.thumb ? 0x20 : 0);
            jit.SetCpsr(initial_cpsr);
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == static_cast<std::uint32_t>(expected));
            CHECK(jit.Regs()[1] == static_cast<std::uint32_t>(expected >> 32));
            if (!operation.source_alias) {
                CHECK(jit.Regs()[2] == input.n);
                CHECK(jit.Regs()[3] == input.m);
            }
            const std::uint32_t expected_flags = operation.update_flags
                                                     ? (initial_flags & 0x380f0000) |
                                                           (expected == 0 ? 0x40000000 : 0) |
                                                           (expected >> 63 != 0 ? 0x80000000 : 0)
                                                     : initial_flags;
            CHECK((jit.Cpsr() & 0xf80f0000) == expected_flags);
        }
    }
}

TEST_CASE("Dynarmic A32 signed long multiply preserves packed low and high words",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        bool source_alias;
        bool thumb;
        bool update_flags;
    };
    constexpr std::array operations{
        Operation{0xe0c10392, false, false, false},  // ARM SMULL R0, R1, R2, R3
        Operation{0xe0d10392, false, false, true},   // ARM SMULLS R0, R1, R2, R3
        Operation{0xe0c10190, true, false, false},   // ARM SMULL R0, R1, R0, R1
        Operation{0x0103fb82, false, true, false},   // Thumb SMULL R0, R1, R2, R3
        Operation{0x0101fb80, true, true, false},    // Thumb SMULL R0, R1, R0, R1
    };
    struct Inputs {
        std::uint32_t n;
        std::uint32_t m;
    };
    constexpr std::array inputs{
        Inputs{0x80000000, 0xffffffff},
        Inputs{0x80000000, 0x80000000},
        Inputs{0x7fffffff, 0x7fffffff},
        Inputs{0xffffffff, 0xffffffff},
        Inputs{0xffffffff, 2},
        Inputs{0, 0xffffffff},
        Inputs{static_cast<std::uint32_t>(-123456789), 1987654321},
    };

    for (const auto& operation : operations) {
        for (const auto& input : inputs) {
            CAPTURE(operation.instruction, input.n, input.m);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                operation.instruction,
                operation.thumb ? 0xe7fee7fe : 0xeafffffe,  // B .
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};
            const std::int64_t expected = static_cast<std::int64_t>(
                                              static_cast<std::int32_t>(input.n)) *
                                          static_cast<std::int64_t>(
                                              static_cast<std::int32_t>(input.m));
            const std::uint64_t expected_bits = static_cast<std::uint64_t>(expected);
            jit.Regs() = {};
            jit.Regs()[0] = operation.source_alias ? input.n : 0x89abcdef;
            jit.Regs()[1] = operation.source_alias ? input.m : 0x01234567;
            jit.Regs()[2] = input.n;
            jit.Regs()[3] = input.m;
            constexpr std::uint32_t initial_flags = 0x780f0000;  // ZCV/Q/GE
            const std::uint32_t initial_cpsr =
                initial_flags | 0x000001d0 | (operation.thumb ? 0x20 : 0);
            jit.SetCpsr(initial_cpsr);
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == static_cast<std::uint32_t>(expected_bits));
            CHECK(jit.Regs()[1] == static_cast<std::uint32_t>(expected_bits >> 32));
            if (!operation.source_alias) {
                CHECK(jit.Regs()[2] == input.n);
                CHECK(jit.Regs()[3] == input.m);
            }
            const std::uint32_t expected_flags = operation.update_flags
                                                     ? (initial_flags & 0x380f0000) |
                                                           (expected_bits == 0 ? 0x40000000 : 0) |
                                                           (expected_bits >> 63 != 0 ? 0x80000000 : 0)
                                                     : initial_flags;
            CHECK((jit.Cpsr() & 0xf80f0000) == expected_flags);
        }
    }
}

TEST_CASE("Dynarmic A32 scalar NEON long multiply broadcasts directly from its lane",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf2920a4b,  // VMULL.S16 Q0, D2, D3[1]
        0xf3968267,  // VMLAL.U16 Q4, D6, D7[2]
        0xf2ea066b,  // VMLSL.S32 Q8, D10, D11[1]
        0xf3ee8a4f,  // VMULL.U32 Q12, D14, D15[0]
        0xeafffffe,  // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.ExtRegs() = {};

    // D2: {-32768, -2, 3, 32767}; D3[1]: -7.
    jit.ExtRegs()[4] = 0xfffe8000;
    jit.ExtRegs()[5] = 0x7fff0003;
    jit.ExtRegs()[6] = 0xfff90011;

    // Q4 accumulator and D6 unsigned inputs; D7[2]: 0xfffd.
    jit.ExtRegs()[16] = 0xffffff00;
    jit.ExtRegs()[17] = 0x00000001;
    jit.ExtRegs()[18] = 0x7fffffff;
    jit.ExtRegs()[19] = 0x80000000;
    jit.ExtRegs()[12] = 0x00020001;
    jit.ExtRegs()[13] = 0xffff8000;
    jit.ExtRegs()[15] = 0x1234fffd;

    // Q8 accumulator, D10 signed inputs, and D11[1]: -3.
    constexpr std::int64_t acc0 = 0x0123456789abcdef;
    constexpr std::int64_t acc1 = -0x0123456789abcdf;
    jit.ExtRegs()[32] = static_cast<std::uint32_t>(acc0);
    jit.ExtRegs()[33] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(acc0) >> 32);
    jit.ExtRegs()[34] = static_cast<std::uint32_t>(acc1);
    jit.ExtRegs()[35] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(acc1) >> 32);
    jit.ExtRegs()[20] = 0x80000000;
    jit.ExtRegs()[21] = 0x7fffffff;
    jit.ExtRegs()[23] = 0xfffffffd;

    // D14 unsigned inputs and D15[0].
    jit.ExtRegs()[28] = 0xffffffff;
    jit.ExtRegs()[29] = 0x80000001;
    jit.ExtRegs()[30] = 0xfedcba98;

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    constexpr std::array<std::int16_t, 4> signed16{-32768, -2, 3, 32767};
    for (std::size_t lane = 0; lane < signed16.size(); ++lane) {
        CAPTURE(lane);
        const auto expected = static_cast<std::int32_t>(signed16[lane]) * -7;
        CHECK(jit.ExtRegs()[lane] == static_cast<std::uint32_t>(expected));
    }

    constexpr std::array<std::uint32_t, 4> acc16{
        0xffffff00, 0x00000001, 0x7fffffff, 0x80000000};
    constexpr std::array<std::uint16_t, 4> unsigned16{1, 2, 0x8000, 0xffff};
    for (std::size_t lane = 0; lane < unsigned16.size(); ++lane) {
        CAPTURE(lane);
        const auto expected = acc16[lane] + unsigned16[lane] * 0xfffdu;
        CHECK(jit.ExtRegs()[16 + lane] == expected);
    }

    constexpr std::array<std::int32_t, 2> signed32{
        static_cast<std::int32_t>(0x80000000), 0x7fffffff};
    constexpr std::array<std::int64_t, 2> acc32{acc0, acc1};
    for (std::size_t lane = 0; lane < signed32.size(); ++lane) {
        CAPTURE(lane);
        const auto expected = static_cast<std::uint64_t>(
            acc32[lane] - static_cast<std::int64_t>(signed32[lane]) * -3);
        const auto actual = static_cast<std::uint64_t>(jit.ExtRegs()[32 + lane * 2]) |
                            static_cast<std::uint64_t>(jit.ExtRegs()[33 + lane * 2]) << 32;
        CHECK(actual == expected);
    }

    constexpr std::array<std::uint32_t, 2> unsigned32{0xffffffff, 0x80000001};
    for (std::size_t lane = 0; lane < unsigned32.size(); ++lane) {
        CAPTURE(lane);
        const auto expected = static_cast<std::uint64_t>(unsigned32[lane]) * 0xfedcba98u;
        const auto actual = static_cast<std::uint64_t>(jit.ExtRegs()[48 + lane * 2]) |
                            static_cast<std::uint64_t>(jit.ExtRegs()[49 + lane * 2]) << 32;
        CHECK(actual == expected);
    }
}

TEST_CASE("Dynarmic A32 VSHLL fuses widening shifts on ARM64", "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        std::size_t destination_s;
        std::size_t source_s;
        std::uint8_t element_bits;
        std::uint8_t shift;
        bool is_signed;
    };
    constexpr std::array operations{
        Operation{0xf2890a12, 0, 4, 8, 1, true},      // VSHLL.S8 Q0, D2, #1
        Operation{0xf38f8a1a, 16, 20, 8, 7, false},   // VSHLL.U8 Q4, D10, #7
        Operation{0xf2d30a32, 32, 36, 16, 3, true},   // VSHLL.S16 Q8, D18, #3
        Operation{0xf3df8a3a, 48, 52, 16, 15, false}, // VSHLL.U16 Q12, D26, #15
        Operation{0xf2a12a12, 4, 4, 32, 1, true},     // VSHLL.S32 Q1, D2, #1 (overlap)
        Operation{0xf3ffea3f, 60, 62, 32, 31, false}, // VSHLL.U32 Q15, D31, #31 (overlap)
        Operation{0xf3b20302, 0, 4, 8, 8, false},     // VSHLL.I8 Q0, D2, #8
        Operation{0xf3f60322, 32, 36, 16, 16, false}, // VSHLL.I16 Q8, D18, #16
        Operation{0xf3fae32f, 60, 62, 32, 32, false}, // VSHLL.I32 Q15, D31, #32 (overlap)
    };

    constexpr std::uint64_t source_bits = 0x800000017fff80ff;
    constexpr std::uint32_t initial_cpsr = 0xa80f01d0; // N/C/Q/GE, user mode

    for (const auto& operation : operations) {
        CAPTURE(operation.instruction, operation.destination_s, operation.source_s,
                operation.element_bits, operation.shift, operation.is_signed);

        ArmTestCallbacks callbacks;
        callbacks.code = {
            operation.instruction,
            0xeafffffe, // B .
        };
        Dynarmic::A32::UserConfig config{&callbacks};
        Dynarmic::A32::Jit jit{config};

        jit.ExtRegs().fill(0xa5a5a5a5);
        jit.ExtRegs()[operation.source_s] = static_cast<std::uint32_t>(source_bits);
        jit.ExtRegs()[operation.source_s + 1] = static_cast<std::uint32_t>(source_bits >> 32);

        const auto expected_vector = [&] {
            std::array<std::uint8_t, 16> result_bytes{};
            const std::uint8_t output_bits = operation.element_bits * 2;
            const std::uint64_t input_mask = (std::uint64_t{1} << operation.element_bits) - 1;
            const std::uint64_t output_mask =
                output_bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << output_bits) - 1;
            const std::size_t lane_count = 64 / operation.element_bits;

            for (std::size_t lane = 0; lane < lane_count; ++lane) {
                const std::uint64_t input =
                    (source_bits >> (lane * operation.element_bits)) & input_mask;
                std::uint64_t extended = input;
                if (operation.is_signed &&
                    (input & (std::uint64_t{1} << (operation.element_bits - 1))) != 0) {
                    extended |= ~input_mask;
                }
                const std::uint64_t shifted = (extended << operation.shift) & output_mask;
                for (std::size_t byte = 0; byte < output_bits / 8; ++byte) {
                    result_bytes[lane * output_bits / 8 + byte] =
                        static_cast<std::uint8_t>(shifted >> (byte * 8));
                }
            }

            std::array<std::uint32_t, 4> result{};
            std::memcpy(result.data(), result_bytes.data(), result_bytes.size());
            return result;
        }();

        auto expected_regs = jit.ExtRegs();
        std::copy(expected_vector.begin(), expected_vector.end(),
                  expected_regs.begin() + operation.destination_s);

        jit.SetCpsr(initial_cpsr);
        callbacks.ticks_left = 2;
        jit.Run();

        CHECK(jit.ExtRegs() == expected_regs);
        CHECK((jit.Cpsr() & 0xf80f0000) == (initial_cpsr & 0xf80f0000));
    }
}

TEST_CASE("Dynarmic A32 shift-right narrowing preserves rounding and saturation on ARM64",
          "[core][arm][dynarmic]") {
    enum class NarrowKind {
        Truncate,
        SignedToSigned,
        UnsignedToUnsigned,
        SignedToUnsigned,
    };
    struct Operation {
        std::uint32_t instruction;
        std::size_t destination_s;
        std::size_t source_s;
        std::uint8_t source_bits;
        std::uint8_t shift;
        NarrowKind kind;
        bool rounding{};
    };
    constexpr std::array operations{
        Operation{0xf28b0812, 0, 4, 16, 5, NarrowKind::Truncate},       // VSHRN.I16 D0, Q1, #5
        Operation{0xf2d50832, 32, 36, 32, 11, NarrowKind::Truncate},    // VSHRN.I32 D16, Q9, #11
        Operation{0xf2edf83e, 62, 60, 64, 19, NarrowKind::Truncate},    // VSHRN.I64 D31, Q15, #19
        Operation{0xf28b0912, 0, 4, 16, 5, NarrowKind::SignedToSigned}, // VQSHRN.S16 D0, Q1, #5
        Operation{0xf2d50932, 32, 36, 32, 11,
                  NarrowKind::SignedToSigned}, // VQSHRN.S32 D16, Q9, #11
        Operation{0xf2edf93e, 62, 60, 64, 19,
                  NarrowKind::SignedToSigned}, // VQSHRN.S64 D31, Q15, #19
        Operation{0xf38b0912, 0, 4, 16, 5, NarrowKind::UnsignedToUnsigned}, // VQSHRN.U16 D0, Q1, #5
        Operation{0xf3d50932, 32, 36, 32, 11,
                  NarrowKind::UnsignedToUnsigned}, // VQSHRN.U32 D16, Q9, #11
        Operation{0xf3edf93e, 62, 60, 64, 19,
                  NarrowKind::UnsignedToUnsigned}, // VQSHRN.U64 D31, Q15, #19
        Operation{0xf38b0812, 0, 4, 16, 5, NarrowKind::SignedToUnsigned}, // VQSHRUN.S16 D0, Q1, #5
        Operation{0xf3d50832, 32, 36, 32, 11,
                  NarrowKind::SignedToUnsigned}, // VQSHRUN.S32 D16, Q9, #11
        Operation{0xf3edf83e, 62, 60, 64, 19,
                  NarrowKind::SignedToUnsigned}, // VQSHRUN.S64 D31, Q15, #19
        Operation{0xf28b0852, 0, 4, 16, 5, NarrowKind::Truncate, true}, // VRSHRN.I16 D0, Q1, #5
        Operation{0xf2d50872, 32, 36, 32, 11, NarrowKind::Truncate,
                  true}, // VRSHRN.I32 D16, Q9, #11
        Operation{0xf2edf87e, 62, 60, 64, 19, NarrowKind::Truncate,
                  true}, // VRSHRN.I64 D31, Q15, #19
        Operation{0xf28b0952, 0, 4, 16, 5, NarrowKind::SignedToSigned,
                  true}, // VQRSHRN.S16 D0, Q1, #5
        Operation{0xf2d50972, 32, 36, 32, 11, NarrowKind::SignedToSigned,
                  true}, // VQRSHRN.S32 D16, Q9, #11
        Operation{0xf2edf97e, 62, 60, 64, 19, NarrowKind::SignedToSigned,
                  true}, // VQRSHRN.S64 D31, Q15, #19
        Operation{0xf38b0952, 0, 4, 16, 5, NarrowKind::UnsignedToUnsigned,
                  true}, // VQRSHRN.U16 D0, Q1, #5
        Operation{0xf3d50972, 32, 36, 32, 11, NarrowKind::UnsignedToUnsigned,
                  true}, // VQRSHRN.U32 D16, Q9, #11
        Operation{0xf3edf97e, 62, 60, 64, 19, NarrowKind::UnsignedToUnsigned,
                  true}, // VQRSHRN.U64 D31, Q15, #19
        Operation{0xf38b0852, 0, 4, 16, 5, NarrowKind::SignedToUnsigned,
                  true}, // VQRSHRUN.S16 D0, Q1, #5
        Operation{0xf3d50872, 32, 36, 32, 11, NarrowKind::SignedToUnsigned,
                  true}, // VQRSHRUN.S32 D16, Q9, #11
        Operation{0xf3edf87e, 62, 60, 64, 19, NarrowKind::SignedToUnsigned,
                  true}, // VQRSHRUN.S64 D31, Q15, #19
    };

    constexpr std::array<std::uint32_t, 4> source_words{
        0xffffffff,
        0x80808080,
        0x7f7f7f7f,
        0x55555555,
    };
    constexpr std::uint32_t initial_cpsr = 0xa80f01d0;  // N/C/Q/GE, user mode
    constexpr std::uint32_t initial_fpscr = 0xa3400001; // N/C, rounding mode, IOC
    constexpr std::uint32_t fpscr_qc = 1U << 27;

    for (const auto& operation : operations) {
        CAPTURE(operation.instruction, operation.destination_s, operation.source_s,
                operation.source_bits, operation.shift, operation.kind, operation.rounding);

        ArmTestCallbacks callbacks;
        callbacks.code = {
            operation.instruction,
            0xeafffffe, // B .
        };
        Dynarmic::A32::UserConfig config{&callbacks};
        Dynarmic::A32::Jit jit{config};

        jit.ExtRegs().fill(0xa5a5a5a5);
        std::copy(source_words.begin(), source_words.end(),
                  jit.ExtRegs().begin() + operation.source_s);

        std::array<std::uint8_t, 16> source_bytes{};
        std::memcpy(source_bytes.data(), source_words.data(), source_bytes.size());
        std::array<std::uint8_t, 8> result_bytes{};
        const std::uint8_t result_bits = operation.source_bits / 2;
        const std::uint64_t result_mask =
            result_bits == 32 ? 0xffffffffULL : (std::uint64_t{1} << result_bits) - 1;
        const std::uint64_t signed_max = (std::uint64_t{1} << (result_bits - 1)) - 1;
        const std::uint64_t signed_min = ~signed_max;
        const std::size_t lane_count = 128 / operation.source_bits;
        bool saturated = false;

        for (std::size_t lane = 0; lane < lane_count; ++lane) {
            std::uint64_t input = 0;
            for (std::size_t byte = 0; byte < operation.source_bits / 8; ++byte) {
                input |= static_cast<std::uint64_t>(
                             source_bytes[lane * operation.source_bits / 8 + byte])
                         << (byte * 8);
            }

            const bool signed_input = operation.kind == NarrowKind::SignedToSigned ||
                                      operation.kind == NarrowKind::SignedToUnsigned;
            std::uint64_t shifted = input >> operation.shift;
            if (signed_input && (input & (std::uint64_t{1} << (operation.source_bits - 1))) != 0) {
                shifted |= ~std::uint64_t{0} << (operation.source_bits - operation.shift);
            }
            if (operation.rounding &&
                (input & (std::uint64_t{1} << (operation.shift - 1))) != 0) {
                ++shifted;
            }

            std::uint64_t result = shifted & result_mask;
            if (operation.kind == NarrowKind::SignedToSigned) {
                if ((shifted >> 63) != 0 && shifted < signed_min) {
                    result = signed_min & result_mask;
                    saturated = true;
                } else if ((shifted >> 63) == 0 && shifted > signed_max) {
                    result = signed_max;
                    saturated = true;
                }
            } else if (operation.kind == NarrowKind::UnsignedToUnsigned) {
                if (shifted > result_mask) {
                    result = result_mask;
                    saturated = true;
                }
            } else if (operation.kind == NarrowKind::SignedToUnsigned) {
                if ((shifted >> 63) != 0) {
                    result = 0;
                    saturated = true;
                } else if (shifted > result_mask) {
                    result = result_mask;
                    saturated = true;
                }
            }

            for (std::size_t byte = 0; byte < result_bits / 8; ++byte) {
                result_bytes[lane * result_bits / 8 + byte] =
                    static_cast<std::uint8_t>(result >> (byte * 8));
            }
        }

        std::array<std::uint32_t, 2> expected_result{};
        std::memcpy(expected_result.data(), result_bytes.data(), result_bytes.size());
        auto expected_regs = jit.ExtRegs();
        std::copy(expected_result.begin(), expected_result.end(),
                  expected_regs.begin() + operation.destination_s);

        jit.SetCpsr(initial_cpsr);
        jit.SetFpscr(initial_fpscr);
        callbacks.ticks_left = 2;
        jit.Run();

        CHECK(jit.ExtRegs() == expected_regs);
        CHECK((jit.Cpsr() & 0xf80f0000) == (initial_cpsr & 0xf80f0000));
        if (operation.kind == NarrowKind::Truncate) {
            CHECK_FALSE(saturated);
            CHECK(jit.Fpscr() == initial_fpscr);
        } else {
            CHECK(saturated);
            CHECK((jit.Fpscr() & ~fpscr_qc) == initial_fpscr);
            CHECK((jit.Fpscr() & fpscr_qc) != 0);
        }
    }
}

TEST_CASE("Dynarmic A32 vector rounding shift-right preserves lane semantics on ARM64",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        std::size_t destination_s;
        std::size_t source_s;
        std::uint8_t vector_bits;
        std::uint8_t element_bits;
        std::uint8_t shift;
        bool is_signed;
        bool accumulate;
    };
    constexpr std::array operations{
        Operation{0xf2880212, 0, 4, 64, 8, 8, true, false},       // VRSHR.S8 D0, D2, #8
        Operation{0xf3c80272, 32, 36, 128, 8, 8, false, false},   // VRSHR.U8 Q8, Q9, #8
        Operation{0xf2990252, 0, 4, 128, 16, 7, true, false},     // VRSHR.S16 Q0, Q1, #7
        Operation{0xf3d90232, 32, 36, 64, 16, 7, false, false},   // VRSHR.U16 D16, D18, #7
        Operation{0xf2a10212, 0, 4, 64, 32, 31, true, false},     // VRSHR.S32 D0, D2, #31
        Operation{0xf3e10272, 32, 36, 128, 32, 31, false, false}, // VRSHR.U32 Q8, Q9, #31
        Operation{0xf28002d2, 0, 4, 128, 64, 64, true, false},    // VRSHR.S64 Q0, Q1, #64
        Operation{0xf3c002b2, 32, 36, 64, 64, 64, false, false},  // VRSHR.U64 D16, D18, #64
        Operation{0xf2880310, 0, 0, 64, 8, 8, true, true},        // VRSRA.S8 D0, D0, #8
        Operation{0xf3c80370, 32, 32, 128, 8, 8, false, true},    // VRSRA.U8 Q8, Q8, #8
        Operation{0xf2990352, 0, 4, 128, 16, 7, true, true},      // VRSRA.S16 Q0, Q1, #7
        Operation{0xf3d90332, 32, 36, 64, 16, 7, false, true},    // VRSRA.U16 D16, D18, #7
        Operation{0xf2a10310, 0, 0, 64, 32, 31, true, true},      // VRSRA.S32 D0, D0, #31
        Operation{0xf3e10370, 32, 32, 128, 32, 31, false, true},  // VRSRA.U32 Q8, Q8, #31
        Operation{0xf28003d2, 0, 4, 128, 64, 64, true, true},     // VRSRA.S64 Q0, Q1, #64
        Operation{0xf3c003b2, 32, 36, 64, 64, 64, false, true},   // VRSRA.U64 D16, D18, #64
    };
    constexpr std::array<std::uint32_t, 4> source_words{
        0x80000080,
        0x7fffff7f,
        0x00000004,
        0xfffffffc,
    };
    constexpr std::array<std::uint32_t, 4> accumulator_words{
        0xffffffff,
        0x7fffffff,
        0x80000000,
        0x12345678,
    };
    constexpr std::uint32_t initial_cpsr = 0xa80f01d0;  // N/C/Q/GE, user mode
    constexpr std::uint32_t initial_fpscr = 0xa3400001; // N/C, rounding mode, IOC

    for (const auto& operation : operations) {
        CAPTURE(operation.instruction, operation.destination_s, operation.source_s,
                operation.vector_bits, operation.element_bits, operation.shift, operation.is_signed,
                operation.accumulate);

        ArmTestCallbacks callbacks;
        callbacks.code = {
            operation.instruction,
            0xeafffffe, // B .
        };
        Dynarmic::A32::UserConfig config{&callbacks};
        Dynarmic::A32::Jit jit{config};

        jit.ExtRegs().fill(0xa5a5a5a5);
        const std::size_t vector_words = operation.vector_bits / 32;
        std::copy_n(accumulator_words.begin(), vector_words,
                    jit.ExtRegs().begin() + operation.destination_s);
        std::copy_n(source_words.begin(), vector_words, jit.ExtRegs().begin() + operation.source_s);

        auto expected_regs = jit.ExtRegs();
        const auto* source_bytes =
            reinterpret_cast<const std::uint8_t*>(expected_regs.data() + operation.source_s);
        auto* destination_bytes =
            reinterpret_cast<std::uint8_t*>(expected_regs.data() + operation.destination_s);
        const std::size_t element_bytes = operation.element_bits / 8;
        const std::size_t lane_count = operation.vector_bits / operation.element_bits;
        const std::uint64_t mask = operation.element_bits == 64
                                       ? ~std::uint64_t{0}
                                       : (std::uint64_t{1} << operation.element_bits) - 1;
        const std::uint64_t sign_bit = std::uint64_t{1} << (operation.element_bits - 1);

        for (std::size_t lane = 0; lane < lane_count; ++lane) {
            std::uint64_t input{};
            std::uint64_t accumulator{};
            std::memcpy(&input, source_bytes + lane * element_bytes, element_bytes);
            std::memcpy(&accumulator, destination_bytes + lane * element_bytes, element_bytes);

            std::uint64_t shifted{};
            if (operation.shift == operation.element_bits) {
                shifted = operation.is_signed && (input & sign_bit) != 0 ? mask : 0;
            } else {
                shifted = input >> operation.shift;
                if (operation.is_signed && (input & sign_bit) != 0) {
                    shifted |= mask << (operation.element_bits - operation.shift);
                }
            }
            if ((input & (std::uint64_t{1} << (operation.shift - 1))) != 0) {
                shifted = (shifted + 1) & mask;
            }
            const std::uint64_t result =
                operation.accumulate ? (accumulator + shifted) & mask : shifted & mask;
            std::memcpy(destination_bytes + lane * element_bytes, &result, element_bytes);
        }

        jit.SetCpsr(initial_cpsr);
        jit.SetFpscr(initial_fpscr);
        callbacks.ticks_left = 2;
        jit.Run();

        CHECK(jit.ExtRegs() == expected_regs);
        CHECK((jit.Cpsr() & 0xf80f0000) == (initial_cpsr & 0xf80f0000));
        CHECK(jit.Fpscr() == initial_fpscr);
    }
}

TEST_CASE("Dynarmic A32 vector shift-insert preserves destination bits on ARM64",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        std::size_t destination_s;
        std::size_t source_s;
        std::uint8_t vector_bits;
        std::uint8_t element_bits;
        std::uint8_t shift;
        bool shift_left;
    };
    constexpr std::array operations{
        Operation{0xf3880510, 0, 0, 64, 8, 0, true},        // VSLI.8 D0, D0, #0
        Operation{0xf3cf0572, 32, 36, 128, 8, 7, true},     // VSLI.8 Q8, Q9, #7
        Operation{0xf3900552, 0, 4, 128, 16, 0, true},      // VSLI.16 Q0, Q1, #0
        Operation{0xf3df0532, 32, 36, 64, 16, 15, true},    // VSLI.16 D16, D18, #15
        Operation{0xf3a00510, 0, 0, 64, 32, 0, true},       // VSLI.32 D0, D0, #0
        Operation{0xf3ff0572, 32, 36, 128, 32, 31, true},   // VSLI.32 Q8, Q9, #31
        Operation{0xf38005d2, 0, 4, 128, 64, 0, true},      // VSLI.64 Q0, Q1, #0
        Operation{0xf3ff05b2, 32, 36, 64, 64, 63, true},    // VSLI.64 D16, D18, #63
        Operation{0xf38f0410, 0, 0, 64, 8, 1, false},       // VSRI.8 D0, D0, #1
        Operation{0xf3c80472, 32, 36, 128, 8, 8, false},    // VSRI.8 Q8, Q9, #8
        Operation{0xf39f0452, 0, 4, 128, 16, 1, false},     // VSRI.16 Q0, Q1, #1
        Operation{0xf3d00432, 32, 36, 64, 16, 16, false},   // VSRI.16 D16, D18, #16
        Operation{0xf3bf0410, 0, 0, 64, 32, 1, false},      // VSRI.32 D0, D0, #1
        Operation{0xf3e00472, 32, 36, 128, 32, 32, false},  // VSRI.32 Q8, Q9, #32
        Operation{0xf3bf04d2, 0, 4, 128, 64, 1, false},     // VSRI.64 Q0, Q1, #1
        Operation{0xf3c004b2, 32, 36, 64, 64, 64, false},   // VSRI.64 D16, D18, #64
    };
    constexpr std::array<std::uint32_t, 4> source_words{
        0x80ff017f,
        0x01234567,
        0x89abcdef,
        0xfedcba98,
    };
    constexpr std::array<std::uint32_t, 4> destination_words{
        0xa55a3cc3,
        0x0f0ff0f0,
        0x13579bdf,
        0x2468ace0,
    };
    constexpr std::uint32_t initial_cpsr = 0xa80f01d0;   // N/C/Q/GE, user mode
    constexpr std::uint32_t initial_fpscr = 0xa3400001;  // N/C, rounding mode, IOC

    for (const auto& operation : operations) {
        CAPTURE(operation.instruction, operation.destination_s, operation.source_s,
                operation.vector_bits, operation.element_bits, operation.shift,
                operation.shift_left);

        ArmTestCallbacks callbacks;
        callbacks.code = {
            operation.instruction,
            0xeafffffe,  // B .
        };
        Dynarmic::A32::UserConfig config{&callbacks};
        Dynarmic::A32::Jit jit{config};

        jit.ExtRegs().fill(0xa5a5a5a5);
        const std::size_t vector_words = operation.vector_bits / 32;
        std::copy_n(destination_words.begin(), vector_words,
                    jit.ExtRegs().begin() + operation.destination_s);
        std::copy_n(source_words.begin(), vector_words, jit.ExtRegs().begin() + operation.source_s);

        auto expected_regs = jit.ExtRegs();
        const auto original_regs = expected_regs;
        const auto* source_bytes =
            reinterpret_cast<const std::uint8_t*>(original_regs.data() + operation.source_s);
        auto* destination_bytes =
            reinterpret_cast<std::uint8_t*>(expected_regs.data() + operation.destination_s);
        const std::size_t element_bytes = operation.element_bits / 8;
        const std::size_t lane_count = operation.vector_bits / operation.element_bits;
        const std::uint64_t element_mask = operation.element_bits == 64
                                               ? ~std::uint64_t{0}
                                               : (std::uint64_t{1} << operation.element_bits) - 1;
        const std::uint64_t insert_mask = operation.shift_left
                                              ? (element_mask << operation.shift) & element_mask
                                              : operation.shift == operation.element_bits
                                                    ? 0
                                                    : element_mask >> operation.shift;

        for (std::size_t lane = 0; lane < lane_count; ++lane) {
            std::uint64_t source{};
            std::uint64_t destination{};
            std::memcpy(&source, source_bytes + lane * element_bytes, element_bytes);
            std::memcpy(&destination, destination_bytes + lane * element_bytes, element_bytes);
            const std::uint64_t shifted = operation.shift_left
                                              ? source << operation.shift
                                              : operation.shift == operation.element_bits
                                                    ? 0
                                                    : source >> operation.shift;
            const std::uint64_t result =
                ((destination & ~insert_mask) | (shifted & insert_mask)) & element_mask;
            std::memcpy(destination_bytes + lane * element_bytes, &result, element_bytes);
        }

        jit.SetCpsr(initial_cpsr);
        jit.SetFpscr(initial_fpscr);
        callbacks.ticks_left = 2;
        jit.Run();

        CHECK(jit.ExtRegs() == expected_regs);
        CHECK((jit.Cpsr() & 0xf80f0000) == (initial_cpsr & 0xf80f0000));
        CHECK(jit.Fpscr() == initial_fpscr);
    }
}

TEST_CASE("Dynarmic A32 VMLAL and VMLSL widen before modular accumulation",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf2820803, // VMLAL.S8 Q0, D2, D3
        0xf3968807, // VMLAL.U16 Q4, D6, D7
        0xf2ea080b, // VMLAL.S32 Q8, D10, D11
        0xf3ce8a0f, // VMLSL.U8 Q12, D14, D15
        0xeafffffe, // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};
    jit.ExtRegs() = {};

    const auto set_vector = [&](std::size_t first_s_register, const auto& lanes) {
        std::memcpy(jit.ExtRegs().data() + first_s_register, lanes.data(), sizeof(lanes));
    };

    constexpr std::array<std::uint16_t, 8> signed8_acc{0,      1,      0x7fff, 0x8000,
                                                       0xfffe, 0xffff, 1234,   65000};
    constexpr std::array<std::int8_t, 8> signed8_n{-128, -127, -1, 0, 1, 2, 100, 127};
    constexpr std::array<std::int8_t, 8> signed8_m{-128, 127, -100, -1, 1, 100, -127, 127};
    set_vector(0, signed8_acc);
    set_vector(4, signed8_n);
    set_vector(6, signed8_m);

    constexpr std::array<std::uint32_t, 4> unsigned16_acc{0, 1, 0x7fffffff, 0xfffffff0};
    constexpr std::array<std::uint16_t, 4> unsigned16_n{0, 1, 0x8000, 0xffff};
    constexpr std::array<std::uint16_t, 4> unsigned16_m{0xffff, 0xfffe, 0x8001, 0xffff};
    set_vector(16, unsigned16_acc);
    set_vector(12, unsigned16_n);
    set_vector(14, unsigned16_m);

    constexpr std::array<std::uint64_t, 2> signed32_acc{0xfffffffffffffff0ULL,
                                                        0x7fffffffffffffffULL};
    constexpr std::array<std::int32_t, 2> signed32_n{static_cast<std::int32_t>(0x80000000),
                                                     0x7fffffff};
    constexpr std::array<std::int32_t, 2> signed32_m{-1, 0x7fffffff};
    set_vector(32, signed32_acc);
    set_vector(20, signed32_n);
    set_vector(22, signed32_m);

    constexpr std::array<std::uint16_t, 8> unsigned8_acc{0,      1,      255,    256,
                                                         0x7fff, 0x8000, 0xfffe, 0xffff};
    constexpr std::array<std::uint8_t, 8> unsigned8_n{0, 1, 2, 3, 127, 128, 254, 255};
    constexpr std::array<std::uint8_t, 8> unsigned8_m{255, 254, 253, 252, 129, 128, 2, 255};
    set_vector(48, unsigned8_acc);
    set_vector(28, unsigned8_n);
    set_vector(30, unsigned8_m);

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    std::array<std::uint16_t, 8> signed8_result{};
    std::array<std::uint32_t, 4> unsigned16_result{};
    std::array<std::uint64_t, 2> signed32_result{};
    std::array<std::uint16_t, 8> unsigned8_result{};
    std::memcpy(signed8_result.data(), jit.ExtRegs().data(), sizeof(signed8_result));
    std::memcpy(unsigned16_result.data(), jit.ExtRegs().data() + 16, sizeof(unsigned16_result));
    std::memcpy(signed32_result.data(), jit.ExtRegs().data() + 32, sizeof(signed32_result));
    std::memcpy(unsigned8_result.data(), jit.ExtRegs().data() + 48, sizeof(unsigned8_result));

    for (std::size_t lane = 0; lane < signed8_result.size(); ++lane) {
        CAPTURE(lane);
        const auto product = static_cast<std::int16_t>(signed8_n[lane]) * signed8_m[lane];
        CHECK(signed8_result[lane] == static_cast<std::uint16_t>(signed8_acc[lane] + product));
    }
    for (std::size_t lane = 0; lane < unsigned16_result.size(); ++lane) {
        CAPTURE(lane);
        const auto product = static_cast<std::uint32_t>(unsigned16_n[lane]) * unsigned16_m[lane];
        CHECK(unsigned16_result[lane] == unsigned16_acc[lane] + product);
    }
    for (std::size_t lane = 0; lane < signed32_result.size(); ++lane) {
        CAPTURE(lane);
        const auto product = static_cast<std::int64_t>(signed32_n[lane]) * signed32_m[lane];
        CHECK(signed32_result[lane] == signed32_acc[lane] + static_cast<std::uint64_t>(product));
    }
    for (std::size_t lane = 0; lane < unsigned8_result.size(); ++lane) {
        CAPTURE(lane);
        const auto product = static_cast<std::uint16_t>(unsigned8_n[lane]) * unsigned8_m[lane];
        CHECK(unsigned8_result[lane] == static_cast<std::uint16_t>(unsigned8_acc[lane] - product));
    }
}

TEST_CASE("Dynarmic A32 VABDL widens signed and unsigned differences",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf2820703,  // VABDL.S8 Q0, D2, D3
        0xf3968707,  // VABDL.U16 Q4, D6, D7
        0xf2ea070b,  // VABDL.S32 Q8, D10, D11
        0xeafffffe,  // B .
        0xeafffffe,
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.ExtRegs() = {};

    // D2: {-128, -1, 0, 1, 42, 100, 127, -64}.
    // D3: {127, 1, 0, -1, -42, -100, -128, 64}.
    jit.ExtRegs()[4] = 0x0100ff80;
    jit.ExtRegs()[5] = 0xc07f642a;
    jit.ExtRegs()[6] = 0xff00017f;
    jit.ExtRegs()[7] = 0x40809cd6;

    // D6: {0, 1, 0x8000, 0xffff}; D7: {0xffff, 0, 0x7fff, 1}.
    jit.ExtRegs()[12] = 0x00010000;
    jit.ExtRegs()[13] = 0xffff8000;
    jit.ExtRegs()[14] = 0x0000ffff;
    jit.ExtRegs()[15] = 0x00017fff;

    // D10: {INT32_MIN, 123456789}; D11: {INT32_MAX, -123456789}.
    jit.ExtRegs()[20] = 0x80000000;
    jit.ExtRegs()[21] = 0x075bcd15;
    jit.ExtRegs()[22] = 0x7fffffff;
    jit.ExtRegs()[23] = 0xf8a432eb;

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 4;
    jit.Run();

    CHECK(jit.ExtRegs()[0] == 0x000200ff);
    CHECK(jit.ExtRegs()[1] == 0x00020000);
    CHECK(jit.ExtRegs()[2] == 0x00c80054);
    CHECK(jit.ExtRegs()[3] == 0x008000ff);

    constexpr std::array<std::uint32_t, 4> unsigned16_expected{
        0x0000ffff, 0x00000001, 0x00000001, 0x0000fffe};
    for (std::size_t lane = 0; lane < unsigned16_expected.size(); ++lane) {
        CAPTURE(lane);
        CHECK(jit.ExtRegs()[16 + lane] == unsigned16_expected[lane]);
    }

    CHECK(jit.ExtRegs()[32] == 0xffffffff);
    CHECK(jit.ExtRegs()[33] == 0x00000000);
    CHECK(jit.ExtRegs()[34] == 0x0eb79a2a);
    CHECK(jit.ExtRegs()[35] == 0x00000000);
}
TEST_CASE("Dynarmic A32 VABAL widens before accumulating with lane wraparound",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf3ce850f,  // VABAL.U8 Q12, D14, D15
        0xf29245a3,  // VABAL.S16 Q2, D18, D19
        0xf3e4c5a5,  // VABAL.U32 Q14, D20, D21
        0xeafffffe,  // B .
        0xeafffffe,
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.ExtRegs() = {};

    // Q12 accumulator and unsigned byte inputs D14/D15.
    jit.ExtRegs()[48] = 0xffffff80;
    jit.ExtRegs()[49] = 0x00010000;
    jit.ExtRegs()[50] = 0xfff07fff;
    jit.ExtRegs()[51] = 0xff010064;
    jit.ExtRegs()[28] = 0x7f020100;
    jit.ExtRegs()[29] = 0xfffec880;
    jit.ExtRegs()[30] = 0x800300ff;
    jit.ExtRegs()[31] = 0x00ff647f;

    // Q2 accumulator and signed halfword inputs D18/D19.
    jit.ExtRegs()[8] = 0xfffffff0;
    jit.ExtRegs()[9] = 0xffffffff;
    jit.ExtRegs()[10] = 0x00000000;
    jit.ExtRegs()[11] = 0x80000000;
    jit.ExtRegs()[36] = 0xffff8000;
    jit.ExtRegs()[37] = 0x7fff0000;
    jit.ExtRegs()[38] = 0x00017fff;
    jit.ExtRegs()[39] = 0x8000ffff;

    // Q14 accumulator and unsigned word inputs D20/D21.
    jit.ExtRegs()[56] = 0xfffffff0;
    jit.ExtRegs()[57] = 0xffffffff;
    jit.ExtRegs()[58] = 0x89abcdef;
    jit.ExtRegs()[59] = 0x01234567;
    jit.ExtRegs()[40] = 0x00000000;
    jit.ExtRegs()[41] = 0xffffffff;
    jit.ExtRegs()[42] = 0xffffffff;
    jit.ExtRegs()[43] = 0x00000001;

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 4;
    jit.Run();

    CHECK(jit.ExtRegs()[48] == 0x0000007f);
    CHECK(jit.ExtRegs()[49] == 0x00020001);
    CHECK(jit.ExtRegs()[50] == 0x00548000);
    CHECK(jit.ExtRegs()[51] == 0x00000065);

    CHECK(jit.ExtRegs()[8] == 0x0000ffef);
    CHECK(jit.ExtRegs()[9] == 0x00000001);
    CHECK(jit.ExtRegs()[10] == 0x00000001);
    CHECK(jit.ExtRegs()[11] == 0x8000ffff);

    CHECK(jit.ExtRegs()[56] == 0xffffffef);
    CHECK(jit.ExtRegs()[57] == 0x00000000);
    CHECK(jit.ExtRegs()[58] == 0x89abcded);
    CHECK(jit.ExtRegs()[59] == 0x01234568);
}

TEST_CASE("Dynarmic A32 VADDL and VSUBL preserve signed, unsigned, and wrapping lanes",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf2820003,  // VADDL.S8 Q0, D2, D3
        0xf3968007,  // VADDL.U16 Q4, D6, D7
        0xf2ea020b,  // VSUBL.S32 Q8, D10, D11
        0xf3ce820f,  // VSUBL.U8 Q12, D14, D15
        0xeafffffe,  // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};
    jit.ExtRegs() = {};

    const auto set_d = [&](std::size_t d, const auto& lanes) {
        static_assert(sizeof(lanes) == sizeof(std::uint64_t));
        std::memcpy(jit.ExtRegs().data() + d * 2, lanes.data(), sizeof(lanes));
    };

    set_d(2, std::array<std::int8_t, 8>{-128, -1, 0, 1, 127, 64, -64, 42});
    set_d(3, std::array<std::int8_t, 8>{127, 1, -1, -2, -128, -64, 64, 42});
    set_d(6, std::array<std::uint16_t, 4>{0, 1, 65535, 32768});
    set_d(7, std::array<std::uint16_t, 4>{65535, 2, 1, 32768});
    set_d(10, std::array<std::int32_t, 2>{INT32_MIN, INT32_MAX});
    set_d(11, std::array<std::int32_t, 2>{INT32_MAX, INT32_MIN});
    set_d(14, std::array<std::uint8_t, 8>{0, 255, 1, 128, 42, 200, 250, 5});
    set_d(15, std::array<std::uint8_t, 8>{255, 0, 2, 129, 100, 201, 249, 6});

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    std::array<std::int16_t, 8> signed8_result{};
    std::array<std::uint32_t, 4> unsigned16_result{};
    std::array<std::int64_t, 2> signed32_result{};
    std::array<std::uint16_t, 8> unsigned8_result{};
    std::memcpy(signed8_result.data(), jit.ExtRegs().data(), sizeof(signed8_result));
    std::memcpy(unsigned16_result.data(), jit.ExtRegs().data() + 16, sizeof(unsigned16_result));
    std::memcpy(signed32_result.data(), jit.ExtRegs().data() + 32, sizeof(signed32_result));
    std::memcpy(unsigned8_result.data(), jit.ExtRegs().data() + 48, sizeof(unsigned8_result));

    CHECK((signed8_result == std::array<std::int16_t, 8>{-1, 0, -1, -1, -1, 0, 0, 84}));
    CHECK((unsigned16_result == std::array<std::uint32_t, 4>{65535, 3, 65536, 65536}));
    CHECK((signed32_result == std::array<std::int64_t, 2>{-4294967295LL, 4294967295LL}));
    CHECK((unsigned8_result ==
           std::array<std::uint16_t, 8>{65281, 255, 65535, 65535, 65478, 65535, 1, 65535}));
}

TEST_CASE("Dynarmic A32 VADDW and VSUBW preserve the wide operand and lane wraparound",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf284010a,  // VADDW.S8 Q0, Q2, D10
        0xf39c810e,  // VADDW.U16 Q4, Q6, D14
        0xf2e403a6,  // VSUBW.S32 Q8, Q10, D22
        0xf3cc83ae,  // VSUBW.U8 Q12, Q14, D30
        0xeafffffe,  // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};
    jit.ExtRegs() = {};

    const auto set_vector = [&](std::size_t first_s_register, const auto& lanes) {
        std::memcpy(jit.ExtRegs().data() + first_s_register, lanes.data(), sizeof(lanes));
    };

    set_vector(8, std::array<std::int16_t, 8>{-32768, -1, 0, 1, 32767, 100, -100, 30000});
    set_vector(20, std::array<std::int8_t, 8>{-1, 1, -1, 2, 1, -100, 100, 127});
    set_vector(24, std::array<std::uint32_t, 4>{0, UINT32_MAX, 100, 4000000000U});
    set_vector(28, std::array<std::uint16_t, 4>{65535, 1, 65535, 65535});
    set_vector(40, std::array<std::int64_t, 2>{INT64_MIN, INT64_MAX});
    set_vector(44, std::array<std::int32_t, 2>{1, -1});
    set_vector(56, std::array<std::uint16_t, 8>{0, 255, 1, 128, 65535, 1000, 5, 42});
    set_vector(60, std::array<std::uint8_t, 8>{1, 255, 2, 129, 255, 1, 6, 42});

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    std::array<std::int16_t, 8> signed8_result{};
    std::array<std::uint32_t, 4> unsigned16_result{};
    std::array<std::int64_t, 2> signed32_result{};
    std::array<std::uint16_t, 8> unsigned8_result{};
    std::memcpy(signed8_result.data(), jit.ExtRegs().data(), sizeof(signed8_result));
    std::memcpy(unsigned16_result.data(), jit.ExtRegs().data() + 16, sizeof(unsigned16_result));
    std::memcpy(signed32_result.data(), jit.ExtRegs().data() + 32, sizeof(signed32_result));
    std::memcpy(unsigned8_result.data(), jit.ExtRegs().data() + 48, sizeof(unsigned8_result));

    CHECK((signed8_result ==
           std::array<std::int16_t, 8>{32767, 0, -1, 3, -32768, 0, 0, 30127}));
    CHECK((unsigned16_result ==
           std::array<std::uint32_t, 4>{65535, 0, 65635, 4000065535U}));
    CHECK((signed32_result == std::array<std::int64_t, 2>{INT64_MAX, INT64_MIN}));
    CHECK((unsigned8_result ==
           std::array<std::uint16_t, 8>{65535, 0, 65535, 65535, 65280, 999, 65535, 0}));
}

TEST_CASE("Dynarmic A32 VZIP keeps both D-register results in SIMD",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf3b20181,  // VZIP.8 D0, D1
        0xf3f6e1af,  // VZIP.16 D30, D31
        0xeafffffe,  // B .
        0xeafffffe,
        0xeafffffe,
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.ExtRegs() = {};
    jit.ExtRegs()[0] = 0x33221100;
    jit.ExtRegs()[1] = 0x77665544;
    jit.ExtRegs()[2] = 0xb3a29180;
    jit.ExtRegs()[3] = 0xf7e6d5c4;
    jit.ExtRegs()[60] = 0x22330001;
    jit.ExtRegs()[61] = 0x66774455;
    jit.ExtRegs()[62] = 0xaabb8899;
    jit.ExtRegs()[63] = 0xeeffccdd;

    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 3;
    jit.Run();

    CHECK(jit.ExtRegs()[0] == 0x91118000);
    CHECK(jit.ExtRegs()[1] == 0xb333a222);
    CHECK(jit.ExtRegs()[2] == 0xd555c444);
    CHECK(jit.ExtRegs()[3] == 0xf777e666);
    CHECK(jit.ExtRegs()[60] == 0x88990001);
    CHECK(jit.ExtRegs()[61] == 0xaabb2233);
    CHECK(jit.ExtRegs()[62] == 0xccdd4455);
    CHECK(jit.ExtRegs()[63] == 0xeeff6677);
}

TEST_CASE("Dynarmic A32 SEL preserves per-byte GE selection", "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xe6810fb2,  // SEL R0, R1, R2
        0xeafffffe,  // B .
        0xeafffffe,
        0xeafffffe,
        0xeafffffe,
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    constexpr std::uint32_t from = 0x11223344;
    constexpr std::uint32_t to = 0xaabbccdd;
    for (std::uint32_t ge = 0; ge < 16; ++ge) {
        std::uint32_t expected = 0;
        for (std::uint32_t byte = 0; byte < 4; ++byte) {
            const std::uint32_t byte_mask = 0xffu << (byte * 8);
            expected |= ((ge & (1u << byte)) != 0 ? from : to) & byte_mask;
        }

        CAPTURE(ge);
        jit.Regs() = {};
        jit.Regs()[1] = from;
        jit.Regs()[2] = to;
        jit.SetCpsr(0x000001d0 | ge << 16);  // User mode plus GE[3:0]
        callbacks.ticks_left = 2;
        jit.Run();

        CHECK(jit.Regs()[0] == expected);
        CHECK((jit.Cpsr() & 0x000f0000) == ge << 16);
    }
}

TEST_CASE("Dynarmic A32 mixed halving add-sub preserves rounding and underflow",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xe6310f32,  // SHASX R0, R1, R2
        0xe6343f55,  // SHSAX R3, R4, R5
        0xe6776f38,  // UHASX R6, R7, R8
        0xe67a9f5b,  // UHSAX R9, R10, R11
        0xeafffffe,  // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.Regs() = {};
    jit.Regs()[1] = 0x80007fff;   // {-32768, 32767}
    jit.Regs()[2] = 0xffff0001;   // {-1, 1}
    jit.Regs()[4] = 0x7fff8000;   // {32767, -32768}
    jit.Regs()[5] = 0x0001ffff;   // {1, -1}
    jit.Regs()[7] = 0x00000000;
    jit.Regs()[8] = 0xffffffff;
    jit.Regs()[10] = 0x00000000;
    jit.Regs()[11] = 0xffffffff;
    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    CHECK(jit.Regs()[0] == 0xc0004000);
    CHECK(jit.Regs()[3] == 0x4000c000);
    CHECK(jit.Regs()[6] == 0x7fff8000);
    CHECK(jit.Regs()[9] == 0x80007fff);
}

TEST_CASE("Dynarmic A32 mixed add-sub preserves wrapping lanes and GE flags",
          "[core][arm][dynarmic]") {
    struct MixedInstruction {
        std::uint32_t instruction;
        bool add_is_hi;
        bool is_signed;
    };
    constexpr std::array mixed_instructions{
        MixedInstruction{0xe6110f32, true, true},    // SASX R0, R1, R2
        MixedInstruction{0xe6110f52, false, true},   // SSAX R0, R1, R2
        MixedInstruction{0xe6510f32, true, false},   // UASX R0, R1, R2
        MixedInstruction{0xe6510f52, false, false},  // USAX R0, R1, R2
    };
    constexpr std::array input_cases{
        std::pair{0x00000000u, 0x00000000u},
        std::pair{0xffffffffu, 0x00010001u},
        std::pair{0x7fff8000u, 0x7fff7fffu},
        std::pair{0x80007fffu, 0x80000001u},
        std::pair{0x0000ffffu, 0xffff0000u},
        std::pair{0xffff0000u, 0x0000ffffu},
        std::pair{0x1234abcdu, 0xfedc5678u},
        std::pair{0x80008000u, 0x80008000u},
    };

    for (const auto& mixed_instruction : mixed_instructions) {
        for (const auto& [rn, rm] : input_cases) {
            const auto evaluate_lane = [&](std::uint16_t a, std::uint16_t b, bool add) {
                std::int32_t full_result;
                bool ge;
                if (mixed_instruction.is_signed) {
                    full_result = add ? static_cast<std::int16_t>(a) + static_cast<std::int16_t>(b)
                                      : static_cast<std::int16_t>(a) - static_cast<std::int16_t>(b);
                    ge = full_result >= 0;
                } else if (add) {
                    const std::uint32_t unsigned_result =
                        static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b);
                    full_result = static_cast<std::int32_t>(unsigned_result);
                    ge = unsigned_result >= 0x10000;
                } else {
                    full_result = static_cast<std::int32_t>(a) - static_cast<std::int32_t>(b);
                    ge = a >= b;
                }
                return std::pair{static_cast<std::uint16_t>(full_result), ge};
            };

            const auto [low_result, low_ge] =
                evaluate_lane(static_cast<std::uint16_t>(rn), static_cast<std::uint16_t>(rm >> 16),
                              !mixed_instruction.add_is_hi);
            const auto [high_result, high_ge] =
                evaluate_lane(static_cast<std::uint16_t>(rn >> 16), static_cast<std::uint16_t>(rm),
                              mixed_instruction.add_is_hi);
            const std::uint32_t expected_result =
                low_result | static_cast<std::uint32_t>(high_result) << 16;
            const std::uint32_t expected_ge = (low_ge ? 0b0011 : 0) | (high_ge ? 0b1100 : 0);

            CAPTURE(mixed_instruction.instruction, rn, rm, expected_result, expected_ge);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                mixed_instruction.instruction,
                0xeafffffe,  // B .
                0xeafffffe,
                0xeafffffe,
                0xeafffffe,
                0xeafffffe,
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};

            jit.Regs() = {};
            jit.Regs()[1] = rn;
            jit.Regs()[2] = rm;
            jit.SetCpsr(0xf80f01d0);  // NZCV/Q/GE plus User mode
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == expected_result);
            CHECK((jit.Cpsr() & 0xf80f0000) == (0xf8000000 | expected_ge << 16));
        }
    }
}

TEST_CASE("Dynarmic A32 mixed saturated add-sub preserves lanes and flags",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xe6210f32,  // QASX R0, R1, R2
        0xe6243f55,  // QSAX R3, R4, R5
        0xe6676f38,  // UQASX R6, R7, R8
        0xe66a9f5b,  // UQSAX R9, R10, R11
        0xeafffffe,  // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.Regs() = {};
    jit.Regs()[1] = 0x7fff8000;
    jit.Regs()[2] = 0x7fff7fff;
    jit.Regs()[4] = 0x80007fff;
    jit.Regs()[5] = 0x7fff7fff;
    jit.Regs()[7] = 0xffff0000;
    jit.Regs()[8] = 0xffff0001;
    jit.Regs()[10] = 0x0000ffff;
    jit.Regs()[11] = 0x0001ffff;
    constexpr std::uint32_t initial_cpsr = 0xf80f01d0;  // NZCV/Q/GE plus User mode
    jit.SetCpsr(initial_cpsr);
    callbacks.ticks_left = 5;
    jit.Run();

    CHECK(jit.Regs()[0] == 0x7fff8000);
    CHECK(jit.Regs()[3] == 0x80007fff);
    CHECK(jit.Regs()[6] == 0xffff0000);
    CHECK(jit.Regs()[9] == 0x0000ffff);
    CHECK((jit.Cpsr() & 0xf80f0000) == (initial_cpsr & 0xf80f0000));
}

TEST_CASE("Dynarmic A32 signed narrowing preserves extension and shift semantics",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xe6af1070,  // SXTB R1, R0
        0xe6bf2070,  // SXTH R2, R0
        0xe1630480,  // SMULBB R3, R0, R4
        0xe1a05410,  // LSL R5, R0, R4
        0xeafffffe,  // B .
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    jit.Regs() = {};
    jit.Regs()[0] = 0x12348081;
    jit.Regs()[4] = 0xffff0001;
    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 5;
    jit.Run();

    CHECK(jit.Regs()[1] == 0xffffff81);
    CHECK(jit.Regs()[2] == 0xffff8081);
    CHECK(jit.Regs()[3] == 0xffff8081);
    CHECK(jit.Regs()[5] == 0x24690102);
}

TEST_CASE("Dynarmic A32 signed dual multiply-long preserves 64-bit edge semantics",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        bool exchange;
        bool subtract;
        bool source_alias;
        bool thumb;
    };
    constexpr std::array operations{
        Operation{0xe7410312, false, false, false, false},  // ARM SMLALD R0, R1, R2, R3
        Operation{0xe7410332, true, false, false, false},   // ARM SMLALDX R0, R1, R2, R3
        Operation{0xe7410352, false, true, false, false},   // ARM SMLSLD R0, R1, R2, R3
        Operation{0xe7410372, true, true, false, false},    // ARM SMLSLDX R0, R1, R2, R3
        Operation{0xe7410110, false, false, true, false},   // ARM SMLALD R0, R1, R0, R1
        Operation{0xe7410130, true, false, true, false},    // ARM SMLALDX R0, R1, R0, R1
        Operation{0xe7410150, false, true, true, false},    // ARM SMLSLD R0, R1, R0, R1
        Operation{0xe7410170, true, true, true, false},     // ARM SMLSLDX R0, R1, R0, R1
        Operation{0x01c3fbc2, false, false, false, true},  // Thumb SMLALD R0, R1, R2, R3
        Operation{0x01d3fbc2, true, false, false, true},   // Thumb SMLALDX R0, R1, R2, R3
        Operation{0x01c3fbd2, false, true, false, true},   // Thumb SMLSLD R0, R1, R2, R3
        Operation{0x01d3fbd2, true, true, false, true},    // Thumb SMLSLDX R0, R1, R2, R3
        Operation{0x01c1fbc0, false, false, true, true},   // Thumb SMLALD R0, R1, R0, R1
        Operation{0x01d1fbc0, true, false, true, true},    // Thumb SMLALDX R0, R1, R0, R1
        Operation{0x01c1fbd0, false, true, true, true},    // Thumb SMLSLD R0, R1, R0, R1
        Operation{0x01d1fbd0, true, true, true, true},     // Thumb SMLSLDX R0, R1, R0, R1
    };
    struct Inputs {
        std::uint64_t addend;
        std::uint32_t n;
        std::uint32_t m;
    };
    constexpr std::array inputs{
        Inputs{0x0123456789abcdef, 0x80017fff, 0x7fff8001},
        Inputs{0, 0x80008000, 0x80008000},
        Inputs{0xffffffffffffffff, 0x7fff8000, 0x80007fff},
        Inputs{0x8000000000000000, 0xffffffff, 0x0001ffff},
        Inputs{0x7fffffffffffffff, 0x7fff7fff, 0x7fff7fff},
        Inputs{0xdeadbeef01234567, 0x00010002, 0xfffeffff},
    };
    const auto reference = [](std::uint64_t addend, std::uint32_t n, std::uint32_t m,
                              bool exchange, bool subtract) {
        const auto n_lo = static_cast<std::int64_t>(static_cast<std::int16_t>(n));
        const auto n_hi = static_cast<std::int64_t>(static_cast<std::int16_t>(n >> 16));
        const auto m_lo = static_cast<std::int64_t>(static_cast<std::int16_t>(m));
        const auto m_hi = static_cast<std::int64_t>(static_cast<std::int16_t>(m >> 16));
        const auto product_lo = exchange ? n_lo * m_hi : n_lo * m_lo;
        const auto product_hi = exchange ? n_hi * m_lo : n_hi * m_hi;
        const auto partial = addend + static_cast<std::uint64_t>(product_lo);
        return subtract ? partial - static_cast<std::uint64_t>(product_hi)
                        : partial + static_cast<std::uint64_t>(product_hi);
    };

    for (const auto& operation : operations) {
        for (const auto& input : inputs) {
            CAPTURE(operation.instruction, input.addend, input.n, input.m);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                operation.instruction,
                operation.thumb ? 0xe7fee7fe : 0xeafffffe,  // B .
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};
            const std::uint32_t n = operation.source_alias
                                        ? static_cast<std::uint32_t>(input.addend)
                                        : input.n;
            const std::uint32_t m = operation.source_alias
                                        ? static_cast<std::uint32_t>(input.addend >> 32)
                                        : input.m;
            const std::uint64_t expected =
                reference(input.addend, n, m, operation.exchange, operation.subtract);
            jit.Regs() = {};
            jit.Regs()[0] = static_cast<std::uint32_t>(input.addend);
            jit.Regs()[1] = static_cast<std::uint32_t>(input.addend >> 32);
            jit.Regs()[2] = input.n;
            jit.Regs()[3] = input.m;
            const std::uint32_t initial_cpsr =
                0xf80f01d0 | (operation.thumb ? 0x20 : 0);  // NZCV/Q/GE, state, User mode
            jit.SetCpsr(initial_cpsr);
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == static_cast<std::uint32_t>(expected));
            CHECK(jit.Regs()[1] == static_cast<std::uint32_t>(expected >> 32));
            if (!operation.source_alias) {
                CHECK(jit.Regs()[2] == input.n);
                CHECK(jit.Regs()[3] == input.m);
            }
            CHECK((jit.Cpsr() & 0xf80f0000) == (initial_cpsr & 0xf80f0000));
        }
    }
}

TEST_CASE("Dynarmic A32 signed multiply-accumulate-long preserves 64-bit edge semantics",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        bool halfword;
        bool n_top;
        bool m_top;
        bool source_alias;
        bool thumb;
        bool update_flags;
    };
    constexpr std::array operations{
        Operation{0xe0e10392, false, false, false, false, false, false},  // ARM SMLAL R0, R1, R2, R3
        Operation{0xe0f10392, false, false, false, false, false, true},   // ARM SMLALS R0, R1, R2, R3
        Operation{0xe0e10190, false, false, false, true, false, false},   // ARM SMLAL R0, R1, R0, R1
        Operation{0x0103fbc2, false, false, false, false, true, false},  // Thumb SMLAL R0, R1, R2, R3
        Operation{0x0101fbc0, false, false, false, true, true, false},   // Thumb SMLAL R0, R1, R0, R1
        Operation{0xe1410382, true, false, false, false, false, false},  // ARM SMLALBB R0, R1, R2, R3
        Operation{0xe14103e2, true, true, true, false, false, false},    // ARM SMLALTT R0, R1, R2, R3
        Operation{0xe1410180, true, false, false, true, false, false},   // ARM SMLALBB R0, R1, R0, R1
        Operation{0x0183fbc2, true, false, false, false, true, false},  // Thumb SMLALBB R0, R1, R2, R3
        Operation{0x01b3fbc2, true, true, true, false, true, false},    // Thumb SMLALTT R0, R1, R2, R3
        Operation{0x0181fbc0, true, false, false, true, true, false},   // Thumb SMLALBB R0, R1, R0, R1
    };
    struct Inputs {
        std::uint64_t addend;
        std::uint32_t n;
        std::uint32_t m;
    };
    constexpr std::array inputs{
        Inputs{0x0123456789abcdef, 0x80017fff, 0x7fff8001},
        Inputs{0, 0x80000000, 0xffffffff},
        Inputs{0xffffffffffffffff, 0x7fffffff, 0x7fffffff},
        Inputs{0x8000000000000000, 0xffffffff, 0x00010001},
        Inputs{0x7fffffffffffffff, 0x80008000, 0x80008000},
        Inputs{0xdeadbeef01234567, 0x00010002, 0xfffeffff},
    };
    const auto reference = [](std::uint64_t addend, std::uint32_t n, std::uint32_t m,
                              const Operation& operation) {
        const auto signed_n = operation.halfword
                                  ? static_cast<std::int64_t>(static_cast<std::int16_t>(
                                        n >> (operation.n_top ? 16 : 0)))
                                  : static_cast<std::int64_t>(static_cast<std::int32_t>(n));
        const auto signed_m = operation.halfword
                                  ? static_cast<std::int64_t>(static_cast<std::int16_t>(
                                        m >> (operation.m_top ? 16 : 0)))
                                  : static_cast<std::int64_t>(static_cast<std::int32_t>(m));
        return addend + static_cast<std::uint64_t>(signed_n * signed_m);
    };

    for (const auto& operation : operations) {
        for (const auto& input : inputs) {
            CAPTURE(operation.instruction, input.addend, input.n, input.m);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                operation.instruction,
                operation.thumb ? 0xe7fee7fe : 0xeafffffe,  // B .
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};
            const std::uint32_t n = operation.source_alias
                                        ? static_cast<std::uint32_t>(input.addend)
                                        : input.n;
            const std::uint32_t m = operation.source_alias
                                        ? static_cast<std::uint32_t>(input.addend >> 32)
                                        : input.m;
            const std::uint64_t expected = reference(input.addend, n, m, operation);
            jit.Regs() = {};
            jit.Regs()[0] = static_cast<std::uint32_t>(input.addend);
            jit.Regs()[1] = static_cast<std::uint32_t>(input.addend >> 32);
            jit.Regs()[2] = input.n;
            jit.Regs()[3] = input.m;
            constexpr std::uint32_t initial_flags = 0x780f0000;  // ZCV/Q/GE
            const std::uint32_t initial_cpsr =
                initial_flags | 0x000001d0 | (operation.thumb ? 0x20 : 0);
            jit.SetCpsr(initial_cpsr);
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == static_cast<std::uint32_t>(expected));
            CHECK(jit.Regs()[1] == static_cast<std::uint32_t>(expected >> 32));
            if (!operation.source_alias) {
                CHECK(jit.Regs()[2] == input.n);
                CHECK(jit.Regs()[3] == input.m);
            }
            const std::uint32_t expected_flags = operation.update_flags
                                                     ? (initial_flags & 0x380f0000) |
                                                           (expected == 0 ? 0x40000000 : 0) |
                                                           (expected >> 63 != 0 ? 0x80000000 : 0)
                                                     : initial_flags;
            CHECK((jit.Cpsr() & 0xf80f0000) == expected_flags);
        }
    }
}

TEST_CASE("Dynarmic A32 signed most-significant-word multiplies preserve edge semantics",
          "[core][arm][dynarmic]") {
    enum class MultiplyKind {
        Smmul,
        Smmla,
        Smmls,
    };
    struct Operation {
        std::uint32_t instruction;
        MultiplyKind kind;
        bool rounded;
        bool source_alias;
        bool thumb;
    };
    constexpr std::array operations{
        Operation{0xe750f312, MultiplyKind::Smmul, false, false, false}, // ARM SMMUL R0, R2, R3
        Operation{0xe750f332, MultiplyKind::Smmul, true, false, false},  // ARM SMMULR R0, R2, R3
        Operation{0xe7504312, MultiplyKind::Smmla, false, false, false}, // ARM SMMLA R0, R2, R3, R4
        Operation{0xe7504332, MultiplyKind::Smmla, true, false, false}, // ARM SMMLAR R0, R2, R3, R4
        Operation{0xe75043d2, MultiplyKind::Smmls, false, false, false}, // ARM SMMLS R0, R2, R3, R4
        Operation{0xe75043f2, MultiplyKind::Smmls, true, false, false}, // ARM SMMLSR R0, R2, R3, R4
        Operation{0xe750f110, MultiplyKind::Smmul, false, true, false}, // ARM SMMUL R0, R0, R1
        Operation{0xe7500110, MultiplyKind::Smmla, false, true, false}, // ARM SMMLA R0, R0, R1, R0
        Operation{0xe75001d0, MultiplyKind::Smmls, false, true, false}, // ARM SMMLS R0, R0, R1, R0
        Operation{0xf003fb52, MultiplyKind::Smmul, false, false, true}, // Thumb SMMUL R0, R2, R3
        Operation{0xf013fb52, MultiplyKind::Smmul, true, false, true},  // Thumb SMMULR R0, R2, R3
        Operation{0x4003fb52, MultiplyKind::Smmla, false, false,
                  true}, // Thumb SMMLA R0, R2, R3, R4
        Operation{0x4013fb52, MultiplyKind::Smmla, true, false,
                  true}, // Thumb SMMLAR R0, R2, R3, R4
        Operation{0x4003fb62, MultiplyKind::Smmls, false, false,
                  true}, // Thumb SMMLS R0, R2, R3, R4
        Operation{0x4013fb62, MultiplyKind::Smmls, true, false,
                  true}, // Thumb SMMLSR R0, R2, R3, R4
        Operation{0xf001fb50, MultiplyKind::Smmul, false, true, true}, // Thumb SMMUL R0, R0, R1
        Operation{0x0001fb50, MultiplyKind::Smmla, false, true, true}, // Thumb SMMLA R0, R0, R1, R0
        Operation{0x0001fb60, MultiplyKind::Smmls, false, true, true}, // Thumb SMMLS R0, R0, R1, R0
    };
    struct Inputs {
        std::uint32_t a;
        std::uint32_t n;
        std::uint32_t m;
    };
    constexpr std::array inputs{
        Inputs{0x00000000, 0x80000000, 0xffffffff}, Inputs{0x00000000, 0x7fffffff, 0x7fffffff},
        Inputs{0xffffffff, 0x80000000, 0x00000002}, Inputs{0x80000000, 0x80000000, 0x80000000},
        Inputs{0x7fffffff, 0xffffffff, 0x7fffffff}, Inputs{0x12345678, 0x87654321, 0xfedcba98},
    };

    for (const auto& operation : operations) {
        for (const auto& input : inputs) {
            CAPTURE(operation.instruction, input.a, input.n, input.m);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                operation.instruction,
                operation.thumb ? 0xe7fee7fe : 0xeafffffe, // B .
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};

            const std::uint32_t n =
                operation.source_alias && operation.kind != MultiplyKind::Smmul ? input.a : input.n;
            const auto signed_n = static_cast<std::int64_t>(static_cast<std::int32_t>(n));
            const auto signed_m = static_cast<std::int64_t>(static_cast<std::int32_t>(input.m));
            const std::uint64_t product = static_cast<std::uint64_t>(signed_n * signed_m);
            const std::uint64_t addend = static_cast<std::uint64_t>(input.a) << 32;
            std::uint64_t intermediate{};
            switch (operation.kind) {
            case MultiplyKind::Smmul:
                intermediate = product;
                break;
            case MultiplyKind::Smmla:
                intermediate = addend + product;
                break;
            case MultiplyKind::Smmls:
                intermediate = addend - product;
                break;
            }
            std::uint32_t expected = static_cast<std::uint32_t>(intermediate >> 32);
            if (operation.rounded) {
                expected += static_cast<std::uint32_t>((intermediate >> 31) & 1);
            }

            jit.Regs() = {};
            if (operation.source_alias) {
                jit.Regs()[0] = operation.kind == MultiplyKind::Smmul ? input.n : input.a;
                jit.Regs()[1] = input.m;
            } else {
                jit.Regs()[0] = 0xdeadbeef;
                jit.Regs()[2] = input.n;
                jit.Regs()[3] = input.m;
                jit.Regs()[4] = input.a;
            }
            constexpr std::uint32_t initial_flags = 0xf80f0000; // NZCV/Q/GE
            jit.SetCpsr(initial_flags | 0x000001d0 | (operation.thumb ? 0x20 : 0));
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == expected);
            CHECK((jit.Cpsr() & 0xf80f0000) == initial_flags);
        }
    }
}

TEST_CASE("Dynarmic A32 signed word-by-halfword multiplies preserve edge semantics",
          "[core][arm][dynarmic]") {
    struct Operation {
        std::uint32_t instruction;
        bool top;
        bool source_alias;
        bool thumb;
    };
    constexpr std::array operations{
        Operation{0xe12003a2, false, false, false}, // ARM SMULWB R0, R2, R3
        Operation{0xe12003e2, true, false, false},  // ARM SMULWT R0, R2, R3
        Operation{0xe12001a0, false, true, false},  // ARM SMULWB R0, R0, R1
        Operation{0xe12001e0, true, true, false},   // ARM SMULWT R0, R0, R1
        Operation{0xf003fb32, false, false, true}, // Thumb SMULWB R0, R2, R3
        Operation{0xf013fb32, true, false, true},  // Thumb SMULWT R0, R2, R3
        Operation{0xf001fb30, false, true, true},  // Thumb SMULWB R0, R0, R1
        Operation{0xf011fb30, true, true, true},   // Thumb SMULWT R0, R0, R1
    };
    struct Inputs {
        std::uint32_t n;
        std::uint32_t m;
    };
    constexpr std::array inputs{
        Inputs{0x80000000, 0x7fff8000}, Inputs{0x7fffffff, 0x80007fff},
        Inputs{0xffffffff, 0x0001ffff}, Inputs{0x00000001, 0xffff0001},
        Inputs{0x87654321, 0x13579bdf}, Inputs{0x00000000, 0x8000ffff},
    };

    for (const auto& operation : operations) {
        for (const auto& input : inputs) {
            CAPTURE(operation.instruction, input.n, input.m);
            ArmTestCallbacks callbacks;
            callbacks.code = {
                operation.instruction,
                operation.thumb ? 0xe7fee7fe : 0xeafffffe, // B .
            };
            Dynarmic::A32::UserConfig config{&callbacks};
            Dynarmic::A32::Jit jit{config};

            const auto signed_n = static_cast<std::int64_t>(static_cast<std::int32_t>(input.n));
            const auto signed_m = static_cast<std::int64_t>(
                static_cast<std::int16_t>(input.m >> (operation.top ? 16 : 0)));
            const auto product = static_cast<std::uint64_t>(signed_n * signed_m);
            const auto expected = static_cast<std::uint32_t>(product >> 16);

            jit.Regs() = {};
            if (operation.source_alias) {
                jit.Regs()[0] = input.n;
                jit.Regs()[1] = input.m;
            } else {
                jit.Regs()[0] = 0xdeadbeef;
                jit.Regs()[2] = input.n;
                jit.Regs()[3] = input.m;
            }
            constexpr std::uint32_t initial_flags = 0xf80f0000; // NZCV/Q/GE
            jit.SetCpsr(initial_flags | 0x000001d0 | (operation.thumb ? 0x20 : 0));
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[0] == expected);
            if (operation.source_alias) {
                CHECK(jit.Regs()[1] == input.m);
            } else {
                CHECK(jit.Regs()[2] == input.n);
                CHECK(jit.Regs()[3] == input.m);
            }
            CHECK((jit.Cpsr() & 0xf80f0000) == initial_flags);
        }
    }
}

TEST_CASE("Dynarmic A32 register shifts preserve the complete byte-sized amount",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xe1a05410,  // LSL R5, R0, R4
        0xe1a06430,  // LSR R6, R0, R4
        0xe1a07450,  // ASR R7, R0, R4
        0xe1a08470,  // ROR R8, R0, R4
        0xe1b09410,  // LSLS R9, R0, R4
        0xeafffffe,  // B .
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    constexpr std::uint32_t operand = 0x81234567;
    constexpr std::array shift_registers{
        0xabcdef00u,
        0xabcdef01u,
        0xabcdef1fu,
        0xabcdef20u,
        0xabcdef21u,
        0xabcdefffu,
    };

    for (const std::uint32_t shift_register : shift_registers) {
        const auto shift = static_cast<std::uint8_t>(shift_register);
        const std::uint32_t expected_lsl = shift == 0 ? operand : shift < 32 ? operand << shift : 0;
        const std::uint32_t expected_lsr = shift == 0 ? operand : shift < 32 ? operand >> shift : 0;
        const std::uint32_t expected_asr =
            shift == 0 ? operand
                       : shift < 32 ? (operand >> shift) | (~0u << (32 - shift)) : ~0u;
        const auto rotate = static_cast<std::uint8_t>(shift & 31);
        const std::uint32_t expected_ror =
            rotate == 0 ? operand : (operand >> rotate) | (operand << (32 - rotate));
        const std::uint32_t expected_carry =
            shift == 0 ? 1 : shift <= 32 ? (operand >> (32 - shift)) & 1 : 0;

        CAPTURE(shift_register, shift);
        jit.Regs() = {};
        jit.Regs()[0] = operand;
        jit.Regs()[4] = shift_register;
        jit.SetCpsr(0x200001d0);  // User mode plus carry set
        callbacks.ticks_left = 6;
        jit.Run();

        CHECK(jit.Regs()[5] == expected_lsl);
        CHECK(jit.Regs()[6] == expected_lsr);
        CHECK(jit.Regs()[7] == expected_asr);
        CHECK(jit.Regs()[8] == expected_ror);
        CHECK(jit.Regs()[9] == expected_lsl);
        CHECK(((jit.Cpsr() >> 29) & 1) == expected_carry);
    }
}

TEST_CASE("Dynarmic A32 register shifts preserve carry for dirty upper amounts",
          "[core][arm][dynarmic]") {
    enum class ShiftKind {
        Lsl,
        Lsr,
        Asr,
        Ror,
    };
    struct ShiftCase {
        std::uint32_t instruction;
        ShiftKind kind;
    };

    constexpr std::array shift_cases{
        ShiftCase{0xe1b05410, ShiftKind::Lsl},  // LSLS R5, R0, R4
        ShiftCase{0xe1b05430, ShiftKind::Lsr},  // LSRS R5, R0, R4
        ShiftCase{0xe1b05450, ShiftKind::Asr},  // ASRS R5, R0, R4
        ShiftCase{0xe1b05470, ShiftKind::Ror},  // RORS R5, R0, R4
    };
    constexpr std::array shift_registers{
        0xabcdef00u,
        0xabcdef01u,
        0xabcdef1fu,
        0xabcdef20u,
        0xabcdef21u,
        0xabcdefffu,
    };
    constexpr std::uint32_t operand = 0x81234567;

    for (const auto& test : shift_cases) {
        ArmTestCallbacks callbacks;
        callbacks.code = {
            test.instruction,
            0xeafffffe,  // B .
        };
        Dynarmic::A32::UserConfig config{&callbacks};
        Dynarmic::A32::Jit jit{config};

        for (const std::uint32_t shift_register : shift_registers) {
            const auto shift = static_cast<std::uint8_t>(shift_register);
            std::uint32_t expected_result{};
            std::uint32_t expected_carry{};
            switch (test.kind) {
            case ShiftKind::Lsl:
                expected_result = shift == 0 ? operand : shift < 32 ? operand << shift : 0;
                expected_carry =
                    shift == 0 ? 1 : shift <= 32 ? (operand >> (32 - shift)) & 1 : 0;
                break;
            case ShiftKind::Lsr:
                expected_result = shift == 0 ? operand : shift < 32 ? operand >> shift : 0;
                expected_carry =
                    shift == 0 ? 1 : shift <= 32 ? (operand >> (shift - 1)) & 1 : 0;
                break;
            case ShiftKind::Asr:
                expected_result =
                    shift == 0 ? operand
                               : shift < 32 ? (operand >> shift) | (~0u << (32 - shift)) : ~0u;
                expected_carry =
                    shift == 0 ? 1
                               : shift <= 32 ? (operand >> (shift - 1)) & 1 : operand >> 31;
                break;
            case ShiftKind::Ror: {
                const auto rotate = static_cast<std::uint8_t>(shift & 31);
                expected_result =
                    rotate == 0 ? operand : (operand >> rotate) | (operand << (32 - rotate));
                expected_carry = shift == 0 ? 1 : expected_result >> 31;
                break;
            }
            }

            CAPTURE(test.instruction, shift_register, shift);
            jit.Regs() = {};
            jit.Regs()[0] = operand;
            jit.Regs()[4] = shift_register;
            jit.SetCpsr(0x200001d0);  // User mode plus carry set
            callbacks.ticks_left = 2;
            jit.Run();

            CHECK(jit.Regs()[5] == expected_result);
            CHECK(((jit.Cpsr() >> 29) & 1) == expected_carry);
        }
    }
}
