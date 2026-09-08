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

/** @brief Independently specified predicate results for a real PTX comparison. */
struct SetpPipelineCase {
  /** Opcode and modifiers from the pinned frontend specification. */
  std::string_view instruction;
  /** Whether the instruction writes both predicate destinations. */
  bool pair;
  /** Exact 32-bit source encodings, including signed two's-complement values. */
  std::uint32_t lhs, rhs;
  /** Empty without Boolean combination; otherwise %c or !%c. */
  std::string_view combine;
  /** Predicate source value before execution. */
  bool combine_value;
  /** Expected first and second predicates; single-output forms preserve q=true. */
  bool first, second;
  /** Optional PTX immediate replacing the second numeric register source. */
  std::string_view rhs_literal{};
};

/** @brief Describe a comparison case without exposing raw struct storage. */
void PrintTo(const SetpPipelineCase& item, std::ostream* output) {
  *output << item.instruction << (item.pair ? " pair" : " single")
          << " lhs=" << item.lhs << " rhs=" << item.rhs
          << " combine=" << item.combine << ':' << item.combine_value;
  if (!item.rhs_literal.empty())
    *output << " immediate=" << item.rhs_literal;
}

/** @brief Exercise predicate comparisons through resolve, lowering and execution. */
class SetpPipeline : public ::testing::TestWithParam<SetpPipelineCase> {};

TEST_P(SetpPipeline, ExecutesPinnedPredicateForms) {
  const auto& item = GetParam();
  const std::string text =
      ".entry kernel() {\n.reg .pred %p;\n.reg .pred %q;\n"
      ".reg .b32 %a;\n.reg .b32 %b;\n.reg .pred %c;\n" +
      std::string(item.instruction) + (item.pair ? " %p|%q" : " %p") +
      ", %a, " +
      std::string(item.rhs_literal.empty() ? "%b" : item.rhs_literal) +
      (item.combine.empty() ? "" : ", " + std::string(item.combine)) +
      ";\nexit;\n}";
  SCOPED_TRACE(text);
  ptx_frontend::PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved);
  auto program = exec_ir_lowering::lower(*resolved);
  ASSERT_TRUE(program) << "LoweringErrorCode="
                       << static_cast<unsigned>(program.error().code);

  runtime::LaunchRuntime runtime{
      execution_model::GridId{42},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  ASSERT_TRUE(simulator.run(0));
  const auto& thread = runtime.grid()
                           .cta(execution_model::CtaId{runtime.grid().id(), 0})
                           .warp(0)
                           .thread(execution_model::LaneId{0});
  const auto frame = runtime.register_frame(thread.id(), common::FunctionId{0});
  ASSERT_TRUE(frame);
  auto registers = runtime.registers().view(*frame);
  ASSERT_TRUE(registers);
  ASSERT_TRUE(
      registers->write(common::RegisterSlot{0}, common::RawValue::pred(false)));
  ASSERT_TRUE(
      registers->write(common::RegisterSlot{1}, common::RawValue::pred(true)));
  ASSERT_TRUE(registers->write(common::RegisterSlot{2},
                               common::RawValue::b32(item.lhs)));
  ASSERT_TRUE(registers->write(common::RegisterSlot{3},
                               common::RawValue::b32(item.rhs)));
  ASSERT_TRUE(registers->write(common::RegisterSlot{4},
                               common::RawValue::pred(item.combine_value)));

  const auto run = simulator.run(2);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  const auto first = registers->read(common::RegisterSlot{0});
  const auto second = registers->read(common::RegisterSlot{1});
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(*first, common::RawValue::pred(item.first));
  EXPECT_EQ(*second, common::RawValue::pred(item.second));
}

INSTANTIATE_TEST_SUITE_P(
    PinnedSetpForms, SetpPipeline,
    ::testing::Values(
        SetpPipelineCase{"setp.lt.u32", false, 0xffffffffU, 0U, "", false,
                         false, true},
        SetpPipelineCase{"setp.lt.u32", false, 0U, 0xffffffffU, "", false, true,
                         true},
        SetpPipelineCase{"setp.lt.u32", false, 7U, 7U, "", false, false, true},
        SetpPipelineCase{"setp.lt.u32", false, 15U, 16U, "", false, true, true,
                         "16"},
        SetpPipelineCase{"setp.ge.s32", false, 0x80000000U, 0U, "", false,
                         false, true},
        SetpPipelineCase{"setp.ge.s32", false, 0xffffffffU, 0x80000000U, "",
                         false, true, true},
        SetpPipelineCase{"setp.ge.s32", false, 0xffffffffU, 0xffffffffU, "",
                         false, true, true, "-1"},
        SetpPipelineCase{"setp.lt.and.u32", false, 1U, 2U, "%c", false, false,
                         true},
        SetpPipelineCase{"setp.lt.and.u32", false, 1U, 2U, "%c", true, true,
                         true},
        SetpPipelineCase{"setp.lt.and.u32", false, 1U, 2U, "!%c", false, true,
                         true},
        SetpPipelineCase{"setp.lt.and.u32", false, 2U, 1U, "!%c", false, false,
                         true},
        SetpPipelineCase{"setp.eq.u32", true, 7U, 7U, "", false, true, false},
        SetpPipelineCase{"setp.eq.u32", true, 7U, 8U, "", false, false, true},
        SetpPipelineCase{"setp.lt.and.s32", true, 0x80000000U, 0U, "%c", true,
                         true, false},
        SetpPipelineCase{"setp.lt.and.s32", true, 0x80000000U, 0U, "%c", false,
                         false, false},
        SetpPipelineCase{"setp.lt.and.s32", true, 0U, 0x80000000U, "%c", true,
                         false, true},
        SetpPipelineCase{"setp.lt.and.s32", true, 0U, 0x80000000U, "%c", false,
                         false, false}));

}  // namespace
}  // namespace ptxsim::simulator::test
