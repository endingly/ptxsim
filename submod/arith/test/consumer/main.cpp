#include <ptxsim/arith/arith.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/execution_model/execution_model.hpp>
#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>
#include <ptxsim/memory/memory.hpp>
#include <ptxsim/runtime/runtime.hpp>
#include <ptxsim/simulator/simulator.hpp>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

/** @brief Exercise installed frontend metadata, argument packing and execution. */
auto entry_argument_consumer() -> bool {
  using namespace ptxsim;
  ptx_frontend::PtxSyntaxParser parser(R"ptx(
.entry arguments(.param .u64 output, .param .u32 value) {
  .reg .u64 %address;
  .reg .u32 %value;
  ld.param.u64 %address, [output];
  ld.param.u32 %value, [value];
  st.global.u32 [%address], %value;
  exit;
}
)ptx");
  const auto ast = parser.parseModule();
  if (!ast) {
    return false;
  }
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!resolved) {
    return false;
  }
  auto program = exec_ir_lowering::lower(*resolved);
  if (!program) {
    return false;
  }
  const auto layout = program->function_layout(common::FunctionId{0});
  if (!layout || layout->get().entry_parameter_size != 12 ||
      layout->get().entry_parameters.size() != 2 ||
      layout->get().entry_parameters[1].offset != 8) {
    return false;
  }
  // The first argument is offset eight in simulated global memory.
  const std::array address{std::byte{8}, std::byte{0}, std::byte{0},
                           std::byte{0}, std::byte{0}, std::byte{0},
                           std::byte{0}, std::byte{0}};
  const std::array value{std::byte{42}, std::byte{0}, std::byte{0},
                         std::byte{0}};
  const std::array arguments{std::span<const std::byte>{address},
                             std::span<const std::byte>{value}};
  auto packed = simulator::pack_entry_arguments(*program, common::FunctionId{0},
                                                arguments);
  if (!packed) {
    return false;
  }
  runtime::LaunchRuntime runtime{
      execution_model::GridId{1},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 32}};
  const auto global = runtime.address_spaces().create_global({16});
  if (!runtime.bind_global(global)) {
    return false;
  }
  const arith::context arithmetic;
  simulator::Simulator runner{std::move(*program), runtime,
                              common::FunctionId{0}, arithmetic,
                              std::move(*packed)};
  const auto run = runner.run(4);
  const auto memory = runtime.address_spaces().view(global);
  if (!run || run->termination != simulator::RunTermination::completed ||
      !memory) {
    return false;
  }
  const auto output = memory->snapshot(memory::Address{8}, 4);
  return output &&
         *output == std::vector<std::byte>(value.begin(), value.end());
}

}  // namespace

int main() {
  ptxsim::arith::context context;
  const auto value = ptxsim::arith::cvt<ptxsim::arith::float32_t>(context, 1);
  const auto raw = ptxsim::common::RawValue::b32(std::uint32_t{7});
  const ptxsim::execution_model::Grid grid{
      ptxsim::execution_model::GridId{0},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 32}};
  ptxsim::memory::MemoryRegion memory{sizeof(std::uint32_t)};
  ptxsim::runtime::LaunchRuntime runtime{
      ptxsim::execution_model::GridId{0},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 32}};
  const auto program = ptxsim::exec_ir::ExecutableProgram::create({
      .instructions = {ptxsim::exec_ir::Exit{
          std::nullopt,
          ptxsim::exec_ir::Exit::Variant{ptxsim::exec_ir::Exit::Bare{}}}},
      .functions = {{ptxsim::common::FunctionId{0}, 0, 1, {}}},
  });
  if (!program) {
    return 1;
  }
  ptxsim::simulator::Simulator simulator{
      *program, runtime, ptxsim::common::FunctionId{0}, context};
  const auto run = simulator.run(1);
  return value &&
                 ptxsim::common::to_string(ptxsim::common::ProgramCounter{7}) ==
                     "pc:7" &&
                 raw.as_b32() && *raw.as_b32() == 7 &&
                 ptxsim::common::to_string(raw) == "b32:0x00000007" &&
                 grid.thread_count() == 1 &&
                 memory.size() == sizeof(std::uint32_t) &&
                 runtime.grid().thread_count() == 1 && run &&
                 run->termination ==
                     ptxsim::simulator::RunTermination::completed &&
                 run->issued_groups == 1 && entry_argument_consumer()
             ? 0
             : 1;
}
