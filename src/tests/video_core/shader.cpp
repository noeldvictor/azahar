// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>
#include <nihstro/inline_assembly.h>
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/shader/shader_interpreter.h"
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#elif CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif

using JitShader = Pica::Shader::JitShader;
using ShaderInterpreter = Pica::Shader::InterpreterEngine;

using DestRegister = nihstro::DestRegister;
using OpCode = nihstro::OpCode;
using SourceRegister = nihstro::SourceRegister;
using Type = nihstro::InlineAsm::Type;

static constexpr Common::Vec4f vec4_inf = Common::Vec4f::AssignToAll(INFINITY);
static constexpr Common::Vec4f vec4_nan = Common::Vec4f::AssignToAll(NAN);
static constexpr Common::Vec4f vec4_one = Common::Vec4f::AssignToAll(1.0f);
static constexpr Common::Vec4f vec4_zero = Common::Vec4f::AssignToAll(0.0f);

namespace Catch {
template <>
struct StringMaker<Common::Vec2f> {
    static std::string convert(Common::Vec2f value) {
        return fmt::format("({}, {})", value.x, value.y);
    }
};
template <>
struct StringMaker<Common::Vec3f> {
    static std::string convert(Common::Vec3f value) {
        return fmt::format("({}, {}, {})", value.r(), value.g(), value.b());
    }
};
template <>
struct StringMaker<Common::Vec4f> {
    static std::string convert(Common::Vec4f value) {
        return fmt::format("({}, {}, {}, {})", value.r(), value.g(), value.b(), value.a());
    }
};
} // namespace Catch

static std::unique_ptr<Pica::ShaderSetup> CompileShaderSetup(
    std::initializer_list<nihstro::InlineAsm> code) {
    const auto shbin = nihstro::InlineAsm::CompileToRawBinary(code);

    auto shader = std::make_unique<Pica::ShaderSetup>();
    Pica::ProgramCode program_code{};
    Pica::SwizzleData swizzle_data{};
    std::transform(shbin.program.begin(), shbin.program.end(), program_code.begin(),
                   [](const auto& x) { return x.hex; });
    std::transform(shbin.swizzle_table.begin(), shbin.swizzle_table.end(), swizzle_data.begin(),
                   [](const auto& x) { return x.hex; });

    shader->UpdateProgramCode(program_code);
    shader->UpdateSwizzleData(swizzle_data);

    return shader;
}

class ShaderTest {
public:
    explicit ShaderTest(std::initializer_list<nihstro::InlineAsm> code)
        : shader_setup(CompileShaderSetup(code)) {}

    explicit ShaderTest(std::unique_ptr<Pica::ShaderSetup> input_shader_setup)
        : shader_setup(std::move(input_shader_setup)) {}

    virtual ~ShaderTest() = default;

    virtual void RunShader(Pica::ShaderUnit& shader_unit,
                           std::span<const Common::Vec4f> inputs) = 0;

    Common::Vec4f Run(std::span<const Common::Vec4f> inputs) {
        Pica::ShaderUnit shader_unit;
        RunShader(shader_unit, inputs);
        return {shader_unit.output[shader_unit.output_bank][0].x.ToFloat32(),
                shader_unit.output[shader_unit.output_bank][0].y.ToFloat32(),
                shader_unit.output[shader_unit.output_bank][0].z.ToFloat32(),
                shader_unit.output[shader_unit.output_bank][0].w.ToFloat32()};
    }

    Common::Vec4f Run(std::initializer_list<float> inputs) {
        std::vector<Common::Vec4f> input_vecs;
        for (const float& input : inputs) {
            input_vecs.emplace_back(input, 0.0f, 0.0f, 0.0f);
        }
        return Run(input_vecs);
    }

    Common::Vec4f Run(float input) {
        return Run({input});
    }

    Common::Vec4f Run(std::initializer_list<Common::Vec4f> inputs) {
        return Run(std::vector<Common::Vec4f>{inputs});
    }

    void Run(Pica::ShaderUnit& shader_unit, float input) {
        const Common::Vec4f input_vec(input, 0, 0, 0);
        RunShader(shader_unit, {&input_vec, 1});
    }

    std::unique_ptr<Pica::ShaderSetup> shader_setup;
};

class ShaderInterpreterTest : public ShaderTest {
public:
    explicit ShaderInterpreterTest(std::initializer_list<nihstro::InlineAsm> code)
        : ShaderTest(code) {}

    explicit ShaderInterpreterTest(std::unique_ptr<Pica::ShaderSetup> input_shader_setup)
        : ShaderTest(std::move(input_shader_setup)) {}

    void RunShader(Pica::ShaderUnit& shader_unit, std::span<const Common::Vec4f> inputs) override {
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const Common::Vec4f& input = inputs[i];
            shader_unit.input[i].x = Pica::f24::FromFloat32(input.x);
            shader_unit.input[i].y = Pica::f24::FromFloat32(input.y);
            shader_unit.input[i].z = Pica::f24::FromFloat32(input.z);
            shader_unit.input[i].w = Pica::f24::FromFloat32(input.w);
        }
        shader_unit.temporary.fill(Common::Vec4<Pica::f24>::AssignToAll(Pica::f24::Zero()));
        shader_interpreter.Run(*shader_setup, shader_unit);
    }

private:
    ShaderInterpreter shader_interpreter;
};

class ShaderJitTest : public ShaderTest {
public:
    explicit ShaderJitTest(std::initializer_list<nihstro::InlineAsm> code) : ShaderTest(code) {
        const auto& program_code = shader_setup->GetProgramCode();
        const auto& swizzle_data = shader_setup->GetSwizzleData();
        shader_jit.Compile(&program_code, &swizzle_data);
    }

    explicit ShaderJitTest(std::unique_ptr<Pica::ShaderSetup> input_shader_setup)
        : ShaderTest(std::move(input_shader_setup)) {
        const auto& program_code = shader_setup->GetProgramCode();
        const auto& swizzle_data = shader_setup->GetSwizzleData();
        shader_jit.Compile(&program_code, &swizzle_data);
    }

    void RunShader(Pica::ShaderUnit& shader_unit, std::span<const Common::Vec4f> inputs) override {
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const Common::Vec4f& input = inputs[i];
            shader_unit.input[i].x = Pica::f24::FromFloat32(input.x);
            shader_unit.input[i].y = Pica::f24::FromFloat32(input.y);
            shader_unit.input[i].z = Pica::f24::FromFloat32(input.z);
            shader_unit.input[i].w = Pica::f24::FromFloat32(input.w);
        }
        shader_unit.temporary.fill(Common::Vec4<Pica::f24>::AssignToAll(Pica::f24::Zero()));
        shader_jit.Run(*shader_setup, shader_unit, 0);
    }

private:
    JitShader shader_jit;
};

#define SHADER_TEST_CASE(NAME, TAG)                                                                \
    TEMPLATE_TEST_CASE(NAME, TAG, ShaderInterpreterTest, ShaderJitTest)

SHADER_TEST_CASE("ADD", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::ADD, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({+1.0f, -1.0f}).x == +0.0f);
    REQUIRE(shader.Run({+0.0f, -0.0f}).x == -0.0f);
    REQUIRE(std::isnan(shader.Run({+INFINITY, -INFINITY}).x));
    REQUIRE(std::isinf(shader.Run({INFINITY, +1.0f}).x));
    REQUIRE(std::isinf(shader.Run({INFINITY, -1.0f}).x));
}

SHADER_TEST_CASE("CALL", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader_setup = CompileShaderSetup({
        {OpCode::Id::NOP}, // call foo
        {OpCode::Id::END},
        // .proc foo
        {OpCode::Id::NOP}, // call ex2
        {OpCode::Id::END},
        // .proc ex2
        {OpCode::Id::EX2, sh_output, sh_input},
        {OpCode::Id::END},
    });

    // nihstro does not support the CALL* instructions, so the instruction-binary must be manually
    // inserted here:
    nihstro::Instruction CALL = {};
    CALL.opcode = nihstro::OpCode(nihstro::OpCode::Id::CALL);

    // call foo
    CALL.flow_control.dest_offset = 2;
    CALL.flow_control.num_instructions = 1;
    shader_setup->UpdateProgramCode(0, CALL.hex);

    // call ex2
    CALL.flow_control.dest_offset = 4;
    CALL.flow_control.num_instructions = 1;
    shader_setup->UpdateProgramCode(2, CALL.hex);

    auto shader = TestType(std::move(shader_setup));

    REQUIRE(shader.Run(0.f).x == Catch::Approx(1.f));
}

SHADER_TEST_CASE("DP3", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::DP3, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({vec4_inf, vec4_zero}).x == 0.0f);
    REQUIRE(std::isnan(shader.Run({vec4_nan, vec4_zero}).x));

    REQUIRE(shader.Run({vec4_one, vec4_one}).x == 3.0f);

    const Common::Vec4f lhs = {2.0f, -3.0f, 4.0f, NAN};
    const Common::Vec4f rhs = {5.0f, 6.0f, -7.0f, NAN};
    REQUIRE(shader.Run({lhs, rhs}) == Common::Vec4f::AssignToAll(-36.0f));
}

SHADER_TEST_CASE("DP4", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::DP4, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({vec4_inf, vec4_zero}).x == 0.0f);
    REQUIRE(std::isnan(shader.Run({vec4_nan, vec4_zero}).x));

    REQUIRE(shader.Run({vec4_one, vec4_one}) == Common::Vec4f::AssignToAll(4.0f));

    const Common::Vec4f lhs = {2.0f, -3.0f, 4.0f, -5.0f};
    const Common::Vec4f rhs = {5.0f, 6.0f, -7.0f, 8.0f};
    REQUIRE(shader.Run({lhs, rhs}) == Common::Vec4f::AssignToAll(-76.0f));
}

SHADER_TEST_CASE("DPH", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::DPH, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({vec4_inf, vec4_zero}).x == 0.0f);
    REQUIRE(std::isnan(shader.Run({vec4_nan, vec4_zero}).x));

    REQUIRE(shader.Run({vec4_one, vec4_one}) == Common::Vec4f::AssignToAll(4.0f));
    REQUIRE(shader.Run({vec4_zero, vec4_one}) == Common::Vec4f::AssignToAll(1.0f));

    // DPH replaces the first source's W component with one before multiplying.
    const Common::Vec4f lhs = {2.0f, -3.0f, 4.0f, NAN};
    const Common::Vec4f rhs = {5.0f, 6.0f, -7.0f, 8.0f};
    REQUIRE(shader.Run({lhs, rhs}) == Common::Vec4f::AssignToAll(-28.0f));
}

SHADER_TEST_CASE("LG2", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::LG2, sh_output, sh_input},
        {OpCode::Id::END},
    });

    REQUIRE(std::isnan(shader.Run(NAN).x));
    REQUIRE(std::isnan(shader.Run(-1.f).x));
    REQUIRE(std::isinf(shader.Run(0.f).x));

    for (int exponent = -32; exponent <= 32; ++exponent) {
        CAPTURE(exponent);
        REQUIRE(shader.Run(std::ldexp(1.f, exponent)).x ==
                Catch::Approx(static_cast<float>(exponent)));
    }

    REQUIRE(shader.Run(1.5f).x == Catch::Approx(0.5849625007f));
    REQUIRE(shader.Run(1.e24f).x == Catch::Approx(79.7262742773f));
}

SHADER_TEST_CASE("EX2", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::EX2, sh_output, sh_input},
        {OpCode::Id::END},
    });

    REQUIRE(std::isnan(shader.Run(NAN).x));
    REQUIRE(shader.Run(-800.f).x == Catch::Approx(0.f));
    REQUIRE(shader.Run(-1.f).x == Catch::Approx(0.5f));
    REQUIRE(shader.Run(-0.5f).x == Catch::Approx(0.7071067812f));
    REQUIRE(shader.Run(0.f).x == Catch::Approx(1.f));
    REQUIRE(shader.Run(0.5f).x == Catch::Approx(1.4142135624f));
    REQUIRE(shader.Run(1.5f).x == Catch::Approx(2.8284271247f));
    REQUIRE(shader.Run(2.f).x == Catch::Approx(4.f));
    REQUIRE(shader.Run(6.f).x == Catch::Approx(64.f));
    REQUIRE(shader.Run(79.7262742773f).x == Catch::Approx(1.e24f));
    REQUIRE(std::isinf(shader.Run(800.f).x));
}

SHADER_TEST_CASE("MUL", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::MUL, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({+1.0f, -1.0f}).x == -1.0f);
    REQUIRE(shader.Run({-1.0f, +1.0f}).x == -1.0f);

    REQUIRE(shader.Run({INFINITY, 0.0f}).x == 0.0f);
    REQUIRE(std::isnan(shader.Run({NAN, 0.0f}).x));
    REQUIRE(shader.Run({+INFINITY, +INFINITY}).x == INFINITY);
    REQUIRE(shader.Run({+INFINITY, -INFINITY}).x == -INFINITY);
}

SHADER_TEST_CASE("SGE", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::SGE, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({INFINITY, 0.0f}).x == 1.0f);
    REQUIRE(shader.Run({0.0f, INFINITY}).x == 0.0f);
    REQUIRE(shader.Run({NAN, 0.0f}).x == 0.0f);
    REQUIRE(shader.Run({0.0f, NAN}).x == 0.0f);
    REQUIRE(shader.Run({+INFINITY, +INFINITY}).x == 1.0f);
    REQUIRE(shader.Run({+INFINITY, -INFINITY}).x == 1.0f);
    REQUIRE(shader.Run({-INFINITY, +INFINITY}).x == 0.0f);
    REQUIRE(shader.Run({+1.0f, -1.0f}).x == 1.0f);
    REQUIRE(shader.Run({-1.0f, +1.0f}).x == 0.0f);
}

SHADER_TEST_CASE("SLT", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::SLT, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({INFINITY, 0.0f}).x == 0.0f);
    REQUIRE(shader.Run({0.0f, INFINITY}).x == 1.0f);
    REQUIRE(shader.Run({NAN, 0.0f}).x == 0.0f);
    REQUIRE(shader.Run({0.0f, NAN}).x == 0.0f);
    REQUIRE(shader.Run({+INFINITY, +INFINITY}).x == 0.0f);
    REQUIRE(shader.Run({+INFINITY, -INFINITY}).x == 0.0f);
    REQUIRE(shader.Run({-INFINITY, +INFINITY}).x == 1.0f);
    REQUIRE(shader.Run({+1.0f, -1.0f}).x == 0.0f);
    REQUIRE(shader.Run({-1.0f, +1.0f}).x == 1.0f);
}

SHADER_TEST_CASE("FLR", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::FLR, sh_output, sh_input1},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({0.5}).x == 0.0f);
    REQUIRE(shader.Run({-0.5}).x == -1.0f);
    REQUIRE(shader.Run({1.5}).x == 1.0f);
    REQUIRE(shader.Run({-1.5}).x == -2.0f);
    REQUIRE(std::isnan(shader.Run({NAN}).x));
    REQUIRE(std::isinf(shader.Run({INFINITY}).x));
}

SHADER_TEST_CASE("MAX", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::MAX, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({1.0f, 0.0f}).x == 1.0f);
    REQUIRE(shader.Run({0.0f, 1.0f}).x == 1.0f);
    REQUIRE(shader.Run({0.0f, +INFINITY}).x == +INFINITY);
    // REQUIRE(shader.Run({0.0f, -INFINITY}).x == -INFINITY); // TODO: 3dbrew says this is -INFINITY
    REQUIRE(std::isnan(shader.Run({0.0f, NAN}).x));
    REQUIRE(shader.Run({NAN, 0.0f}).x == 0.0f);
    REQUIRE(shader.Run({-INFINITY, +INFINITY}).x == +INFINITY);
}

SHADER_TEST_CASE("MIN", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::MIN, sh_output, sh_input1, sh_input2},
        {OpCode::Id::END},
    });

    REQUIRE(shader.Run({1.0f, 0.0f}).x == 0.0f);
    REQUIRE(shader.Run({0.0f, 1.0f}).x == 0.0f);
    REQUIRE(shader.Run({0.0f, +INFINITY}).x == 0.0f);
    REQUIRE(shader.Run({0.0f, -INFINITY}).x == -INFINITY);
    REQUIRE(std::isnan(shader.Run({0.0f, NAN}).x));
    REQUIRE(shader.Run({NAN, 0.0f}).x == 0.0f);
    REQUIRE(shader.Run({-INFINITY, +INFINITY}).x == -INFINITY);
}

SHADER_TEST_CASE("RCP", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::RCP, sh_output, sh_input},
        {OpCode::Id::END},
    });

    // REQUIRE(shader.Run({-0.0f}).x == INFINITY); // Violates IEEE
    REQUIRE(shader.Run({0.0f}).x == INFINITY);
    REQUIRE(shader.Run({INFINITY}).x == 0.0f);
    REQUIRE(std::isnan(shader.Run({NAN}).x));

    REQUIRE(shader.Run({16.0f}).x == Catch::Approx(0.0625f).margin(0.001f));
    REQUIRE(shader.Run({8.0f}).x == Catch::Approx(0.125f).margin(0.001f));
    REQUIRE(shader.Run({4.0f}).x == Catch::Approx(0.25f).margin(0.001f));
    REQUIRE(shader.Run({2.0f}).x == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(shader.Run({1.0f}).x == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(shader.Run({0.5f}).x == Catch::Approx(2.0f).margin(0.001f));
    REQUIRE(shader.Run({0.25f}).x == Catch::Approx(4.0f).margin(0.001f));
    REQUIRE(shader.Run({0.125f}).x == Catch::Approx(8.0f).margin(0.002f));
    REQUIRE(shader.Run({0.0625f}).x == Catch::Approx(16.0f).margin(0.004f));
}

SHADER_TEST_CASE("RSQ", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        {OpCode::Id::RSQ, sh_output, sh_input},
        {OpCode::Id::END},
    });

    // REQUIRE(shader.Run({-0.0f}).x == INFINITY); // Violates IEEE
    REQUIRE(std::isnan(shader.Run({-2.0f}).x));
    REQUIRE(shader.Run({0.0f}).x == INFINITY);
    REQUIRE(shader.Run({INFINITY}).x == 0.0f);
    REQUIRE(std::isnan(shader.Run({-INFINITY}).x));
    REQUIRE(std::isnan(shader.Run({NAN}).x));

    REQUIRE(shader.Run({16.0f}).x == Catch::Approx(0.25f).margin(0.001f));
    REQUIRE(shader.Run({8.0f}).x == Catch::Approx(1.0f / std::sqrt(8.0f)).margin(0.001f));
    REQUIRE(shader.Run({4.0f}).x == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(shader.Run({2.0f}).x == Catch::Approx(1.0f / std::sqrt(2.0f)).margin(0.001f));
    REQUIRE(shader.Run({1.0f}).x == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(shader.Run({0.5f}).x == Catch::Approx(1.0f / std::sqrt(0.5f)).margin(0.001f));
    REQUIRE(shader.Run({0.25f}).x == Catch::Approx(2.0f).margin(0.001f));
    REQUIRE(shader.Run({0.125f}).x == Catch::Approx(1.0 / std::sqrt(0.125)).margin(0.002f));
    REQUIRE(shader.Run({0.0625f}).x == Catch::Approx(4.0f).margin(0.004f));

#if CITRA_ARCH(arm64)
    // Cover the complete useful f24 exponent range densely enough to catch an omitted or malformed
    // Newton step. The refined AArch64 estimate measured at <= 1.62e-5 relative error over one
    // million normal inputs on the Thor; allow a small margin above that measured bound.
    for (int exponent = -62; exponent <= 62; ++exponent) {
        for (int mantissa = 0; mantissa < 64; ++mantissa) {
            const float input = std::ldexp(1.0f + static_cast<float>(mantissa) / 64.0f, exponent);
            const float expected = 1.0f / std::sqrt(input);
            CAPTURE(input, expected);
            REQUIRE(shader.Run({input}).x == Catch::Approx(expected).epsilon(0.00002f));
        }
    }
#endif
}

SHADER_TEST_CASE("SETEMIT", "[video_core][shader]") {
    Pica::GeometryEmitter geometry_emitter;

    for (u8 winding = 0; winding <= 1; ++winding) {
        for (u8 prim_emit = 0; prim_emit <= 1; ++prim_emit) {
            for (u8 vertex_id = 0; vertex_id <= 3; ++vertex_id) {
                auto shader_setup = CompileShaderSetup({
                    {OpCode::Id::NOP}, // setemit
                    {OpCode::Id::END},
                });

                // nihstro does not support the SETEMIT instructions, so the instruction-binary must
                // be manually
                // inserted here:
                nihstro::Instruction SETEMIT = {};
                SETEMIT.opcode = nihstro::OpCode(nihstro::OpCode::Id::SETEMIT);
                SETEMIT.setemit.winding.Assign(winding);
                SETEMIT.setemit.prim_emit.Assign(prim_emit);
                SETEMIT.setemit.vertex_id.Assign(vertex_id);
                shader_setup->UpdateProgramCode(0, SETEMIT.hex);

                auto shader = TestType(std::move(shader_setup));
                Pica::ShaderUnit shader_unit(&geometry_emitter);
                shader.Run(shader_unit, 1.0f);

                REQUIRE(geometry_emitter.emit_state.winding == winding);
                REQUIRE(geometry_emitter.emit_state.prim_emit == prim_emit);
                REQUIRE(geometry_emitter.emit_state.vertex_id == vertex_id);
            }
        }
    }
}

SHADER_TEST_CASE("EMIT switches the output bank", "[video_core][shader]") {
    const auto sh_input0 = SourceRegister::MakeInput(0);
    const auto sh_input1 = SourceRegister::MakeInput(1);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader_setup = CompileShaderSetup({
        {OpCode::Id::MOV, sh_output, sh_input0},
        {OpCode::Id::NOP}, // emit
        {OpCode::Id::MOV, sh_output, sh_input1},
        {OpCode::Id::END},
    });

    nihstro::Instruction EMIT{};
    EMIT.opcode = nihstro::OpCode(nihstro::OpCode::Id::EMIT);
    shader_setup->UpdateProgramCode(1, EMIT.hex);

    auto shader = TestType(std::move(shader_setup));
    Pica::GeometryEmitter geometry_emitter{};
    geometry_emitter.output_mask = 1;
    geometry_emitter.emit_state.vertex_id = 0;
    geometry_emitter.emit_state.prim_emit = false;
    Pica::ShaderUnit shader_unit(&geometry_emitter);

    const std::array<Common::Vec4f, 2> inputs = {
        Common::Vec4f{1.0f, 2.0f, 3.0f, 4.0f},
        Common::Vec4f{5.0f, 6.0f, 7.0f, 8.0f},
    };
    shader.RunShader(shader_unit, inputs);

    REQUIRE(shader_unit.output_bank);
    for (std::size_t component = 0; component < 4; ++component) {
        REQUIRE(shader_unit.output[0][0][component].ToFloat32() == inputs[0][component]);
        REQUIRE(geometry_emitter.buffer[0][0][component].ToFloat32() == inputs[0][component]);
        REQUIRE(shader_unit.output[1][0][component].ToFloat32() == inputs[1][component]);
    }
}

SHADER_TEST_CASE("Uniform Read", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_c0 = SourceRegister::MakeFloat(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        // mova a0.x, sh_input.x
        {OpCode::Id::MOVA, DestRegister{}, "x", sh_input, "x", SourceRegister{}, "",
         nihstro::InlineAsm::RelativeAddress::A1},
        // mov sh_output.xyzw, c0[a0.x].xyzw
        {OpCode::Id::MOV, sh_output, "xyzw", sh_c0, "xyzw", SourceRegister{}, "",
         nihstro::InlineAsm::RelativeAddress::A1},
        {OpCode::Id::END},
    });

    // Prepare shader uniforms
    std::array<Common::Vec4f, 96> f_uniforms = {};
    for (u32 i = 0; i < 96; ++i) {
        const float color = (i * 2.0f) / 255.0f;
        const auto color_f24 = Pica::f24::FromFloat32(color);
        shader.shader_setup->uniforms.f[i] = {color_f24, color_f24, color_f24, Pica::f24::One()};
        f_uniforms[i] = {color, color, color, 1.0f};
    }

    for (u32 i = 0; i < 96; ++i) {
        const float index = static_cast<float>(i);
        // Add some fractional values to test proper float->integer truncation
        const float fractional = (i % 17) / 17.0f;

        REQUIRE(shader.Run(index + fractional) == f_uniforms[i]);
    }
}

SHADER_TEST_CASE("Address Register Offset", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_c40 = SourceRegister::MakeFloat(40);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader = TestType({
        // mova a0.x, sh_input.x
        {OpCode::Id::MOVA, DestRegister{}, "x", sh_input, "x", SourceRegister{}, "",
         nihstro::InlineAsm::RelativeAddress::A1},
        // mov sh_output.xyzw, c40[a0.x].xyzw
        {OpCode::Id::MOV, sh_output, "xyzw", sh_c40, "xyzw", SourceRegister{}, "",
         nihstro::InlineAsm::RelativeAddress::A1},
        {OpCode::Id::END},
    });

    // Prepare shader uniforms
    const bool inverted = true;
    std::array<Common::Vec4f, 96> f_uniforms;
    for (u32 i = 0; i < 0x80; i++) {
        if (i >= 0x00 && i < 0x60) {
            const u32 base = inverted ? (0x60 - i) : i;
            const auto color = (base * 2.f) / 255.0f;
            const auto color_f24 = Pica::f24::FromFloat32(color);
            shader.shader_setup->uniforms.f[i] = {color_f24, color_f24, color_f24,
                                                  Pica::f24::One()};
            f_uniforms[i] = {color, color, color, 1.f};
        } else if (i >= 0x60 && i < 0x64) {
            const u8 color = static_cast<u8>((i - 0x60) * 0x10);
            shader.shader_setup->uniforms.i[i - 0x60] = {color, color, color, 255};
        } else if (i >= 0x70 && i < 0x80) {
            shader.shader_setup->uniforms.b[i - 0x70] = i >= 0x78;
        }
    }

    REQUIRE(shader.Run(0.f) == f_uniforms[40]);
    REQUIRE(shader.Run(13.f) == f_uniforms[53]);
    REQUIRE(shader.Run(50.f) == f_uniforms[90]);
    REQUIRE(shader.Run(60.f) == vec4_one);
    REQUIRE(shader.Run(74.f) == vec4_one);
    REQUIRE(shader.Run(87.f) == vec4_one);
    REQUIRE(shader.Run(88.f) == f_uniforms[0]);
    REQUIRE(shader.Run(128.f) == f_uniforms[40]);
    REQUIRE(shader.Run(-40.f) == f_uniforms[0]);
    REQUIRE(shader.Run(-42.f) == vec4_one);
    REQUIRE(shader.Run(-70.f) == vec4_one);
    REQUIRE(shader.Run(-73.f) == f_uniforms[95]);
    REQUIRE(shader.Run(-127.f) == f_uniforms[41]);
    REQUIRE(shader.Run(-129.f) == f_uniforms[40]);
}

SHADER_TEST_CASE("PICA State Access", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_c0 = SourceRegister::MakeFloat(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    SECTION("Unused state is preserved") {
        auto shader = TestType({
            {OpCode::Id::MOV, sh_output, sh_input},
            {OpCode::Id::END},
        });

        Pica::ShaderUnit shader_unit;
        shader_unit.address_registers[0] = 17;
        shader_unit.address_registers[1] = -29;
        shader_unit.address_registers[2] = 41;
        shader_unit.conditional_code[0] = true;
        shader_unit.conditional_code[1] = false;
        shader.Run(shader_unit, 1.0f);

        REQUIRE(shader_unit.address_registers[0] == 17);
        REQUIRE(shader_unit.address_registers[1] == -29);
        REQUIRE(shader_unit.address_registers[2] == 41);
        REQUIRE(shader_unit.conditional_code[0]);
        REQUIRE_FALSE(shader_unit.conditional_code[1]);
    }

    SECTION("Partial MOVA preserves untouched registers") {
        auto shader = TestType({
            {OpCode::Id::MOVA, DestRegister{}, "x", sh_input, "x"},
            {OpCode::Id::END},
        });

        Pica::ShaderUnit shader_unit;
        shader_unit.address_registers[0] = -1;
        shader_unit.address_registers[1] = 23;
        shader_unit.address_registers[2] = -37;
        shader.Run(shader_unit, 9.75f);

        REQUIRE(shader_unit.address_registers[0] == 9);
        REQUIRE(shader_unit.address_registers[1] == 23);
        REQUIRE(shader_unit.address_registers[2] == -37);
    }

    SECTION("Y-only MOVA sign-extends its selected lane") {
        auto shader = TestType({
            {OpCode::Id::MOVA, DestRegister{}, "y", sh_input, "xy"},
            {OpCode::Id::END},
        });

        const Common::Vec4f input{INFINITY, -9.75f, NAN, -INFINITY};
        Pica::ShaderUnit shader_unit;
        shader_unit.address_registers[0] = 17;
        shader_unit.address_registers[1] = 23;
        shader_unit.address_registers[2] = -37;
        shader.RunShader(shader_unit, {&input, 1});

        REQUIRE(shader_unit.address_registers[0] == 17);
        REQUIRE(shader_unit.address_registers[1] == -9);
        REQUIRE(shader_unit.address_registers[2] == -37);
    }

    SECTION("MOVA truncates both consumed lanes and ignores Z/W") {
        auto shader = TestType({
            {OpCode::Id::MOVA, DestRegister{}, "xy", sh_input, "xy"},
            {OpCode::Id::END},
        });

        struct TestCase {
            Common::Vec4f input;
            s32 expected_x;
            s32 expected_y;
        };
        const std::array cases = {
            TestCase{{9.75f, -9.75f, INFINITY, NAN}, 9, -9},
            TestCase{{0.99f, -0.99f, NAN, -INFINITY}, 0, 0},
            TestCase{{127.75f, -127.75f, -INFINITY, INFINITY}, 127, -127},
        };

        for (const auto& test : cases) {
            Pica::ShaderUnit shader_unit;
            shader_unit.address_registers[0] = -1;
            shader_unit.address_registers[1] = 1;
            shader_unit.address_registers[2] = -37;
            shader.RunShader(shader_unit, {&test.input, 1});

            REQUIRE(shader_unit.address_registers[0] == test.expected_x);
            REQUIRE(shader_unit.address_registers[1] == test.expected_y);
            REQUIRE(shader_unit.address_registers[2] == -37);
        }
    }

    SECTION("Relative uniform reads initial address state") {
        auto shader = TestType({
            {OpCode::Id::MOV, sh_output, "xyzw", sh_c0, "xyzw", SourceRegister{}, "",
             nihstro::InlineAsm::RelativeAddress::A1},
            {OpCode::Id::END},
        });
        shader.shader_setup->uniforms.f[5] = {
            Pica::f24::FromFloat32(0.25f), Pica::f24::FromFloat32(0.5f),
            Pica::f24::FromFloat32(0.75f), Pica::f24::One()};

        Pica::ShaderUnit shader_unit;
        shader_unit.address_registers[0] = 5;
        shader.Run(shader_unit, 0.0f);

        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() == 0.25f);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].y.ToFloat32() == 0.5f);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].z.ToFloat32() == 0.75f);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].w.ToFloat32() == 1.0f);
        REQUIRE(shader_unit.address_registers[0] == 5);
    }

    SECTION("CMP writes conditional state") {
        const auto sh_input2 = SourceRegister::MakeInput(1);
        auto shader_setup = CompileShaderSetup({
            {OpCode::Id::NOP},
            {OpCode::Id::END},
        });

        nihstro::Instruction cmp{};
        cmp.opcode = OpCode::Id::CMP;
        cmp.common.operand_desc_id = 0;
        cmp.common.src1 = sh_input;
        cmp.common.src2 = sh_input2;
        cmp.common.compare_op.x = nihstro::Instruction::Common::CompareOpType::LessThan;
        cmp.common.compare_op.y = nihstro::Instruction::Common::CompareOpType::GreaterEqual;
        shader_setup->UpdateProgramCode(0, cmp.hex);

        nihstro::SwizzlePattern swizzle{};
        swizzle.dest_mask = 0b1111;
        for (int component = 0; component < 4; ++component) {
            const auto selector = static_cast<nihstro::SwizzlePattern::Selector>(component);
            swizzle.SetSelectorSrc1(component, selector);
            swizzle.SetSelectorSrc2(component, selector);
        }
        shader_setup->UpdateSwizzleData(0, swizzle.hex);

        auto shader = TestType(std::move(shader_setup));
        Pica::ShaderUnit shader_unit;
        shader_unit.conditional_code[0] = false;
        shader_unit.conditional_code[1] = false;
        const std::array<Common::Vec4f, 2> inputs = {
            Common::Vec4f{1.0f, 4.0f, 0.0f, 0.0f},
            Common::Vec4f{2.0f, 3.0f, 0.0f, 0.0f},
        };
        shader.RunShader(shader_unit, inputs);

        REQUIRE(shader_unit.conditional_code[0]);
        REQUIRE(shader_unit.conditional_code[1]);
    }

    SECTION("CMP same-operation lanes preserve ordered and NaN semantics") {
        using CompareOp = nihstro::Instruction::Common::CompareOpType;
        const auto sh_input2 = SourceRegister::MakeInput(1);
        constexpr std::array<CompareOp::Op, 6> compare_ops = {
            CompareOp::Equal,     CompareOp::NotEqual,    CompareOp::LessThan,
            CompareOp::LessEqual, CompareOp::GreaterThan, CompareOp::GreaterEqual,
        };
        constexpr std::array<bool, 6> less_results = {false, true, true, true, false, false};
        constexpr std::array<bool, 6> unordered_results = {false, true, false, false, false, false};
        constexpr std::array<bool, 6> greater_results = {false, true, false, false, true, true};
        constexpr std::array<bool, 6> equal_results = {true, false, false, true, false, true};

        for (std::size_t i = 0; i < compare_ops.size(); ++i) {
            auto shader_setup = CompileShaderSetup({
                {OpCode::Id::NOP},
                {OpCode::Id::END},
            });

            nihstro::Instruction cmp{};
            cmp.opcode = OpCode::Id::CMP;
            cmp.common.operand_desc_id = 0;
            cmp.common.src1 = sh_input;
            cmp.common.src2 = sh_input2;
            cmp.common.compare_op.x = compare_ops[i];
            cmp.common.compare_op.y = compare_ops[i];
            shader_setup->UpdateProgramCode(0, cmp.hex);

            nihstro::SwizzlePattern swizzle{};
            swizzle.dest_mask = 0b1111;
            for (int component = 0; component < 4; ++component) {
                const auto selector = static_cast<nihstro::SwizzlePattern::Selector>(component);
                swizzle.SetSelectorSrc1(component, selector);
                swizzle.SetSelectorSrc2(component, selector);
            }
            shader_setup->UpdateSwizzleData(0, swizzle.hex);

            auto shader = TestType(std::move(shader_setup));
            const auto run_compare = [&](const Common::Vec4f& lhs, const Common::Vec4f& rhs,
                                         bool expected_x, bool expected_y) {
                Pica::ShaderUnit shader_unit;
                const std::array<Common::Vec4f, 2> inputs = {lhs, rhs};
                shader.RunShader(shader_unit, inputs);
                CAPTURE(i, lhs, rhs);
                REQUIRE(shader_unit.conditional_code[0] == expected_x);
                REQUIRE(shader_unit.conditional_code[1] == expected_y);
            };

            run_compare(Common::Vec4f{1.0f, NAN, 0.0f, 0.0f}, Common::Vec4f{2.0f, 0.0f, 0.0f, 0.0f},
                        less_results[i], unordered_results[i]);
            run_compare(Common::Vec4f{3.0f, 2.0f, 0.0f, 0.0f},
                        Common::Vec4f{2.0f, 2.0f, 0.0f, 0.0f}, greater_results[i],
                        equal_results[i]);
        }
    }
}

SHADER_TEST_CASE("Dest Mask", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    const auto shader = [&sh_input, &sh_output](const char* dest_mask) {
        return std::unique_ptr<TestType>(new TestType{
            {OpCode::Id::MOV, sh_output, dest_mask, sh_input, "xyzw", SourceRegister{}, ""},
            {OpCode::Id::END},
        });
    };

    const Common::Vec4f iota_vec = {1.0f, 2.0f, 3.0f, 4.0f};

    REQUIRE(shader("x")->Run({iota_vec}).x == iota_vec.x);
    REQUIRE(shader("y")->Run({iota_vec}).y == iota_vec.y);
    REQUIRE(shader("z")->Run({iota_vec}).z == iota_vec.z);
    REQUIRE(shader("w")->Run({iota_vec}).w == iota_vec.w);
    REQUIRE(shader("xy")->Run({iota_vec}).xy() == iota_vec.xy());
    REQUIRE(shader("xz")->Run({iota_vec}).xz() == iota_vec.xz());
    REQUIRE(shader("xw")->Run({iota_vec}).xw() == iota_vec.xw());
    REQUIRE(shader("yz")->Run({iota_vec}).yz() == iota_vec.yz());
    REQUIRE(shader("yw")->Run({iota_vec}).yw() == iota_vec.yw());
    REQUIRE(shader("zw")->Run({iota_vec}).zw() == iota_vec.zw());
    REQUIRE(shader("xyz")->Run({iota_vec}).xyz() == iota_vec.xyz());
    REQUIRE(shader("xyw")->Run({iota_vec}).xyw() == iota_vec.xyw());
    REQUIRE(shader("xzw")->Run({iota_vec}).xzw() == iota_vec.xzw());
    REQUIRE(shader("yzw")->Run({iota_vec}).yzw() == iota_vec.yzw());
    REQUIRE(shader("xyzw")->Run({iota_vec}) == iota_vec);

    SECTION("Every partial mask preserves disabled components") {
        constexpr std::array masks = {"x",  "y",  "z",  "w",   "xy",  "xz",  "xw",
                                      "yz", "yw", "zw", "xyz", "xyw", "xzw", "yzw"};
        constexpr Common::Vec4f sentinel = {-11.0f, -12.0f, -13.0f, -14.0f};

        for (const bool output_bank : {false, true}) {
            for (const char* mask : masks) {
                CAPTURE(output_bank, mask);
                auto masked_shader = shader(mask);
                Pica::ShaderUnit shader_unit;
                shader_unit.output_bank = output_bank;
                auto& output = shader_unit.output[output_bank][0];
                for (std::size_t component = 0; component < 4; ++component) {
                    output[component] = Pica::f24::FromFloat32(sentinel[component]);
                }

                masked_shader->RunShader(shader_unit, {&iota_vec, 1});

                for (std::size_t component = 0; component < 4; ++component) {
                    bool enabled = false;
                    for (const char* current = mask; *current != '\0'; ++current) {
                        enabled |= *current == "xyzw"[component];
                    }
                    const float expected = enabled ? iota_vec[component] : sentinel[component];
                    REQUIRE(output[component].ToFloat32() == expected);
                }
            }
        }
    }

    SECTION("An empty hardware mask leaves the destination untouched") {
        auto shader_setup = CompileShaderSetup({
            {OpCode::Id::MOV, sh_output, "x", sh_input, "xyzw", SourceRegister{}, ""},
            {OpCode::Id::END},
        });
        nihstro::SwizzlePattern swizzle = {shader_setup->GetSwizzleData()[0]};
        swizzle.dest_mask = 0;
        shader_setup->UpdateSwizzleData(0, swizzle.hex);
        auto empty_mask_shader = TestType(std::move(shader_setup));

        constexpr Common::Vec4f sentinel = {-21.0f, -22.0f, -23.0f, -24.0f};
        Pica::ShaderUnit shader_unit;
        auto& output = shader_unit.output[shader_unit.output_bank][0];
        for (std::size_t component = 0; component < 4; ++component) {
            output[component] = Pica::f24::FromFloat32(sentinel[component]);
        }

        empty_mask_shader.RunShader(shader_unit, {&iota_vec, 1});

        for (std::size_t component = 0; component < 4; ++component) {
            REQUIRE(output[component].ToFloat32() == sentinel[component]);
        }
    }
}

SHADER_TEST_CASE("MAD", "[video_core][shader]") {
    const auto sh_input1 = SourceRegister::MakeInput(0);
    const auto sh_input2 = SourceRegister::MakeInput(1);
    const auto sh_input3 = SourceRegister::MakeInput(2);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader_setup = CompileShaderSetup({
        // TODO: Requires fix from https://github.com/neobrain/nihstro/issues/68
        // {OpCode::Id::MAD, sh_output, sh_input1, sh_input2, sh_input3},
        {OpCode::Id::NOP},
        {OpCode::Id::END},
    });

    // nihstro does not support the MAD* instructions, so the instruction-binary must be manually
    // inserted here:
    nihstro::Instruction MAD = {};
    MAD.opcode = nihstro::OpCode::Id::MAD;
    MAD.mad.operand_desc_id = 0;
    MAD.mad.src1 = sh_input1;
    MAD.mad.src2 = sh_input2;
    MAD.mad.src3 = sh_input3;
    MAD.mad.dest = sh_output;
    shader_setup->UpdateProgramCode(0, MAD.hex);

    nihstro::SwizzlePattern swizzle = {};
    swizzle.dest_mask = 0b1111;
    swizzle.SetSelectorSrc1(0, SwizzlePattern::Selector::x);
    swizzle.SetSelectorSrc1(1, SwizzlePattern::Selector::y);
    swizzle.SetSelectorSrc1(2, SwizzlePattern::Selector::z);
    swizzle.SetSelectorSrc1(3, SwizzlePattern::Selector::w);
    swizzle.SetSelectorSrc2(0, SwizzlePattern::Selector::x);
    swizzle.SetSelectorSrc2(1, SwizzlePattern::Selector::y);
    swizzle.SetSelectorSrc2(2, SwizzlePattern::Selector::z);
    swizzle.SetSelectorSrc2(3, SwizzlePattern::Selector::w);
    swizzle.SetSelectorSrc3(0, SwizzlePattern::Selector::x);
    swizzle.SetSelectorSrc3(1, SwizzlePattern::Selector::y);
    swizzle.SetSelectorSrc3(2, SwizzlePattern::Selector::z);
    swizzle.SetSelectorSrc3(3, SwizzlePattern::Selector::w);
    shader_setup->UpdateSwizzleData(0, swizzle.hex);

    auto shader = TestType(std::move(shader_setup));

    REQUIRE(shader.Run({vec4_zero, vec4_zero, vec4_zero}) == vec4_zero);
    REQUIRE(shader.Run({vec4_one, vec4_one, vec4_one}) == (vec4_one * 2.0f));
    REQUIRE(shader.Run({vec4_inf, vec4_zero, vec4_zero}) == vec4_zero);
    REQUIRE(shader.Run({vec4_nan, vec4_zero, vec4_zero}) == vec4_nan);
}

// Nested Loops are bugged on on the Shader-Interpreter at the moment
// SHADER_TEST_CASE("Nested Loop", "[video_core][shader]") {
TEMPLATE_TEST_CASE("Nested Loop", "[video_core][shader]", ShaderJitTest) {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_temp = SourceRegister::MakeTemporary(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    auto shader_test = TestType({
        // clang-format off
        {OpCode::Id::MOV, sh_temp, sh_input},
        {OpCode::Id::LOOP, 0},
            {OpCode::Id::LOOP, 1},
                {OpCode::Id::ADD, sh_temp, sh_temp, sh_input},
            {Type::EndLoop},
        {Type::EndLoop},
        {OpCode::Id::MOV, sh_output, sh_temp},
        {OpCode::Id::END},
        // clang-format on
    });

    {
        shader_test.shader_setup->uniforms.i[0] = {(u8)GENERATE(4, 9), 0, (u8)GENERATE(1, 2), 0};
        shader_test.shader_setup->uniforms.i[1] = {(u8)GENERATE(4, 7), 0, (u8)GENERATE(1, 1), 0};
        Common::Vec4<u8> loop_parms{shader_test.shader_setup->uniforms.i[0]};

        const int expected_aL = loop_parms[1] + ((loop_parms[0] + 1) * loop_parms[2]);
        const float input = 1.0f;
        const float expected_out = (((shader_test.shader_setup->uniforms.i[0][0] + 1) *
                                     (shader_test.shader_setup->uniforms.i[1][0] + 1)) *
                                    input) +
                                   input;

        Pica::ShaderUnit shader_unit;
        shader_test.Run(shader_unit, input);

        REQUIRE(shader_unit.address_registers[2] == expected_aL);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() ==
                Catch::Approx(expected_out));
    }
}

SHADER_TEST_CASE("Conditional", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_temp = SourceRegister::MakeTemporary(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    const std::initializer_list<nihstro::InlineAsm> assembly_template = {
        // IFC configured later
        {OpCode::Id::NOP},
        // True
        {OpCode::Id::MOV, sh_output, sh_input},
        {OpCode::Id::END},
        // False
        {OpCode::Id::MOV, sh_output, sh_temp},
        {OpCode::Id::END},
    };

    const bool ref_x = GENERATE(0, 1);
    const bool cmp_x = GENERATE(0, 1);
    const bool result_x = (cmp_x == ref_x);

    const bool ref_y = GENERATE(0, 1);
    const bool cmp_y = GENERATE(0, 1);
    const bool result_y = (cmp_y == ref_y);

    nihstro::Instruction IFC = {};
    IFC.opcode = nihstro::OpCode::Id::IFC;
    IFC.flow_control.num_instructions = 2;
    IFC.flow_control.dest_offset = 3;
    IFC.flow_control.refx = ref_x;
    IFC.flow_control.refy = ref_y;

    Pica::ShaderUnit shader_unit;
    shader_unit.conditional_code[0] = cmp_x;
    shader_unit.conditional_code[1] = cmp_y;

    // JustX
    {
        auto shader_setup = CompileShaderSetup(assembly_template);
        IFC.flow_control.op = nihstro::Instruction::FlowControlType::Op::JustX;
        shader_setup->UpdateProgramCode(0, IFC.hex);
        const float result = result_x ? 1.0f : 0.0f;

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);

        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() == result);
    }

    // JustY
    {
        auto shader_setup = CompileShaderSetup(assembly_template);
        IFC.flow_control.op = nihstro::Instruction::FlowControlType::Op::JustY;
        shader_setup->UpdateProgramCode(0, IFC.hex);
        const float result = result_y ? 1.0f : 0.0f;

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);

        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() == result);
    }

    // OR
    {
        auto shader_setup = CompileShaderSetup(assembly_template);
        IFC.flow_control.op = nihstro::Instruction::FlowControlType::Op::Or;
        shader_setup->UpdateProgramCode(0, IFC.hex);
        const float result = (result_x || result_y) ? 1.0f : 0.0f;

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);

        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() == result);
    }

    // AND
    {
        auto shader_setup = CompileShaderSetup(assembly_template);
        IFC.flow_control.op = nihstro::Instruction::FlowControlType::Op::And;
        shader_setup->UpdateProgramCode(0, IFC.hex);
        const float result = (result_x && result_y) ? 1.0f : 0.0f;

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);

        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() == result);
    }
}

SHADER_TEST_CASE("Conditional control flow", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_temp = SourceRegister::MakeTemporary(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    // This condition lowers to CMP + GE on AArch64. Keep COND1 true so COND0 selects false/true;
    // that catches consumers which incorrectly assume every compiled condition uses EQ/NE.
    const bool condition = GENERATE(false, true);
    Pica::ShaderUnit shader_unit;
    shader_unit.conditional_code[0] = condition;
    shader_unit.conditional_code[1] = true;

    const auto make_condition = [](OpCode::Id opcode) {
        nihstro::Instruction instr{};
        instr.opcode = nihstro::OpCode(opcode);
        instr.flow_control.op = nihstro::Instruction::FlowControlType::Op::Or;
        instr.flow_control.refx = 1;
        instr.flow_control.refy = 0;
        return instr;
    };

    SECTION("CALLC") {
        auto shader_setup = CompileShaderSetup({
            {OpCode::Id::MOV, sh_output, sh_temp},
            {OpCode::Id::NOP}, // CALLC configured below
            {OpCode::Id::END},
            {OpCode::Id::NOP},
            {OpCode::Id::MOV, sh_output, sh_input},
            {OpCode::Id::END},
        });
        auto CALLC = make_condition(OpCode::Id::CALLC);
        CALLC.flow_control.dest_offset = 4;
        CALLC.flow_control.num_instructions = 1;
        shader_setup->UpdateProgramCode(1, CALLC.hex);

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() ==
                (condition ? 1.0f : 0.0f));
    }

    SECTION("JMPC") {
        auto shader_setup = CompileShaderSetup({
            {OpCode::Id::MOV, sh_output, sh_temp},
            {OpCode::Id::NOP}, // JMPC configured below
            {OpCode::Id::END},
            {OpCode::Id::NOP},
            {OpCode::Id::MOV, sh_output, sh_input},
            {OpCode::Id::END},
        });
        auto JMPC = make_condition(OpCode::Id::JMPC);
        JMPC.flow_control.dest_offset = 4;
        shader_setup->UpdateProgramCode(1, JMPC.hex);

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() ==
                (condition ? 1.0f : 0.0f));
    }

    SECTION("BREAKC") {
        auto shader_setup = CompileShaderSetup({
            {OpCode::Id::MOV, sh_output, sh_temp},
            {OpCode::Id::LOOP, 0},
            {OpCode::Id::NOP}, // BREAKC configured below
            {OpCode::Id::MOV, sh_output, sh_input},
            {Type::EndLoop},
            {OpCode::Id::END},
        });
        auto BREAKC = make_condition(OpCode::Id::BREAKC);
        shader_setup->UpdateProgramCode(2, BREAKC.hex);
        shader_setup->uniforms.i[0] = {0, 0, 0, 0};

        auto shader_test = TestType(std::move(shader_setup));
        shader_test.Run(shader_unit, 1.0f);
        REQUIRE(shader_unit.output[shader_unit.output_bank][0].x.ToFloat32() ==
                (condition ? 0.0f : 1.0f));
    }
}

SHADER_TEST_CASE("Source Swizzle", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);

    const auto shader = [&sh_input, &sh_output](const char* swizzle) {
        return std::unique_ptr<TestType>(new TestType{
            {OpCode::Id::MOV, sh_output, "xyzw", sh_input, swizzle, SourceRegister{}, ""},
            {OpCode::Id::END},
        });
    };

    const Common::Vec4f iota_vec = {1.0f, 2.0f, 3.0f, 4.0f};

    REQUIRE(shader("x")->Run({iota_vec}).x == iota_vec.x);
    REQUIRE(shader("y")->Run({iota_vec}).x == iota_vec.y);
    REQUIRE(shader("z")->Run({iota_vec}).x == iota_vec.z);
    REQUIRE(shader("w")->Run({iota_vec}).x == iota_vec.w);
    REQUIRE(shader("xy")->Run({iota_vec}).xy() == iota_vec.xy());
    REQUIRE(shader("xz")->Run({iota_vec}).xy() == iota_vec.xz());
    REQUIRE(shader("xw")->Run({iota_vec}).xy() == iota_vec.xw());
    REQUIRE(shader("yz")->Run({iota_vec}).xy() == iota_vec.yz());
    REQUIRE(shader("yw")->Run({iota_vec}).xy() == iota_vec.yw());
    REQUIRE(shader("zw")->Run({iota_vec}).xy() == iota_vec.zw());
    REQUIRE(shader("yy")->Run({iota_vec}).xy() == iota_vec.yy());
    REQUIRE(shader("wx")->Run({iota_vec}).xy() == iota_vec.wx());
    REQUIRE(shader("xyz")->Run({iota_vec}).xyz() == iota_vec.xyz());
    REQUIRE(shader("xyw")->Run({iota_vec}).xyz() == iota_vec.xyw());
    REQUIRE(shader("xzw")->Run({iota_vec}).xyz() == iota_vec.xzw());
    REQUIRE(shader("yzw")->Run({iota_vec}).xyz() == iota_vec.yzw());
    REQUIRE(shader("yyy")->Run({iota_vec}).xyz() == iota_vec.yyy());
    REQUIRE(shader("yxw")->Run({iota_vec}).xyz() == iota_vec.yxw());
    REQUIRE(shader("xyzw")->Run({iota_vec}) == iota_vec);
    REQUIRE(shader("wzxy")->Run({iota_vec}) ==
            Common::Vec4f(iota_vec.w, iota_vec.z, iota_vec.x, iota_vec.y));
    REQUIRE(shader("yyyy")->Run({iota_vec}) ==
            Common::Vec4f(iota_vec.y, iota_vec.y, iota_vec.y, iota_vec.y));
}

TEST_CASE("All Source Swizzles", "[video_core][shader]") {
    const auto sh_input = SourceRegister::MakeInput(0);
    const auto sh_output = DestRegister::MakeOutput(0);
    const Common::Vec4f input = {1.0f, 2.0f, 3.0f, 4.0f};
    const std::array<float, 4> input_components = {input.x, input.y, input.z, input.w};

    for (u32 raw_selector = 0; raw_selector < 256; raw_selector++) {
        auto shader_setup = CompileShaderSetup({
            {OpCode::Id::MOV, sh_output, "xyzw", sh_input, "xyzw", SourceRegister{}, ""},
            {OpCode::Id::END},
        });

        nihstro::SwizzlePattern swizzle{};
        swizzle.dest_mask = 0b1111;
        std::array<u32, 4> selectors{};
        for (u32 lane = 0; lane < selectors.size(); lane++) {
            selectors[lane] = (raw_selector >> (6 - lane * 2)) & 0b11;
            swizzle.SetSelectorSrc1(
                lane, static_cast<nihstro::SwizzlePattern::Selector>(selectors[lane]));
        }
        shader_setup->UpdateSwizzleData(0, swizzle.hex);

        ShaderJitTest shader{std::move(shader_setup)};
        const Common::Vec4f result = shader.Run({input});
        const std::array<float, 4> result_components = {result.x, result.y, result.z, result.w};

        for (u32 lane = 0; lane < selectors.size(); lane++) {
            CAPTURE(raw_selector, lane);
            REQUIRE(result_components[lane] == input_components[selectors[lane]]);
        }
    }
}

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
