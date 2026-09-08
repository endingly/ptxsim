#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {

using common::RawValue;

/** @brief One independently specified arithmetic result from real PTX input. */
struct ValueAluPipelineCase {
  /** PTX instruction spelling; register declarations are sized from the bits. */
  std::string_view instruction;
  /** First source's already encoded register value. */
  RawValue lhs;
  /** Second source's already encoded register value. */
  RawValue rhs;
  /** Expected result bits, not calculated by the implementation under test. */
  RawValue expected;
  /** Optional source spelling; empty means read the second register. */
  std::string_view rhs_literal{};
  /** Legacy spelling that must lower identically to the canonical instruction. */
  std::string_view legacy_instruction{};
};

/** @brief Give parameterized tests stable names without dumping object bytes. */
void PrintTo(const ValueAluPipelineCase& item, std::ostream* output) {
  *output << item.instruction;
  if (!item.rhs_literal.empty())
    *output << " immediate " << item.rhs_literal;
}

/** @brief Select a bit-register declaration for the tested scalar container. */
auto register_type(const RawValue& value) -> std::string_view {
  switch (value.width()) {
    case common::RawWidth::b16:
      return ".b16";
    case common::RawWidth::b32:
      return ".b32";
    case common::RawWidth::b64:
      return ".b64";
    default:
      return ".invalid";
  }
}

/** @brief Verify arithmetic forms through parse, resolve, lowering and commit. */
class ValueAluPipeline : public ::testing::TestWithParam<ValueAluPipelineCase> {
};

TEST_P(ValueAluPipeline, ExecutesResolvedRegisterOperands) {
  const auto& item = GetParam();
  /** @brief Build identical register declarations around either modifier spelling. */
  const auto module_text = [&](std::string_view instruction) {
    return std::string(
        ".entry kernel() {\n.reg " + std::string(register_type(item.expected)) +
        " %d;\n.reg " + std::string(register_type(item.lhs)) + " %a;\n.reg " +
        std::string(register_type(item.rhs)) + " %b;\n" +
        std::string(instruction) + " %d, %a, " +
        std::string(item.rhs_literal.empty() ? "%b" : item.rhs_literal) +
        ";\nexit;\n}");
  };
  const std::string text = module_text(item.instruction);
  SCOPED_TRACE(text);
  ptx_frontend::PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved);
  auto program = exec_ir_lowering::lower(*resolved);
  ASSERT_TRUE(program);
  if (!item.legacy_instruction.empty()) {
    const auto legacy_text = module_text(item.legacy_instruction);
    SCOPED_TRACE(legacy_text);
    ptx_frontend::PtxSyntaxParser legacy_parser(legacy_text);
    const auto legacy_ast = legacy_parser.parseModule();
    ASSERT_TRUE(legacy_ast);
    const auto legacy_resolved =
        ptx_frontend::resolved_ir::resolveModule(*legacy_ast);
    ASSERT_TRUE(legacy_resolved);
    const auto legacy_program = exec_ir_lowering::lower(*legacy_resolved);
    ASSERT_TRUE(legacy_program);
    const auto canonical_instruction =
        program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
    const auto legacy_instruction = legacy_program->fetch(
        {common::FunctionId{0}, common::ProgramCounter{0}});
    ASSERT_TRUE(canonical_instruction);
    ASSERT_TRUE(legacy_instruction);
    EXPECT_EQ(
        exec_ir::to_string(canonical_instruction->get()),
        std::string(item.instruction) + " register:0, register:1, register:2");
    EXPECT_EQ(canonical_instruction->get(), legacy_instruction->get());
  }
  runtime::LaunchRuntime runtime{
      execution_model::GridId{41},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  ASSERT_TRUE(simulator.run(0));
  auto& thread = runtime.grid()
                     .cta(execution_model::CtaId{runtime.grid().id(), 0})
                     .warp(0)
                     .thread(execution_model::LaneId{0});
  const auto frame = runtime.register_frame(thread.id(), common::FunctionId{0});
  ASSERT_TRUE(frame);
  auto registers = runtime.registers().view(*frame);
  ASSERT_TRUE(registers);
  ASSERT_TRUE(registers->write(common::RegisterSlot{1}, item.lhs));
  ASSERT_TRUE(registers->write(common::RegisterSlot{2}, item.rhs));
  const auto run = simulator.run(2);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  const auto result = registers->read(common::RegisterSlot{0});
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, item.expected);
}

INSTANTIATE_TEST_SUITE_P(
    PinnedAddForms, ValueAluPipeline,
    ::testing::Values(
        ValueAluPipelineCase{"add.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x40400000U)},
        ValueAluPipelineCase{"add.f64",
                             RawValue::b64(std::uint64_t{0x3ff0000000000000}),
                             RawValue::b64(std::uint64_t{0x4000000000000000}),
                             RawValue::b64(std::uint64_t{0x4008000000000000})},
        ValueAluPipelineCase{"add.f32x2",
                             RawValue::b64(std::uint64_t{0x3f8000003f800000}),
                             RawValue::b64(std::uint64_t{0x4000000040000000}),
                             RawValue::b64(std::uint64_t{0x4040000040400000})},
        ValueAluPipelineCase{"add.f16", RawValue::b16(std::uint16_t{0x3c00}),
                             RawValue::b16(std::uint16_t{0x4000}),
                             RawValue::b16(std::uint16_t{0x4200})},
        ValueAluPipelineCase{"add.f16x2", RawValue::b32(0x3c003c00U),
                             RawValue::b32(0x40004000U),
                             RawValue::b32(0x42004200U)},
        ValueAluPipelineCase{"add.bf16", RawValue::b16(std::uint16_t{0x3f80}),
                             RawValue::b16(std::uint16_t{0x4000}),
                             RawValue::b16(std::uint16_t{0x4040})},
        ValueAluPipelineCase{"add.bf16x2", RawValue::b32(0x3f803f80U),
                             RawValue::b32(0x40004000U),
                             RawValue::b32(0x40404040U)},
        ValueAluPipelineCase{
            "add.f32.f16", RawValue::b16(std::uint16_t{0x3c00}),
            RawValue::b32(0x40000000U), RawValue::b32(0x40400000U)},
        ValueAluPipelineCase{
            "add.f32.bf16", RawValue::b16(std::uint16_t{0x3f80}),
            RawValue::b32(0x40000000U), RawValue::b32(0x40400000U)},
        ValueAluPipelineCase{"add.sat.f32.f16",
                             RawValue::b16(std::uint16_t{0x3c00}),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x3f800000U),
                             {},
                             "add.f32.f16.sat"},
        ValueAluPipelineCase{"add.sat.f32.bf16",
                             RawValue::b16(std::uint16_t{0x3f80}),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x3f800000U),
                             {},
                             "add.f32.bf16.sat"},
        ValueAluPipelineCase{"add.u16", RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{2}),
                             RawValue::b16(std::uint16_t{3})},
        ValueAluPipelineCase{"add.s16", RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{2}),
                             RawValue::b16(std::uint16_t{3})},
        ValueAluPipelineCase{"add.u32", RawValue::b32(1U), RawValue::b32(2U),
                             RawValue::b32(3U)},
        ValueAluPipelineCase{"add.s32", RawValue::b32(1U), RawValue::b32(2U),
                             RawValue::b32(3U)},
        ValueAluPipelineCase{"add.u64", RawValue::b64(std::uint64_t{1}),
                             RawValue::b64(std::uint64_t{2}),
                             RawValue::b64(std::uint64_t{3})},
        ValueAluPipelineCase{"add.s64", RawValue::b64(std::uint64_t{1}),
                             RawValue::b64(std::uint64_t{2}),
                             RawValue::b64(std::uint64_t{3})},
        ValueAluPipelineCase{"add.u16x2", RawValue::b32(0x00010001U),
                             RawValue::b32(0x00020002U),
                             RawValue::b32(0x00030003U)},
        ValueAluPipelineCase{"add.s16x2", RawValue::b32(0x00010001U),
                             RawValue::b32(0x00020002U),
                             RawValue::b32(0x00030003U)},
        ValueAluPipelineCase{"add.sat.u32", RawValue::b32(1U),
                             RawValue::b32(2U), RawValue::b32(3U)},
        ValueAluPipelineCase{"add.sat.s32", RawValue::b32(1U),
                             RawValue::b32(2U), RawValue::b32(3U)},
        ValueAluPipelineCase{"add.sat.u16x2", RawValue::b32(0x00010001U),
                             RawValue::b32(0x00020002U),
                             RawValue::b32(0x00030003U)},
        ValueAluPipelineCase{"add.sat.s16x2", RawValue::b32(0x00010001U),
                             RawValue::b32(0x00020002U),
                             RawValue::b32(0x00030003U)},
        ValueAluPipelineCase{"add.u8x4", RawValue::b32(0x01010101U),
                             RawValue::b32(0x02020202U),
                             RawValue::b32(0x03030303U)},
        ValueAluPipelineCase{"add.s8x4", RawValue::b32(0x01010101U),
                             RawValue::b32(0x02020202U),
                             RawValue::b32(0x03030303U)},
        ValueAluPipelineCase{"add.s16", RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{0}),
                             RawValue::b16(std::uint16_t{0}), "-1"},
        ValueAluPipelineCase{"add.s64", RawValue::b64(std::uint64_t{1}),
                             RawValue::b64(std::uint64_t{0}),
                             RawValue::b64(std::uint64_t{0}), "-1"},
        ValueAluPipelineCase{"add.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0U), RawValue::b32(0x40400000U),
                             "0f40000000"},
        ValueAluPipelineCase{"add.f64",
                             RawValue::b64(std::uint64_t{0x3ff0000000000000}),
                             RawValue::b64(std::uint64_t{0}),
                             RawValue::b64(std::uint64_t{0x4008000000000000}),
                             "0d4000000000000000"}));

INSTANTIATE_TEST_SUITE_P(
    PinnedSubForms, ValueAluPipeline,
    ::testing::Values(
        ValueAluPipelineCase{"sub.f32", RawValue::b32(0x40a00000U),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x40400000U)},
        ValueAluPipelineCase{"sub.f64",
                             RawValue::b64(std::uint64_t{0x4014000000000000}),
                             RawValue::b64(std::uint64_t{0x4000000000000000}),
                             RawValue::b64(std::uint64_t{0x4008000000000000})},
        ValueAluPipelineCase{"sub.f32x2",
                             RawValue::b64(std::uint64_t{0x3f80000040a00000}),
                             RawValue::b64(std::uint64_t{0x4000000040000000}),
                             RawValue::b64(std::uint64_t{0xbf80000040400000})},
        ValueAluPipelineCase{"sub.f16", RawValue::b16(std::uint16_t{0x4500}),
                             RawValue::b16(std::uint16_t{0x4000}),
                             RawValue::b16(std::uint16_t{0x4200})},
        ValueAluPipelineCase{"sub.f16x2", RawValue::b32(0x3c004500U),
                             RawValue::b32(0x40004000U),
                             RawValue::b32(0xbc004200U)},
        ValueAluPipelineCase{"sub.bf16", RawValue::b16(std::uint16_t{0x40a0}),
                             RawValue::b16(std::uint16_t{0x4000}),
                             RawValue::b16(std::uint16_t{0x4040})},
        ValueAluPipelineCase{"sub.bf16x2", RawValue::b32(0x3f8040a0U),
                             RawValue::b32(0x40004000U),
                             RawValue::b32(0xbf804040U)},
        ValueAluPipelineCase{
            "sub.f32.f16", RawValue::b16(std::uint16_t{0x4500}),
            RawValue::b32(0x40000000U), RawValue::b32(0x40400000U)},
        ValueAluPipelineCase{
            "sub.f32.bf16", RawValue::b16(std::uint16_t{0x40a0}),
            RawValue::b32(0x40000000U), RawValue::b32(0x40400000U)},
        ValueAluPipelineCase{"sub.u16", RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{2}),
                             RawValue::b16(std::uint16_t{0xffff})},
        ValueAluPipelineCase{"sub.u32", RawValue::b32(1U), RawValue::b32(2U),
                             RawValue::b32(0xffffffffU)},
        ValueAluPipelineCase{"sub.u64", RawValue::b64(std::uint64_t{1}),
                             RawValue::b64(std::uint64_t{2}),
                             RawValue::b64(std::uint64_t{0xffffffffffffffff})},
        ValueAluPipelineCase{"sub.s16", RawValue::b16(std::uint16_t{0x8000}),
                             RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{0x7fff})},
        ValueAluPipelineCase{"sub.s64",
                             RawValue::b64(std::uint64_t{0x8000000000000000}),
                             RawValue::b64(std::uint64_t{1}),
                             RawValue::b64(std::uint64_t{0x7fffffffffffffff})},
        ValueAluPipelineCase{"sub.s32", RawValue::b32(0x80000000U),
                             RawValue::b32(1U), RawValue::b32(0x7fffffffU)},
        ValueAluPipelineCase{"sub.u8x4", RawValue::b32(0x01000100U),
                             RawValue::b32(0x00010001U),
                             RawValue::b32(0x01ff01ffU)},
        ValueAluPipelineCase{"sub.s8x4", RawValue::b32(0x80007f00U),
                             RawValue::b32(0x0100ff00U),
                             RawValue::b32(0x7f008000U)},
        ValueAluPipelineCase{"sub.sat.s32", RawValue::b32(0x80000000U),
                             RawValue::b32(1U), RawValue::b32(0x80000000U)},
        ValueAluPipelineCase{"sub.sat.u8x4", RawValue::b32(0x01000100U),
                             RawValue::b32(0x00010001U),
                             RawValue::b32(0x01000100U)},
        ValueAluPipelineCase{"sub.sat.s8x4", RawValue::b32(0x80007f00U),
                             RawValue::b32(0x0100ff00U),
                             RawValue::b32(0x80007f00U)},
        ValueAluPipelineCase{"sub.s16", RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{0}),
                             RawValue::b16(std::uint16_t{2}), "-1"},
        ValueAluPipelineCase{"sub.s64", RawValue::b64(std::uint64_t{1}),
                             RawValue::b64(std::uint64_t{0}),
                             RawValue::b64(std::uint64_t{2}), "-1"},
        ValueAluPipelineCase{"sub.f32", RawValue::b32(0x40a00000U),
                             RawValue::b32(0U), RawValue::b32(0x40400000U),
                             "0f40000000"},
        ValueAluPipelineCase{"sub.f64",
                             RawValue::b64(std::uint64_t{0x4014000000000000}),
                             RawValue::b64(std::uint64_t{0}),
                             RawValue::b64(std::uint64_t{0x4008000000000000}),
                             "0d4000000000000000"},
        ValueAluPipelineCase{"sub.rn.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0x33000000U),
                             RawValue::b32(0x3f800000U)},
        ValueAluPipelineCase{"sub.rz.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0x33000000U),
                             RawValue::b32(0x3f7fffffU)},
        ValueAluPipelineCase{"sub.rm.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0x33000000U),
                             RawValue::b32(0x3f7fffffU)},
        ValueAluPipelineCase{"sub.rp.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0x33000000U),
                             RawValue::b32(0x3f800000U)},
        ValueAluPipelineCase{"sub.ftz.f32", RawValue::b32(1U),
                             RawValue::b32(0U), RawValue::b32(0U)},
        ValueAluPipelineCase{"sub.sat.f32", RawValue::b32(0x3f800000U),
                             RawValue::b32(0x40000000U), RawValue::b32(0U)},
        ValueAluPipelineCase{"sub.rn.sat.f32", RawValue::b32(0x40a00000U),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x3f800000U)},
        ValueAluPipelineCase{"sub.ftz.f16", RawValue::b16(std::uint16_t{1}),
                             RawValue::b16(std::uint16_t{0}),
                             RawValue::b16(std::uint16_t{0})},
        ValueAluPipelineCase{
            "sub.sat.f16", RawValue::b16(std::uint16_t{0x7e00}),
            RawValue::b16(std::uint16_t{0}), RawValue::b16(std::uint16_t{0})},
        ValueAluPipelineCase{"sub.sat.f32.f16",
                             RawValue::b16(std::uint16_t{0x3c00}),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0U),
                             {},
                             "sub.f32.f16.sat"},
        ValueAluPipelineCase{"sub.sat.f32.bf16",
                             RawValue::b16(std::uint16_t{0x3f80}),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0U),
                             {},
                             "sub.f32.bf16.sat"}));

INSTANTIATE_TEST_SUITE_P(
    PinnedMulForms, ValueAluPipeline,
    ::testing::Values(
        ValueAluPipelineCase{"mul.rn.f32", RawValue::b32(0x40000000U),
                             RawValue::b32(0x40400000U),
                             RawValue::b32(0x40c00000U)},
        ValueAluPipelineCase{"mul.rn.f32", RawValue::b32(0x3f800001U),
                             RawValue::b32(0x3f800001U),
                             RawValue::b32(0x3f800002U)},
        ValueAluPipelineCase{"mul.rn.f32", RawValue::b32(1U),
                             RawValue::b32(0x3f800000U), RawValue::b32(1U)},
        ValueAluPipelineCase{"mul.rn.f32", RawValue::b32(0x80000000U),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x80000000U)},
        ValueAluPipelineCase{"mul.rn.f32", RawValue::b32(0x7f800000U),
                             RawValue::b32(0x40000000U),
                             RawValue::b32(0x7f800000U)},
        ValueAluPipelineCase{"mul.lo.u32", RawValue::b32(0xffffffffU),
                             RawValue::b32(0xffffffffU), RawValue::b32(1U)},
        ValueAluPipelineCase{"mul.lo.u32", RawValue::b32(0x80000000U),
                             RawValue::b32(2U), RawValue::b32(0U), "2"},
        ValueAluPipelineCase{"mul.hi.u32", RawValue::b32(0xffffffffU),
                             RawValue::b32(0xffffffffU),
                             RawValue::b32(0xfffffffeU)},
        ValueAluPipelineCase{"mul.hi.u32", RawValue::b32(0x80000000U),
                             RawValue::b32(2U), RawValue::b32(1U), "2"},
        ValueAluPipelineCase{"mul.wide.u32", RawValue::b32(0xffffffffU),
                             RawValue::b32(0xffffffffU),
                             RawValue::b64(std::uint64_t{0xfffffffe00000001})},
        ValueAluPipelineCase{"mul.wide.u32", RawValue::b32(0xffffffffU),
                             RawValue::b32(2U),
                             RawValue::b64(std::uint64_t{0x1fffffffe}), "2"},
        ValueAluPipelineCase{"mul.wide.s32", RawValue::b32(0xffffffffU),
                             RawValue::b32(2U),
                             RawValue::b64(std::uint64_t{0xfffffffffffffffe})},
        ValueAluPipelineCase{"mul.wide.s32", RawValue::b32(0x80000000U),
                             RawValue::b32(0x80000000U),
                             RawValue::b64(std::uint64_t{0x4000000000000000})},
        ValueAluPipelineCase{"mul.wide.s32", RawValue::b32(0x80000000U),
                             RawValue::b32(0xffffffffU),
                             RawValue::b64(std::uint64_t{0x80000000}), "-1"}));

}  // namespace
}  // namespace ptxsim::simulator::test
