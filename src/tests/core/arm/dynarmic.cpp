// Copyright Azahar Emulator Project / Azahar Thor Experiment
// Licensed under GPLv2 or any later version

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

TEST_CASE("Dynarmic A32 VMLAL and VMLSL widen before modular accumulation",
          "[core][arm][dynarmic]") {
    ArmTestCallbacks callbacks;
    callbacks.code = {
        0xf2820803,  // VMLAL.S8 Q0, D2, D3
        0xf3968807,  // VMLAL.U16 Q4, D6, D7
        0xf2ea080b,  // VMLAL.S32 Q8, D10, D11
        0xf3ce8a0f,  // VMLSL.U8 Q12, D14, D15
        0xeafffffe,  // B .
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
