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

#include <optional>

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
                 run->issued_groups == 1
             ? 0
             : 1;
}
