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

/** @brief One real-PTX CTA collective program expected to terminate normally. */
struct BarrierPipelineCase {
  /** Stable name printed when parsing, lowering, or execution fails. */
  std::string_view name;
  /** Instruction sequence following the common thread-ID setup. */
  std::string_view body;
  /** Expected numeric and predicate results in every surviving lane. */
  std::uint32_t output = 77;
  bool predicate = false;
  /** Whether the program needs a CTA-local shared-memory binding. */
  bool shared_memory = false;
};

/** @brief Exercise persistent CTA barriers through parse, lower, and scheduling. */
TEST(BarrierPipeline, SynchronizesTwoWarpsAndReusesGenerations) {
  const std::array cases{
      BarrierPipelineCase{"sync_reuse", "bar.sync 0; bar.sync 0; exit;"},
      BarrierPipelineCase{"cta_sync_registers",
                          "bar.cta.sync %bar, %count; exit;"},
      BarrierPipelineCase{"sync_register", "bar.sync %bar; exit;"},
      BarrierPipelineCase{"cta_sync", "bar.cta.sync 0; exit;"},
      BarrierPipelineCase{"arrive", "bar.arrive 0, 4; exit;"},
      BarrierPipelineCase{"cta_arrive", "bar.cta.arrive %bar, %count; exit;"},
      BarrierPipelineCase{"sync_arrive_mix",
                          "@%p bar.sync 1, 4; @!%p bar.arrive 1, 4; exit;"},
      BarrierPipelineCase{
          "cta_sync_arrive_mix",
          "@%p bar.cta.sync 1, 4; @!%p bar.cta.arrive 1, 4; exit;"},
      BarrierPipelineCase{"popc", "bar.red.popc.u32 %out, 0, %p; exit;", 2},
      BarrierPipelineCase{"popc_count",
                          "bar.red.popc.u32 %out, %bar, %count, !%p; exit;", 2},
      BarrierPipelineCase{"cta_popc", "bar.cta.red.popc.u32 %out, 0, %p; exit;",
                          2},
      BarrierPipelineCase{"cta_popc_count",
                          "bar.cta.red.popc.u32 %out, 0, 4, !%p; exit;", 2},
      BarrierPipelineCase{
          "and",
          "setp.lt.u32 %q, %out, 100; bar.red.and.pred %q, 0, %p; exit;"},
      BarrierPipelineCase{"and_count", "bar.red.and.pred %q, 0, 4, !%q; exit;",
                          77, true},
      BarrierPipelineCase{"cta_and", "bar.cta.red.and.pred %q, 0, !%q; exit;",
                          77, true},
      BarrierPipelineCase{"cta_and_count",
                          "setp.lt.u32 %q, %out, 100; bar.cta.red.and.pred %q, "
                          "%bar, %count, %p; exit;"},
      BarrierPipelineCase{"or", "bar.red.or.pred %q, 0, %p; exit;", 77, true},
      BarrierPipelineCase{"or_count", "bar.red.or.pred %q, 0, 4, !%p; exit;",
                          77, true},
      BarrierPipelineCase{"cta_or", "bar.cta.red.or.pred %q, 0, %p; exit;", 77,
                          true},
      BarrierPipelineCase{"cta_or_count",
                          "setp.lt.u32 %q, %out, 100; bar.cta.red.or.pred %q, "
                          "0, 4, !%q; exit;"},
      BarrierPipelineCase{"warp_sync", "bar.warp.sync 3; exit;"},
      BarrierPipelineCase{"exit_releases_cta", "@!%p exit; bar.sync 0; exit;"},
      BarrierPipelineCase{
          "shared_visibility",
          "mul.lo.u32 %addr, %lane, 4; st.shared.u32 [%addr], %lane; "
          "bar.sync 0; ld.shared.u32 %out, [12]; exit;",
          3, false, true},
  };
  for (const auto& item : cases) {
    SCOPED_TRACE(item.name);
    const std::string text =
        ".entry kernel() { .reg .u32 %out; .reg .u32 %lane; .reg .pred %p; "
        ".reg .pred %q; .reg .u32 %addr; .reg .u32 %bar; .reg .u32 %count; "
        "mov.u32 %lane, %tid.x; sub.u32 %bar, %lane, %lane; "
        "add.u32 %out, %bar, 77; setp.lt.u32 %p, %lane, 2; "
        "setp.lt.u32 %q, %out, 0; add.u32 %count, %bar, 4; " +
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
        execution_model::GridId{71},
        {.cta_dim = {1, 1, 1}, .thread_dim = {4, 1, 1}, .warp_size = 2}};
    if (item.shared_memory) {
      const auto shared = runtime.address_spaces().create_shared({64});
      ASSERT_TRUE(runtime.bind_shared({runtime.grid().id(), 0}, shared));
    }
    const arith::context arithmetic;
    Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                        arithmetic};
    const auto run = simulator.run(64);
    ASSERT_TRUE(run);
    ASSERT_EQ(run->termination, RunTermination::completed);
    for (const auto& warp : runtime.grid().cta({runtime.grid().id(), 0})) {
      for (const auto& thread : warp) {
        const auto frame =
            runtime.register_frame(thread.id(), common::FunctionId{0});
        ASSERT_TRUE(frame);
        const auto registers = runtime.registers().view(*frame);
        ASSERT_TRUE(registers);
        const auto output = registers->read(common::RegisterSlot{0});
        const auto predicate = registers->read(common::RegisterSlot{3});
        ASSERT_TRUE(output);
        ASSERT_TRUE(predicate);
        EXPECT_EQ(*output, common::RawValue::b32(item.output));
        EXPECT_EQ(*predicate, common::RawValue::pred(item.predicate));
      }
    }
  }
}

}  // namespace
}  // namespace ptxsim::simulator::test
