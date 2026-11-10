#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.gen.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {
/** @brief Construct a real typed view whose slot may deliberately have no runtime backing. */
auto reg(std::uint32_t slot,exec_ir::DataType type) -> exec_ir::RegisterRef {
  return {.slot=common::RegisterSlot{slot},
          .register_class=type==exec_ir::DataType::pred?exec_ir::RegisterClass::Predicate:exec_ir::RegisterClass::General,
          .declared_type=type};
}
/** @brief Freeze an otherwise coherent entry while retaining ordinary lazy operand identities. */
auto entry(std::vector<std::unique_ptr<exec_ir::Instruction>> instructions)
    -> std::expected<exec_ir::ExecutableProgram,exec_ir::ProgramError> {
  exec_ir::ProgramDefinition definition;
  definition.function_declarations={{.id=exec_ir::FunctionDeclarationId{0},.function=common::FunctionId{0},
      .name="entry",.is_entry=true}};
  definition.functions={{.id=common::FunctionId{0},.name="entry",.is_entry=true,
      .declarations={exec_ir::FunctionDeclarationId{0}},.definition=exec_ir::FunctionDeclarationId{0},
      .body=exec_ir::FunctionLayout{.id=common::FunctionId{0},.instruction_count=static_cast<std::uint32_t>(instructions.size()),
                                   .register_widths={common::RawWidth::pred}}}};
  definition.instructions=std::move(instructions);
  return exec_ir::ExecutableProgram::create(definition);
}
/** @brief Build an unimplemented exact form with invalid ordinary IDs that must remain unread. */
auto unknown(std::optional<exec_ir::Predicate> guard=std::nullopt) -> std::unique_ptr<exec_ir::Instruction> {
  return std::make_unique<exec_ir::DivRnF32>(exec_ir::DivRnF32::Operands{
      reg(999,exec_ir::DataType::f32),exec_ir::Immediate{0,exec_ir::DataType::f32},reg(888,exec_ir::DataType::f32)},guard);
}
/** @brief Access the single lane whose PC/status prove the issue had no side effects. */
auto thread(runtime::LaunchRuntime& runtime) -> execution_model::Thread& {
  return runtime.grid().cta(execution_model::CtaId{runtime.grid().id(),0}).warp(0).thread(execution_model::LaneId{0});
}
}  // namespace
TEST(SuccessorOrder, TerminalUnsupportedPrecedesFallthroughAndInvalidOrFalsePredicate) {
  const std::array guards{std::optional<exec_ir::Predicate>{},
      std::optional{exec_ir::Predicate{reg(999,exec_ir::DataType::pred)}},
      std::optional{exec_ir::Predicate{reg(0,exec_ir::DataType::pred)}}};
  for(const auto& guard:guards) {
    std::vector<std::unique_ptr<exec_ir::Instruction>> instructions;instructions.push_back(unknown(guard));
    const auto program=entry(std::move(instructions));ASSERT_TRUE(program);
    runtime::LaunchRuntime runtime{execution_model::GridId{404},{.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}};
    const arith::context arithmetic;Simulator simulator{*program,runtime,common::FunctionId{0},arithmetic};
    ASSERT_TRUE(simulator.run(0));
    const auto status=thread(runtime).status();
    const auto frame=runtime.register_frame(thread(runtime).id(),common::FunctionId{0});ASSERT_TRUE(frame);
    auto registers=runtime.registers().view(*frame);ASSERT_TRUE(registers);
    ASSERT_TRUE(registers->write(common::RegisterSlot{0},common::RawValue::pred(false)));
    const auto result=simulator.step();ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,RunErrorCode::execution_error);ASSERT_TRUE(result.error().execution_error);
    EXPECT_EQ(result.error().execution_error->code,inst_execute_engine::StepErrorCode::unsupported_instruction);
    EXPECT_FALSE(result.error().program_error);
    EXPECT_EQ(thread(runtime).pc(),common::ProgramCounter{0});EXPECT_EQ(thread(runtime).status(),status);
    EXPECT_EQ(*registers->read(common::RegisterSlot{0}),common::RawValue::pred(false));
  }
}
TEST(SuccessorOrder, SupportedTerminalFormsReportProgramContinuationBeforeAnyOperandRead) {
  const exec_ir::Predicate guard{reg(999,exec_ir::DataType::pred)};
  std::vector<std::unique_ptr<exec_ir::Instruction>> cases;
  cases.push_back(std::make_unique<exec_ir::AddIntegerNoSat>(exec_ir::AddIntegerNoSat::Operands{
      reg(999,exec_ir::DataType::u32),reg(888,exec_ir::DataType::u32),exec_ir::Immediate{1,exec_ir::DataType::u32}},
      exec_ir::AddIntegerNoSat::Controls{exec_ir::DataType::u32},guard));
  cases.push_back(std::make_unique<exec_ir::ExitBare>(exec_ir::ExitBare::Operands{},guard));
  cases.push_back(std::make_unique<exec_ir::BraDirect>(exec_ir::BraDirect::Operands{
      {common::FunctionId{0},common::ProgramCounter{0}}},guard));
  for(auto& instruction:cases) {
    std::vector<std::unique_ptr<exec_ir::Instruction>> instructions;instructions.push_back(std::move(instruction));
    const auto program=entry(std::move(instructions));ASSERT_TRUE(program);
    runtime::LaunchRuntime runtime{execution_model::GridId{405},{.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}};
    const arith::context arithmetic;Simulator simulator{*program,runtime,common::FunctionId{0},arithmetic};
    ASSERT_TRUE(simulator.run(0));const auto status=thread(runtime).status();
    const auto result=simulator.step();ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,RunErrorCode::program_error);ASSERT_TRUE(result.error().program_error);
    EXPECT_EQ(result.error().program_error->code,exec_ir::ProgramErrorCode::no_fallthrough);
    EXPECT_FALSE(result.error().execution_error);
    EXPECT_EQ(thread(runtime).pc(),common::ProgramCounter{0});EXPECT_EQ(thread(runtime).status(),status);
  }
}
TEST(SuccessorOrder, UnreachableUnsupportedRemainsLoadableAndUnissued) {
  std::vector<std::unique_ptr<exec_ir::Instruction>> instructions;
  instructions.push_back(std::make_unique<exec_ir::BraDirect>(exec_ir::BraDirect::Operands{
      {common::FunctionId{0},common::ProgramCounter{2}}}));
  instructions.push_back(unknown(exec_ir::Predicate{reg(999,exec_ir::DataType::pred)}));
  instructions.push_back(std::make_unique<exec_ir::ExitBare>(exec_ir::ExitBare::Operands{}));
  const auto program=entry(std::move(instructions));ASSERT_TRUE(program);
  runtime::LaunchRuntime runtime{execution_model::GridId{406},{.cta_dim={1,1,1},.thread_dim={1,1,1},.warp_size=1}};
  const arith::context arithmetic;Simulator simulator{*program,runtime,common::FunctionId{0},arithmetic};
  const auto result=simulator.run(2);ASSERT_TRUE(result);
  EXPECT_EQ(result->termination,RunTermination::completed);EXPECT_TRUE(runtime.grid().completed());
}
}  // namespace ptxsim::simulator::test
