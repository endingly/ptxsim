#include <gtest/gtest.h>

#include <array>
#include <cstdint>
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

/** @brief PTX control flow and its independently expected per-lane result. */
struct ControlFlowCase {
  /** Stable failure label for the branch or exit behavior. */
  std::string_view name;
  /** Body following initialization of r=0 and p=(tid.x==0). */
  std::string_view body;
  /** Expected r for lanes zero and one after termination. */
  std::array<std::uint32_t, 2> result;
};

}  // namespace

TEST(ControlFlowPipeline, PreservesThreadPcAcrossBranchesAndExits) {
  const std::array cases{
      ControlFlowCase{
          "direct", "bra done; add.u32 %r, %r, 99; done: exit;", {0, 0}},
      ControlFlowCase{
          "uniform", "bra.uni done; add.u32 %r, %r, 99; done: exit;", {0, 0}},
      ControlFlowCase{
          "divergent", "@%p bra done; add.u32 %r, %r, 7; done: exit;", {0, 7}},
      ControlFlowCase{
          "negated", "@!%p bra done; add.u32 %r, %r, 7; done: exit;", {7, 0}},
      ControlFlowCase{"uniform_false",
                      "setp.lt.u32 %p, %r, 0; @%p bra.uni done; add.u32 %r, "
                      "%r, 7; done: exit;",
                      {7, 7}},
      ControlFlowCase{"uniform_true",
                      "setp.lt.u32 %p, %r, 1; @%p bra.uni done; add.u32 %r, "
                      "%r, 7; done: exit;",
                      {0, 0}},
      ControlFlowCase{
          "predicated_exit", "@%p exit; add.u32 %r, %r, 7; exit;", {0, 7}},
      ControlFlowCase{
          "negated_exit", "@!%p exit; add.u32 %r, %r, 7; exit;", {7, 0}},
      ControlFlowCase{
          "backedge",
          "loop: add.u32 %r, %r, 1; setp.lt.u32 %p, %r, 3; @%p bra loop; exit;",
          {3, 3}},
  };
  for (const auto& item : cases) {
    SCOPED_TRACE(item.name);
    const std::string text =
        ".entry kernel() { .reg .u32 %r; .reg .u32 %lane; .reg .pred %p; "
        "mov.u32 %lane, %tid.x; sub.u32 %r, %lane, %lane; setp.lt.u32 %p, "
        "%lane, 1; " +
        std::string(item.body) + " }";
    ptx_frontend::PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
    ASSERT_TRUE(resolved);
    auto program = exec_ir_lowering::lower(*resolved);
    ASSERT_TRUE(program) << "LoweringErrorCode="
                         << static_cast<unsigned>(program.error().code)
                         << " instruction="
                         << program.error().instruction.value_or(9999);
    runtime::LaunchRuntime runtime{
        execution_model::GridId{43},
        {.cta_dim = {1, 1, 1}, .thread_dim = {2, 1, 1}, .warp_size = 2}};
    const arith::context arithmetic;
    Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                        arithmetic};
    const auto run = simulator.run(32);
    ASSERT_TRUE(run);
    ASSERT_EQ(run->termination, RunTermination::completed);
    const auto& warp = runtime.grid().cta({runtime.grid().id(), 0}).warp(0);
    for (const auto& thread : warp) {
      EXPECT_TRUE(thread.exited());
      const auto frame =
          runtime.register_frame(thread.id(), common::FunctionId{0});
      ASSERT_TRUE(frame);
      const auto registers = runtime.registers().view(*frame);
      ASSERT_TRUE(registers);
      const auto value = registers->read(common::RegisterSlot{0});
      ASSERT_TRUE(value);
      EXPECT_EQ(*value,
                common::RawValue::b32(item.result[thread.lane_id().value]));
    }
  }
}

}  // namespace ptxsim::simulator::test
