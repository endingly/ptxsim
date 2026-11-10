#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/exec_ir/exec_ir_types.hpp>
#include <ptxsim/exec_ir/instruction_catalogue.gen.hpp>
#include <ptxsim/exec_ir/model/arithmetic/add.gen.hpp>
#include <ptxsim/exec_ir/model/arithmetic/sub.gen.hpp>
#include <ptxsim/exec_ir/model/control_flow/bra.gen.hpp>
#include <ptxsim/exec_ir/model/control_flow/exit.gen.hpp>
#include <ptxsim/exec_ir/model/data_movement/mov.gen.hpp>
#include <ptxsim/exec_ir/model/data_movement/ld.gen.hpp>
#include <ptxsim/exec_ir/model/data_movement/st.gen.hpp>
#include <ptxsim/exec_ir/model/parallel_synchronization_and_communication/bar.gen.hpp>

namespace ptxsim::exec_ir::test {
namespace {
using common::CodeLocation;
using common::FunctionId;
using common::ProgramCounter;
using common::RawWidth;
static_assert(std::is_abstract_v<Instruction> == false);
static_assert(GeneratedForm<AddIntegerNoSat>);
static_assert(GeneratedForm<SubIntegerNoSat>);
static_assert(AddIntegerNoSat::opcode == Opcode::Add);
static_assert(std::is_final_v<AddIntegerNoSat>);

/** @brief Bind a test register's real slot and declared scalar type. */
auto reg(std::uint32_t slot, DataType type = DataType::b32) -> RegisterRef {
  return {.slot = common::RegisterSlot{slot},
          .register_class = type == DataType::pred ? RegisterClass::Predicate : RegisterClass::General,
          .declared_type = type};
}
/** @brief Build a complete ordinary body/declaration pair in dense order. */
void body(ProgramDefinition& input, std::uint32_t id, std::size_t begin,
          std::uint32_t count, std::vector<RawWidth> widths = {}) {
  input.function_declarations.push_back({.id = FunctionDeclarationId{id}, .function = FunctionId{id}});
  input.functions.push_back({.id = FunctionId{id}, .declarations = {FunctionDeclarationId{id}},
      .definition = FunctionDeclarationId{id}, .body = FunctionLayout{
          .id = FunctionId{id}, .begin = begin, .instruction_count = count,
          .register_widths = std::move(widths)}});
}
/** @brief Construct independent function-local bodies with a predicate and a branch. */
auto valid_definition() -> ProgramDefinition {
  ProgramDefinition input;
  input.instructions.push_back(std::make_unique<MovScalar>(
      MovScalar::ScalarOperands{reg(1), reg(0)}, MovScalar::Controls{DataType::b32}));
  input.instructions.push_back(std::make_unique<AddIntegerNoSat>(
      AddIntegerNoSat::Operands{reg(1, DataType::u32), ScalarOperand{reg(1, DataType::u32)},
          ScalarOperand{Immediate{1, DataType::u32}}}, AddIntegerNoSat::Controls{DataType::u32},
      Predicate{reg(2, DataType::pred), true}));
  input.instructions.push_back(std::make_unique<BraDirect>(
      BraDirect::Operands{BranchTarget{FunctionId{0}, ProgramCounter{0}}}));
  input.instructions.push_back(std::make_unique<ExitBare>(ExitBare::Operands{}));
  body(input, 0, 0, 3, {RawWidth::b32, RawWidth::b32, RawWidth::pred});
  body(input, 1, 3, 1);
  return input;
}
/** @brief Create a valid independently frozen test snapshot. */
auto make_program() -> ExecutableProgram {
  auto result = ExecutableProgram::create(valid_definition());
  EXPECT_TRUE(result.has_value());
  return std::move(*result);
}
/** @brief Add full parameter identities for a derived entry packing test. */
void parameter_records(ProgramDefinition& input) {
  input.functions[0].is_entry=true;
  input.function_declarations[0].is_entry=true;
  for (std::uint32_t id = 0; id < 3; ++id) {
    input.parameters.push_back({.id = ParameterId{id}, .owner_declaration = FunctionDeclarationId{0},
        .owner_function = FunctionId{0}, .role=ParameterDeclarationRole::EntryInput,
        .scalar_type=id==1?DataType::u64:DataType::u32, .alignment=id==1?8U:4U, .byte_extent=id==1?8U:4U});
    input.function_declarations[0].parameters.push_back(ParameterId{id});
  }
}

TEST(ExecutableProgram, FetchUsesFunctionLocalProgramCounters) {
  const auto program = make_program();
  const auto first_layout = program.function_layout(FunctionId{0});
  const auto first = program.fetch({FunctionId{0}, ProgramCounter{0}});
  const auto second = program.fetch({FunctionId{1}, ProgramCounter{0}});
  ASSERT_TRUE(first_layout); ASSERT_TRUE(first); ASSERT_TRUE(second);
  EXPECT_EQ(first_layout->get().id, FunctionId{0});
  EXPECT_EQ(first_layout->get().register_widths,
            (std::vector{RawWidth::b32, RawWidth::b32, RawWidth::pred}));
  EXPECT_NE(&first->get(), &second->get());
  EXPECT_NE(first->get().get_if<MovScalar>(), nullptr);
  EXPECT_NE(second->get().get_if<ExitBare>(), nullptr);
  EXPECT_EQ(first->get().instruction_opcode(), Opcode::Mov);
  EXPECT_EQ(second->get().instruction_opcode(), Opcode::Exit);
}
TEST(ExecutableProgram, DerivesFlatOffsetsAndSameFunctionFallthrough) {
  const auto program = make_program();
  EXPECT_EQ(*program.flat_offset({FunctionId{0}, ProgramCounter{2}}), 2U);
  EXPECT_EQ(*program.flat_offset({FunctionId{1}, ProgramCounter{0}}), 3U);
  EXPECT_EQ(*program.fallthrough({FunctionId{0}, ProgramCounter{1}}),
            (CodeLocation{FunctionId{0}, ProgramCounter{2}}));
  const auto final = program.fallthrough({FunctionId{1}, ProgramCounter{0}});
  ASSERT_FALSE(final); EXPECT_EQ(final.error().code, ProgramErrorCode::no_fallthrough);
}
TEST(ExecutableProgram, RejectsInvalidLocationsAndDefinitions) {
  const auto program = make_program();
  const auto missing_function = program.function_layout(FunctionId{2});
  ASSERT_FALSE(missing_function);
  EXPECT_EQ(missing_function.error().code, ProgramErrorCode::function_not_found);
  const auto missing_location_function = program.fetch({FunctionId{2}, ProgramCounter{0}});
  ASSERT_FALSE(missing_location_function);
  EXPECT_EQ(missing_location_function.error().code, ProgramErrorCode::function_not_found);
  const auto invalid_pc = program.fetch({FunctionId{1}, ProgramCounter{1}});
  ASSERT_FALSE(invalid_pc);
  EXPECT_EQ(invalid_pc.error().code, ProgramErrorCode::pc_out_of_range);
  auto input = valid_definition();
  input.functions[1].body->begin = 2;
  const auto overlapping = ExecutableProgram::create(input);
  ASSERT_FALSE(overlapping);
  EXPECT_EQ(overlapping.error().code, ProgramErrorCode::invalid_layout);
  input = valid_definition(); input.functions[1].body->instruction_count = 2;
  const auto outside_instructions = ExecutableProgram::create(input);
  ASSERT_FALSE(outside_instructions);
  EXPECT_EQ(outside_instructions.error().code, ProgramErrorCode::invalid_layout_range);
  input = valid_definition(); input.functions[1].id = FunctionId{2};
  const auto nondense = ExecutableProgram::create(input);
  ASSERT_FALSE(nondense);
  EXPECT_EQ(nondense.error().code, ProgramErrorCode::function_id_not_dense);
  input = valid_definition(); input.functions[0].body->register_widths[0] = static_cast<RawWidth>(99);
  const auto invalid = ExecutableProgram::create(input);
  ASSERT_FALSE(invalid); EXPECT_EQ(invalid.error().code, ProgramErrorCode::invalid_register_width);
  EXPECT_EQ(invalid.error().actual, static_cast<RawWidth>(99));
}
TEST(ExecutableProgram, ValidatesExactSourceOrderedEntryParameterLayouts) {
  auto input = valid_definition(); parameter_records(input);
  auto& layout = *input.functions[0].body;
  layout.entry_parameter_size = 20;
  layout.entry_parameters = {{0, 4, 4, ParameterId{0}}, {8, 8, 8, ParameterId{1}}, {16, 4, 4, ParameterId{2}}};
  const auto expected = layout.entry_parameters;
  const auto program = ExecutableProgram::create(input);
  ASSERT_TRUE(program); const auto actual = program->function_layout(FunctionId{0});
  ASSERT_TRUE(actual); EXPECT_EQ(actual->get().entry_parameter_size, 20U);
  EXPECT_EQ(actual->get().entry_parameters, expected);
}
TEST(ExecutableProgram, RejectsMalformedEntryParameterLayouts) {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    auto input = valid_definition(); parameter_records(input);
    auto& layout = *input.functions[0].body;
    layout.entry_parameter_size = 4;
    if (scenario == 1) layout.entry_parameters = {{0, 4, 3, ParameterId{0}}};
    if (scenario == 2) { layout.entry_parameter_size = 12; layout.entry_parameters = {{0,4,4,ParameterId{0}},{4,8,8,ParameterId{1}}}; }
    if (scenario == 3) { layout.entry_parameter_size = 8; layout.entry_parameters = {{0,4,4,ParameterId{0}}}; }
    if (scenario == 4) {
      const auto max = std::numeric_limits<std::size_t>::max();
      layout.entry_parameter_size = max;
      layout.entry_parameters = {{0,max,1,ParameterId{0}},{max,1,1,ParameterId{1}}};
    }
    const auto result = ExecutableProgram::create(input);
    ASSERT_FALSE(result); EXPECT_EQ(result.error().code, ProgramErrorCode::invalid_entry_parameter_layout);
  }
}
TEST(ExecutableProgram, PrintsCanonicalExecutableProgram) {
  const auto text = to_string(make_program());
  EXPECT_EQ(text, R"expected(function:0:
  pc:0: mov/mov_scalar type=b32 layout=scalar dst=register{slot=register:1, class=General, type=b32, width=none, component=none} src=register{slot=register:0, class=General, type=b32, width=none, component=none}
  pc:1: add/add_integer_no_sat @predicate{register=register{slot=register:2, class=Predicate, type=pred, width=none, component=none}, negated=true} type=u32 layout=default dst=register{slot=register:1, class=General, type=u32, width=none, component=none} src1=register{slot=register:1, class=General, type=u32, width=none, component=none} src2=immediate{bits=0x1, type=u32, negative=false, integer-source=none}
  pc:2: bra/bra_direct uni=false layout=default target=branch-target{function=function:0, pc=pc:0}
function:1:
  pc:0: exit/exit_bare layout=default
)expected");
}
TEST(ExecutableProgram, PrintsScalarLoadStore) {
  ProgramDefinition input;
  input.instructions.push_back(std::make_unique<LdGenericScalar>(
      LdGenericScalar::Operands{reg(0, DataType::u32), Address{reg(1,DataType::u64)}},
      LdGenericScalar::Controls{.type=DataType::u32}));
  input.instructions.push_back(std::make_unique<StExplicitScalar>(
      StExplicitScalar::Operands{Address{reg(1,DataType::u64)},reg(0,DataType::u32)},
      StExplicitScalar::Controls{.state_space=AddressSpace::global,.type=DataType::u32}));
  input.instructions.push_back(std::make_unique<ExitBare>(ExitBare::Operands{}));
  body(input,0,0,3,{RawWidth::b32,RawWidth::b64});
  const auto program=ExecutableProgram::create(input); ASSERT_TRUE(program);
  const auto text=to_string(*program);
  EXPECT_EQ(text, R"expected(function:0:
  pc:0: ld/ld_generic_scalar mmio=false semantics=omitted scope=none cache=unspecified type=u32 layout=default dst=register{slot=register:0, class=General, type=u32, width=none, component=none} address=address{base=register{slot=register:1, class=General, type=u64, width=none, component=none}, offset=none, enclosing=Unknown, parameter=Default, unified=false, named-index=none}
  pc:1: st/st_explicit_scalar mmio=false semantics=omitted scope=none state_space=global cache=unspecified type=u32 layout=default address=address{base=register{slot=register:1, class=General, type=u64, width=none, component=none}, offset=none, enclosing=Unknown, parameter=Default, unified=false, named-index=none} src=register{slot=register:0, class=General, type=u32, width=none, component=none}
  pc:2: exit/exit_bare layout=default
)expected");
}
TEST(ExecutableProgram, PrintsWarpSync) {
  ProgramDefinition input;
  input.instructions.push_back(std::make_unique<BarWarpSync>(
      BarWarpSync::Operands{ScalarOperand{Immediate{3,DataType::b32}}}));
  input.instructions.push_back(std::make_unique<ExitBare>(ExitBare::Operands{}));
  body(input,0,0,2);
  const auto program=ExecutableProgram::create(input); ASSERT_TRUE(program);
  const auto instruction=program->fetch({FunctionId{0},ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  EXPECT_EQ(instruction->get().instruction_opcode(),Opcode::Bar);
  EXPECT_EQ(to_string(*program), R"expected(function:0:
  pc:0: bar/bar_warp_sync warp=true sync=true layout=default membermask=immediate{bits=0x3, type=b32, negative=false, integer-source=none}
  pc:1: exit/exit_bare layout=default
)expected");
}
TEST(ExecutableProgram, AcceptsAndPrintsDeclarationOnlyOpcode) {
  ProgramDefinition input;
  input.instructions.push_back(std::make_unique<SubIntegerNoSat>(
      SubIntegerNoSat::Operands{reg(0,DataType::u32),ScalarOperand{reg(1,DataType::u32)},ScalarOperand{reg(2,DataType::u32)}},
      SubIntegerNoSat::Controls{DataType::u32},Predicate{reg(3,DataType::pred)}));
  body(input,0,0,1,{RawWidth::b32,RawWidth::b32,RawWidth::b32,RawWidth::pred});
  const auto program=ExecutableProgram::create(input); ASSERT_TRUE(program);
  const auto instruction=program->fetch({FunctionId{0},ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  EXPECT_EQ(instruction->get().instruction_opcode(),Opcode::Sub);
  EXPECT_EQ(to_string(*program), R"expected(function:0:
  pc:0: sub/sub_integer_no_sat @predicate{register=register{slot=register:3, class=Predicate, type=pred, width=none, component=none}, negated=false} type=u32 layout=default dst=register{slot=register:0, class=General, type=u32, width=none, component=none} src1=register{slot=register:1, class=General, type=u32, width=none, component=none} src2=register{slot=register:2, class=General, type=u32, width=none, component=none}
)expected");
}
TEST(InstructionDiagnostic, FormatsDeclarationOnlyOpcodeWithoutValidation) {
  const SubIntegerNoSat form{
      SubIntegerNoSat::Operands{reg(0,DataType::u32),ScalarOperand{reg(1,DataType::u32)},ScalarOperand{Immediate{1,DataType::u32}}},
      SubIntegerNoSat::Controls{DataType::u32}};
  const auto text=to_string(form);
  EXPECT_EQ(text, R"expected(sub/sub_integer_no_sat type=u32 layout=default dst=register{slot=register:0, class=General, type=u32, width=none, component=none} src1=register{slot=register:1, class=General, type=u32, width=none, component=none} src2=immediate{bits=0x1, type=u32, negative=false, integer-source=none})expected");
  const BarWarpSync barrier{BarWarpSync::Operands{ScalarOperand{Immediate{3,DataType::b32}}}};
  EXPECT_EQ(to_string(barrier), R"expected(bar/bar_warp_sync warp=true sync=true layout=default membermask=immediate{bits=0x3, type=b32, negative=false, integer-source=none})expected");
}
TEST(InstructionDiagnostic, FormatsStableModifierAliases) {
  EXPECT_NE(to_string(Predicate{reg(3,DataType::pred),false}).find("negated=false"),std::string::npos);
  EXPECT_NE(to_string(Predicate{reg(3,DataType::pred),true}).find("negated=true"),std::string::npos);
  EXPECT_EQ(to_string(BooleanOperator::and_),"and");
  EXPECT_EQ(to_string(MemoryConsistency::volatile_),"volatile");
  EXPECT_EQ(to_string(AddressSpace::const_),"const");
  EXPECT_EQ(to_string(AddressSpace::param_entry),"param::entry");
  EXPECT_EQ(to_string(AddressSpace::param_func),"param::func");
  EXPECT_EQ(to_string(MbarrierPhaseType::phase_type_conditional),"phase_type::conditional");
  EXPECT_EQ(to_string(MbarrierPhaseType::phase_type_primary),"phase_type::primary");
  EXPECT_EQ(to_string(MbarrierLayout::layout_v0),"layout::v0");
  EXPECT_EQ(to_string(MbarrierLayout::layout_v1),"layout::v1");
  EXPECT_EQ(to_string(AsyncProxyKind::async_shared_cta),"async.shared::cta");
  EXPECT_EQ(to_string(ProxyKindPair::async_generic),"async::generic");
  EXPECT_EQ(to_string(static_cast<DataType>(65535)),"<invalid>");
}
}  // namespace
}  // namespace ptxsim::exec_ir::test
