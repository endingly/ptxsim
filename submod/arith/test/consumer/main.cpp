#include <ptxsim/arith/arith.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/instruction.hpp>
#include <ptxsim/exec_ir/operand.hpp>
#include <ptxsim/program/program_image.hpp>

int main() {
  ptxsim::arith::context context;
  const auto value = ptxsim::arith::cvt<ptxsim::arith::float32_t>(context, 1);
  const auto raw = ptxsim::common::RawValue::b32(std::uint32_t{7});
  const auto operand = ptxsim::exec_ir::RegisterOperand{
      ptxsim::common::RegisterSlot{1}, ptxsim::common::RawWidth::b32};
  const auto instruction = ptxsim::exec_ir::MovInst{
      operand,
      ptxsim::exec_ir::ValueOperand{ptxsim::exec_ir::ImmediateOperand{
          ptxsim::common::RawValue::b32(std::uint32_t{7})}},
      std::nullopt};
  const auto image = ptxsim::program::ProgramImage::create({
      .instructions = {instruction},
      .functions = {{ptxsim::common::FunctionId{0},
                     "main",
                     ptxsim::common::ProgramCounter{0},
                     ptxsim::common::ProgramCounter{1},
                     {{ptxsim::common::RegisterSlot{0},
                       ptxsim::common::RawWidth::b32},
                      {ptxsim::common::RegisterSlot{1},
                       ptxsim::common::RawWidth::b32}}}},
      .entry_points = {ptxsim::common::FunctionId{0}},
      .source_locations_by_pc = {std::nullopt},
  });
  return value &&
                 ptxsim::common::to_string(ptxsim::common::ProgramCounter{7}) ==
                     "pc:7" &&
                 raw.as_b32() && *raw.as_b32() == 7 &&
                 ptxsim::common::to_string(raw) == "b32:0x00000007" &&
                 ptxsim::exec_ir::to_string(operand) == "register:1:b32" &&
                 ptxsim::exec_ir::validate(instruction) && image &&
                 ptxsim::program::verify(*image) &&
                 !ptxsim::program::dump(*image).empty()
             ? 0
             : 1;
}
