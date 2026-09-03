#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::exec_ir::test {
namespace {

using common::CodeLocation;
using common::FunctionId;
using common::ProgramCounter;
using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;

auto valid_definition() -> ProgramDefinition {
  return {
      .instructions =
          {
              {.predicate = std::nullopt,
               .operation =
                   Move{DataType::b32, RegisterSlot{1}, RegisterSlot{0}}},
              {.predicate = Predicate{RegisterSlot{2}, true},
               .operation = Add{DataType::u32, RegisterSlot{1},
                                B32Operand{RegisterSlot{1}},
                                B32Operand{RawValue::b32(1U)}}},
              {.predicate = std::nullopt,
               .operation = Branch{ProgramCounter{0}}},
              {.predicate = std::nullopt, .operation = Exit{}},
          },
      .functions =
          {
              {FunctionId{0},
               0,
               3,
               {RawWidth::b32, RawWidth::b32, RawWidth::pred}},
              {FunctionId{1}, 3, 1, {}},
          },
  };
}

auto make_program() -> ExecutableProgram {
  auto program = ExecutableProgram::create(valid_definition());
  EXPECT_TRUE(program);
  return std::move(*program);
}

TEST(ExecutableProgram, FetchUsesFunctionLocalProgramCounters) {
  const auto program = make_program();

  const auto first = program.fetch({FunctionId{0}, ProgramCounter{0}});
  const auto second = program.fetch({FunctionId{1}, ProgramCounter{0}});

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(&first->get(), &second->get());
  EXPECT_TRUE(std::holds_alternative<Move>(first->get().operation));
  EXPECT_TRUE(std::holds_alternative<Exit>(second->get().operation));
  EXPECT_EQ(op(first->get().operation), Op::mov);
  EXPECT_EQ(op(second->get().operation), Op::exit);
}

TEST(ExecutableProgram, DerivesFlatOffsetsAndSameFunctionFallthrough) {
  const auto program = make_program();

  EXPECT_EQ(*program.flat_offset({FunctionId{0}, ProgramCounter{2}}), 2U);
  EXPECT_EQ(*program.flat_offset({FunctionId{1}, ProgramCounter{0}}), 3U);
  EXPECT_EQ(*program.fallthrough({FunctionId{0}, ProgramCounter{1}}),
            (CodeLocation{FunctionId{0}, ProgramCounter{2}}));
  const auto final_fallthrough =
      program.fallthrough({FunctionId{1}, ProgramCounter{0}});
  ASSERT_FALSE(final_fallthrough);
  EXPECT_EQ(final_fallthrough.error().code, ProgramErrorCode::no_fallthrough);
}

TEST(ExecutableProgram, RejectsInvalidLocationsAndDefinitions) {
  const auto program = make_program();
  const auto missing_function =
      program.fetch({FunctionId{2}, ProgramCounter{0}});
  ASSERT_FALSE(missing_function);
  EXPECT_EQ(missing_function.error().code,
            ProgramErrorCode::function_not_found);
  const auto missing_pc = program.fetch({FunctionId{1}, ProgramCounter{1}});
  ASSERT_FALSE(missing_pc);
  EXPECT_EQ(missing_pc.error().code, ProgramErrorCode::pc_out_of_range);

  auto invalid_layout = valid_definition();
  invalid_layout.functions[1].begin = 2;
  const auto layout_result =
      ExecutableProgram::create(std::move(invalid_layout));
  ASSERT_FALSE(layout_result);
  EXPECT_EQ(layout_result.error().code, ProgramErrorCode::invalid_layout);

  auto invalid_layout_range = valid_definition();
  invalid_layout_range.functions[1].instruction_count = 2;
  const auto range_result =
      ExecutableProgram::create(std::move(invalid_layout_range));
  ASSERT_FALSE(range_result);
  EXPECT_EQ(range_result.error().code, ProgramErrorCode::invalid_layout_range);

  auto invalid_dense_id = valid_definition();
  invalid_dense_id.functions[1].id = FunctionId{2};
  const auto dense_id_result =
      ExecutableProgram::create(std::move(invalid_dense_id));
  ASSERT_FALSE(dense_id_result);
  EXPECT_EQ(dense_id_result.error().code,
            ProgramErrorCode::function_id_not_dense);

  auto invalid_predicate = valid_definition();
  invalid_predicate.instructions[0].predicate = Predicate{RegisterSlot{0}};
  const auto predicate_result =
      ExecutableProgram::create(std::move(invalid_predicate));
  ASSERT_FALSE(predicate_result);
  EXPECT_EQ(predicate_result.error().code,
            ProgramErrorCode::operand_width_mismatch);

  auto invalid_operand = valid_definition();
  std::get<Move>(invalid_operand.instructions[0].operation).source =
      RegisterSlot{9};
  const auto operand_result =
      ExecutableProgram::create(std::move(invalid_operand));
  ASSERT_FALSE(operand_result);
  EXPECT_EQ(operand_result.error().code,
            ProgramErrorCode::operand_slot_out_of_range);

  auto invalid_type = valid_definition();
  std::get<Move>(invalid_type.instructions[0].operation).type = DataType::u32;
  const auto type_result = ExecutableProgram::create(std::move(invalid_type));
  ASSERT_FALSE(type_result);
  EXPECT_EQ(type_result.error().code,
            ProgramErrorCode::unsupported_instruction);

  auto invalid_immediate = valid_definition();
  std::get<Add>(invalid_immediate.instructions[1].operation).rhs =
      RawValue::b16(std::uint16_t{1});
  const auto immediate_result =
      ExecutableProgram::create(std::move(invalid_immediate));
  ASSERT_FALSE(immediate_result);
  EXPECT_EQ(immediate_result.error().code,
            ProgramErrorCode::immediate_width_mismatch);

  auto invalid_branch = valid_definition();
  std::get<Branch>(invalid_branch.instructions[2].operation).target =
      ProgramCounter{3};
  const auto branch_result =
      ExecutableProgram::create(std::move(invalid_branch));
  ASSERT_FALSE(branch_result);
  EXPECT_EQ(branch_result.error().code,
            ProgramErrorCode::branch_target_out_of_range);

  auto missing_fallthrough = valid_definition();
  missing_fallthrough.instructions[2].operation =
      Move{DataType::b32, RegisterSlot{1}, RegisterSlot{0}};
  const auto fallthrough_result =
      ExecutableProgram::create(std::move(missing_fallthrough));
  ASSERT_FALSE(fallthrough_result);
  EXPECT_EQ(fallthrough_result.error().code, ProgramErrorCode::no_fallthrough);
  EXPECT_EQ(fallthrough_result.error().function, FunctionId{0});
  EXPECT_EQ(fallthrough_result.error().pc, ProgramCounter{2});

  auto predicated_branch = valid_definition();
  predicated_branch.instructions[2].predicate = Predicate{RegisterSlot{2}};
  const auto predicated_branch_result =
      ExecutableProgram::create(std::move(predicated_branch));
  ASSERT_FALSE(predicated_branch_result);
  EXPECT_EQ(predicated_branch_result.error().code,
            ProgramErrorCode::no_fallthrough);

  auto predicated_exit = valid_definition();
  predicated_exit.functions[1].register_widths = {RawWidth::pred};
  predicated_exit.instructions[3].predicate = Predicate{RegisterSlot{0}};
  const auto predicated_exit_result =
      ExecutableProgram::create(std::move(predicated_exit));
  ASSERT_FALSE(predicated_exit_result);
  EXPECT_EQ(predicated_exit_result.error().code,
            ProgramErrorCode::no_fallthrough);
}

TEST(ExecutableProgram, PrintsCanonicalExecutableProgram) {
  const auto program = make_program();

  EXPECT_EQ(to_string(program),
            "@0  [func:0 pc:0]  mov.b32 reg:1, reg:0\n"
            "@1  [func:0 pc:1]  @!reg:2 add.u32 reg:1, reg:1, "
            "b32:0x00000001\n"
            "@2  [func:0 pc:2]  bra pc:0\n"
            "@3  [func:1 pc:0]  exit");
}

}  // namespace
}  // namespace ptxsim::exec_ir::test
