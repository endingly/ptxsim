#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/memory/register/register_error.hpp>
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

/** @brief Return the warp at @p index in the test runtime's only CTA. */
auto warp(runtime::LaunchRuntime& runtime, std::uint32_t index = 0)
    -> execution_model::Warp& {
  return runtime.grid()
      .cta(execution_model::CtaId{runtime.grid().id(), 0})
      .warp(index);
}

/** @brief Return one CTA containing four threads split into two warps. */
auto two_warp_shape() -> execution_model::GridShape {
  return {.cta_dim = {1, 1, 1}, .thread_dim = {4, 1, 1}, .warp_size = 2};
}

}  // namespace

TEST(Simulator, RunsLoweredMoveAddAndExitToCompletion) {
  auto program = exec_ir_lowering::lower(resolve(R"ptx(
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
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  const auto first_step = simulator.step();
  ASSERT_TRUE(first_step);
  EXPECT_EQ(first_step->termination, StepTermination::issued);
  ASSERT_TRUE(first_step->issue);
  EXPECT_EQ(first_step->issue->group.lanes.count(), 2U);

  const auto run = simulator.run(2);

  ASSERT_TRUE(run);
  EXPECT_EQ(*run, (RunReport{RunTermination::completed, 2}));
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

TEST(Simulator, StepsWarpsInTopologyOrderWithIndependentFrames) {
  auto program = exec_ir_lowering::lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %tid.x;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);

  runtime::LaunchRuntime runtime{execution_model::GridId{6}, two_warp_shape()};
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  const std::array expected_warps{warp(runtime, 0).id(), warp(runtime, 0).id(),
                                  warp(runtime, 1).id(), warp(runtime, 1).id()};

  for (std::size_t step = 0; step < expected_warps.size(); ++step) {
    const auto report = simulator.step();
    ASSERT_TRUE(report);
    ASSERT_TRUE(report->issue);
    EXPECT_EQ(report->issue->warp, expected_warps[step]);
    EXPECT_EQ(report->issue->group.lanes.count(), 2U);
    EXPECT_EQ(report->termination, step + 1U == expected_warps.size()
                                       ? StepTermination::completed
                                       : StepTermination::issued);
  }

  std::vector<memory::RegisterFrameHandle> frames;
  std::uint32_t expected_tid = 0;
  for (std::uint32_t warp_index = 0; warp_index < 2; ++warp_index) {
    for (const auto& thread : warp(runtime, warp_index)) {
      const auto frame =
          runtime.register_frame(thread.id(), common::FunctionId{0});
      ASSERT_TRUE(frame);
      for (const auto previous : frames) {
        EXPECT_NE(*frame, previous);
      }
      frames.push_back(*frame);
      const auto registers = runtime.registers().view(*frame);
      ASSERT_TRUE(registers);
      EXPECT_EQ(*registers->read(common::RegisterSlot{0}),
                common::RawValue::b32(expected_tid++));
    }
  }
}

TEST(Simulator, SelectsDivergentGroupsByLowestReadyLane) {
  auto program = exec_ir_lowering::lower(resolve(R"ptx(
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
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  const auto run = simulator.run(8);

  ASSERT_TRUE(run);
  EXPECT_EQ(*run, (RunReport{RunTermination::completed, 8}));
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

TEST(Simulator, RetainsFaultingLaneCausesInTrappedReport) {
  auto program = exec_ir_lowering::lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  mov.u32 %r1, %r0;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);

  runtime::LaunchRuntime runtime{execution_model::GridId{3}, two_warp_shape()};

  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  const auto run = simulator.run(2);

  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::trapped);
  EXPECT_EQ(run->issued_groups, 1U);
  EXPECT_EQ(run->faulting_warp, warp(runtime, 0).id());
  ASSERT_EQ(run->faults.size(), 2U);
  for (std::uint32_t lane = 0; lane < 2; ++lane) {
    EXPECT_EQ(run->faults[lane].lane, execution_model::LaneId{lane});
    ASSERT_TRUE(
        std::holds_alternative<memory::RegisterError>(run->faults[lane].cause));
    EXPECT_EQ(std::get<memory::RegisterError>(run->faults[lane].cause).code,
              memory::RegisterErrorCode::uninitialized_read);
  }
  for (const auto& thread : warp(runtime, 1)) {
    EXPECT_TRUE(thread.ready());
    EXPECT_EQ(thread.pc(), common::ProgramCounter{0});
  }
}

TEST(Simulator, ExecutesGlobalMemoryProgramAndRetainsMissingBindingFaults) {
  const auto program = exec_ir_lowering::lower(resolve(R"ptx(
.entry kernel() {
  .reg .b64 %addr;
  .reg .u32 %value;
  mov.b64 %addr, 0;
  ld.global.u32 %value, [%addr];
  add.u32 %value, %value, 1;
  st.global.u32 [%addr], %value;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const arith::context arithmetic;

  runtime::LaunchRuntime successful_runtime{execution_model::GridId{4},
                                            shape()};
  const auto global = successful_runtime.address_spaces().create_global({4});
  ASSERT_TRUE(successful_runtime.bind_global(global));
  auto memory = successful_runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(memory->initialize(
      memory::Address{0},
      std::array{std::byte{41}, std::byte{0}, std::byte{0}, std::byte{0}}));
  Simulator successful_simulator{*program, successful_runtime,
                                 common::FunctionId{0}, arithmetic};
  const auto successful_run = successful_simulator.run(5);

  ASSERT_TRUE(successful_run);
  EXPECT_EQ(*successful_run, (RunReport{RunTermination::completed, 5}));
  EXPECT_EQ(*memory->snapshot(memory::Address{0}, 4),
            (std::vector<std::byte>{std::byte{42}, std::byte{0}, std::byte{0},
                                    std::byte{0}}));

  runtime::LaunchRuntime unbound_runtime{execution_model::GridId{5}, shape()};
  Simulator unbound_simulator{*program, unbound_runtime, common::FunctionId{0},
                              arithmetic};
  const auto unbound_run = unbound_simulator.run(5);

  ASSERT_TRUE(unbound_run);
  EXPECT_EQ(unbound_run->termination, RunTermination::trapped);
  EXPECT_EQ(unbound_run->issued_groups, 2U);
  EXPECT_EQ(unbound_run->faults,
            (std::vector<inst_execute_engine::LaneFault>{
                {execution_model::LaneId{0},
                 runtime::RuntimeBindingError{
                     runtime::RuntimeBindingErrorCode::missing_binding,
                     runtime::RuntimeResourceKind::global}},
                {execution_model::LaneId{1},
                 runtime::RuntimeBindingError{
                     runtime::RuntimeBindingErrorCode::missing_binding,
                     runtime::RuntimeResourceKind::global}},
            }));
}

}  // namespace ptxsim::simulator::test
