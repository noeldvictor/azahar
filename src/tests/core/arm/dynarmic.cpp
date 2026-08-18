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
