// Copyright Azahar Emulator Project / Azahar Thor Experiment
// Licensed under GPLv2 or any later version

#include <array>
#include <cstdint>
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
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xe0a21493,  // UMLAL R1, R2, R3, R4
        0xeafffffe,  // B .
        0xeafffffe,
        0xeafffffe,
        0xeafffffe,
        0xeafffffe,
    };

    Dynarmic::A32::UserConfig config{&callbacks};
    Dynarmic::A32::Jit jit{config};

    constexpr std::uint32_t lo = 0x89abcdef;
    constexpr std::uint32_t hi = 0x01234567;
    constexpr std::uint32_t lhs = 0xfedcba98;
    constexpr std::uint32_t rhs = 0x76543210;
    constexpr std::uint64_t expected =
        (static_cast<std::uint64_t>(hi) << 32 | lo) +
        static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs);

    jit.Regs() = {};
    jit.Regs()[1] = lo;
    jit.Regs()[2] = hi;
    jit.Regs()[3] = lhs;
    jit.Regs()[4] = rhs;
    jit.SetCpsr(0x000001d0);  // User mode
    callbacks.ticks_left = 2;
    jit.Run();

    CHECK(jit.Regs()[1] == static_cast<std::uint32_t>(expected));
    CHECK(jit.Regs()[2] == static_cast<std::uint32_t>(expected >> 32));
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
