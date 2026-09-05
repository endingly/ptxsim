#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {

/** @brief Parse and resolve one PTX module for a simulator integration test. */
auto resolve(std::string_view source)
    -> ptx_frontend::resolved_ir::ResolvedModule {
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast) {
    ADD_FAILURE() << "PTX parse failed";
    return {};
  }
  auto module = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!module) {
    ADD_FAILURE() << "PTX resolve failed";
    return {};
  }
  return std::move(*module);
}

/** @brief Return the two-thread launch shape used by all simulator tests. */
auto shape() -> execution_model::GridShape {
  return {.cta_dim = {1, 1, 1}, .thread_dim = {2, 1, 1}, .warp_size = 2};
}

/** @brief Return the only warp in the test runtime. */
auto warp(runtime::LaunchRuntime& runtime) -> execution_model::Warp& {
  return runtime.grid()
      .cta(execution_model::CtaId{runtime.grid().id(), 0})
      .warp(0);
}

}  // namespace

TEST(Simulator, RunsLoweredMoveAddAndExitToCompletion) {
  const auto program = exec_ir_lowering::lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %tid.x;
  add.u32 %r1, %r0, 7;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);

  runtime::LaunchRuntime runtime{execution_model::GridId{1}, shape()};

  const arith::context arithmetic;
  const auto run = run_to_completion(runtime, *program, common::FunctionId{0},
                                     arithmetic, 3);

  ASSERT_TRUE(run);
  EXPECT_EQ(*run, (RunReport{RunTermination::completed, 3}));
  EXPECT_TRUE(runtime.grid().completed());
  for (std::uint32_t lane = 0; lane < 2; ++lane) {
    const auto frame = runtime.register_frame(
        warp(runtime).thread(execution_model::LaneId{lane}).id(),
        common::FunctionId{0});
    ASSERT_TRUE(frame);
    const auto registers = runtime.registers().view(*frame);
    ASSERT_TRUE(registers);
    EXPECT_EQ(*registers->read(common::RegisterSlot{0}),
              common::RawValue::b32(lane));
    EXPECT_EQ(*registers->read(common::RegisterSlot{1}),
              common::RawValue::b32(7U + lane));
  }
}

TEST(Simulator, GroupsDivergentLanesByProgramCounter) {
  const auto program = exec_ir_lowering::lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .u32 %r<2>;
  mov.u32 %r0, %tid.x;
  setp.lt.u32 %p, %r0, 1;
  @%p bra target;
  add.u32 %r0, %r0, 10;
target:
  add.u32 %r1, %r0, 2;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);

  runtime::LaunchRuntime runtime{execution_model::GridId{2}, shape()};

  const arith::context arithmetic;
  const auto run = run_to_completion(runtime, *program, common::FunctionId{0},
                                     arithmetic, 6);

  ASSERT_TRUE(run);
  EXPECT_EQ(*run, (RunReport{RunTermination::completed, 6}));
  for (std::uint32_t lane = 0; lane < 2; ++lane) {
    const auto frame = runtime.register_frame(
        warp(runtime).thread(execution_model::LaneId{lane}).id(),
        common::FunctionId{0});
    ASSERT_TRUE(frame);
    const auto registers = runtime.registers().view(*frame);
    ASSERT_TRUE(registers);
    EXPECT_EQ(*registers->read(common::RegisterSlot{1}),
              common::RawValue::b32(lane == 0U ? 0U : 11U));
    EXPECT_EQ(*registers->read(common::RegisterSlot{2}),
              common::RawValue::b32(lane == 0U ? 2U : 13U));
  }
}

}  // namespace ptxsim::simulator::test
