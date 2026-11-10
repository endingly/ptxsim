#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <stdexcept>
#include <ptxsim/exec_ir/exec_ir.gen.hpp>
#include <ptxsim/exec_ir/exec_ir_types.hpp>
#include <cstdint>
#include <string>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

namespace ptxsim::exec_ir_lowering::test {
namespace {

namespace source = ptx_frontend::resolved_ir;
using ptx_frontend::resolved_ir::ResolvedModule;

/** @brief Query a sealed native exact form with an assertion before dereferencing. */
template <exec_ir::GeneratedForm Form>
auto native_form(const exec_ir::Instruction& value) -> const Form& {
  const auto* form=value.get_if<Form>();
  EXPECT_NE(form,nullptr);
  if (!form) throw std::runtime_error("unexpected native form");
  return *form;
}
/** @brief Access a mutable exact frontend fixture after validating its real dynamic class. */
template <typename Form> requires std::derived_from<Form,source::Instruction>
auto source_form(std::unique_ptr<source::Instruction>& value) -> Form& {
  auto* form=dynamic_cast<Form*>(value.get());
  EXPECT_NE(form,nullptr);
  if (!form) throw std::runtime_error("unexpected source form");
  return *form;
}
/** @brief Access an exact frontend fixture without exposing mutable test references. */
template <typename Form> requires std::derived_from<Form,source::Instruction>
auto source_form(const std::unique_ptr<source::Instruction>& value) -> const Form& {
  const auto* form=dynamic_cast<const Form*>(value.get());
  EXPECT_NE(form,nullptr);
  if (!form) throw std::runtime_error("unexpected source form");
  return *form;
}
/** @brief Bind one ordinary register test view with its actual native type and class. */
auto reg(std::uint32_t id,exec_ir::DataType type=exec_ir::DataType::b32) -> exec_ir::RegisterRef {
  return {.slot=common::RegisterSlot{id},
          .register_class=type==exec_ir::DataType::pred?exec_ir::RegisterClass::Predicate:exec_ir::RegisterClass::General,
          .declared_type=type};
}
/** @brief Parse and resolve a test module before introducing malformed metadata. */
auto resolve(std::string_view source) -> ResolvedModule {
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast) {
    ADD_FAILURE() << "PTX parse failed";
    return {};
  }
  auto module = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!module) {
    for(const auto& diagnostic:module.error()) ADD_FAILURE() << diagnostic.message;
    return {};
  }
  return std::move(*module);
}

auto lowered_after_frontend_dies()
    -> std::expected<exec_ir::ExecutableProgram, LoweringError> {
  return lower(resolve(R"ptx(
.entry first() {
  .reg .pred %p;
  .reg .b32 %b<2>;
  .reg .u32 %r<3>;
start:
  @!%p mov.b32 %b1, %b0;
  @%p add.u32 %r2, %r0, 7;
  bra done;
  add.u32 %r0, %r0, %r1;
  {
    .reg .u32 %nested;
    add.u32 %nested, %r0, %r1;
  }
done:
  exit;
}
.entry second() {
  .reg .b32 %x<2>;
  mov.b32 %x1, %x0;
  exit;
}
)ptx"));
}

/** @brief Lower entry ABI metadata after parser, AST, and module temporaries die. */
auto lowered_entry_parameters_after_frontend_dies()
    -> std::expected<exec_ir::ExecutableProgram, LoweringError> {
  return lower(resolve(R"ptx(
.entry kernel(.param .align 16 .b8 bytes[16], .param .u64 address,
              .param .u32 count) {
  exit;
}
)ptx"));
}
TEST(ExecIrLowering, PreservesMemoryVectorSinksAndSubtractedOffsets) {
  ptx_frontend::PtxSyntaxParser legacy_parser(R"ptx(
.entry kernel() {
  .reg .b32 %r;
  .reg .b64 %a;
  ld.global.v8.b32 {%r, _, _, _, _, _, _, _}, [%a-32];
  ld.global.u32 %r, [-4];
  exit;
}
)ptx");
  const auto legacy_ast=legacy_parser.parseModule(); ASSERT_TRUE(legacy_ast);
  const auto legacy_source=ptx_frontend::resolved_ir::resolveModule(*legacy_ast);
  ASSERT_FALSE(legacy_source);
  EXPECT_TRUE(std::ranges::any_of(legacy_source.error(),[](const auto& error) {
    return error.message.find("must be an unsigned 32-bit value")!=std::string::npos;
  }));
  const auto program=lower(resolve(R"ptx(
.entry kernel() {
  .reg .b32 %r;
  .reg .b64 %a;
  ld.global.v8.b32 {%r, _, _, _, _, _, _, _}, [%a-32];
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto instruction=program->fetch({common::FunctionId{0},common::ProgramCounter{0}}); ASSERT_TRUE(instruction);
  const auto& form=native_form<exec_ir::LdExplicitVector>(instruction->get());
  EXPECT_EQ(form.vector,exec_ir::VectorArity::v8); EXPECT_EQ(form.type,exec_ir::DataType::b32);
  ASSERT_EQ(form.operands.dst.elements.size(),8);
  ASSERT_TRUE(form.operands.dst.elements[0]); EXPECT_EQ(form.operands.dst.elements[0]->slot,common::RegisterSlot{0});
  for(std::size_t index=1;index<8;++index) EXPECT_FALSE(form.operands.dst.elements[index]);
  EXPECT_EQ(form.operands.address.offset,(std::optional{exec_ir::AddressOffset{
      exec_ir::AddressOffsetOperator::Subtract,exec_ir::Immediate{32,exec_ir::DataType::s64,false,32}}}));
}

}  // namespace

TEST(ExecIrLowering, LowersBoundProgramsWithoutFrontendLifetime) {
  auto program_result = lowered_after_frontend_dies();
  ASSERT_TRUE(program_result);
  const auto& program = *program_result;

  EXPECT_EQ(exec_ir::to_string(program),
            R"expected(function:0:
  pc:0: mov/mov_scalar @predicate{register=register{slot=register:0, class=Predicate, type=pred, width=none, component=none}, negated=true} type=b32 layout=scalar dst=register{slot=register:2, class=General, type=b32, width=none, component=none} src=register{slot=register:1, class=General, type=b32, width=none, component=none}
  pc:1: add/add_integer_no_sat @predicate{register=register{slot=register:0, class=Predicate, type=pred, width=none, component=none}, negated=false} type=u32 layout=default dst=register{slot=register:5, class=General, type=u32, width=none, component=none} src1=register{slot=register:3, class=General, type=u32, width=none, component=none} src2=immediate{bits=0x7, type=u32, negative=false, integer-source=0x7}
  pc:2: bra/bra_direct uni=false layout=default target=branch-target{function=function:0, pc=pc:5}
  pc:3: add/add_integer_no_sat type=u32 layout=default dst=register{slot=register:3, class=General, type=u32, width=none, component=none} src1=register{slot=register:3, class=General, type=u32, width=none, component=none} src2=register{slot=register:4, class=General, type=u32, width=none, component=none}
  pc:4: add/add_integer_no_sat type=u32 layout=default dst=register{slot=register:6, class=General, type=u32, width=none, component=none} src1=register{slot=register:3, class=General, type=u32, width=none, component=none} src2=register{slot=register:4, class=General, type=u32, width=none, component=none}
  pc:5: exit/exit_bare layout=default
function:1:
  pc:0: mov/mov_scalar type=b32 layout=scalar dst=register{slot=register:1, class=General, type=b32, width=none, component=none} src=register{slot=register:0, class=General, type=b32, width=none, component=none}
  pc:1: exit/exit_bare layout=default
)expected");
  EXPECT_TRUE(
      program.fetch({common::FunctionId{0}, common::ProgramCounter{0}}));
  EXPECT_TRUE(
      program.fetch({common::FunctionId{1}, common::ProgramCounter{0}}));
}

TEST(ExecIrLowering, BindsScalarNamesEndingInDigitsBySymbolIdentity) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .b32 %r0;
  mov.b32 %r0, %r0;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  EXPECT_EQ(exec_ir::to_string(*program),
            R"expected(function:0:
  pc:0: mov/mov_scalar type=b32 layout=scalar dst=register{slot=register:0, class=General, type=b32, width=none, component=none} src=register{slot=register:0, class=General, type=b32, width=none, component=none}
  pc:1: exit/exit_bare layout=default
)expected");
}

TEST(ExecIrLowering, BindsThreadIdXAsTheCanonicalSpecialRegister) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %tid.x;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto instruction =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& form = native_form<exec_ir::MovScalar>(instruction->get());
  const auto& operands =
      std::get<exec_ir::MovScalar::ScalarOperands>(form.operands);
  EXPECT_EQ(std::get<exec_ir::SpecialRegisterRef>(operands.src),
            (exec_ir::SpecialRegisterRef{
                .id = exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::Tid,0},
                .component = 0U,
                .element_type=exec_ir::DataType::u32, .vector_width=4,
            }));

  const auto lowered_component = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %tid.y;
  exit;
}
)ptx"));
  ASSERT_TRUE(lowered_component);

  const auto lowered_identity = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %ntid.x;
  exit;
}
)ptx"));
  ASSERT_TRUE(lowered_identity);
}

TEST(ExecIrLowering, BindsB64MoveImmediate) {
  const auto module = resolve(R"ptx(
.entry kernel() {
  .reg .b64 %addr;
  mov.b64 %addr, 0;
  exit;
}
)ptx");
  const auto& resolved_form=source_form<source::MovScalar>(module.functions[0].body[0]);
  const auto& resolved_operands=*resolved_form.src_mov_source;
  const auto& immediate =
      std::get<ptx_frontend::resolved_ir::ResolvedImmediate>(
          resolved_operands.value);
  EXPECT_EQ(immediate.type, ptx_frontend::base::ScalarType::B64);
  EXPECT_EQ(immediate.bits, 0U);
  EXPECT_FALSE(immediate.is_negative);

  const auto program = lower(module);
  ASSERT_TRUE(program);
  const auto instruction =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& form = native_form<exec_ir::MovScalar>(instruction->get());
  const auto& operands =
      std::get<exec_ir::MovScalar::ScalarOperands>(form.operands);
  EXPECT_EQ(form.type, exec_ir::DataType::b64);
  EXPECT_EQ(operands.dst, reg(0,exec_ir::DataType::b64));
  EXPECT_EQ(*exec_ir::immediate_value(std::get<exec_ir::Immediate>(operands.src)),
            common::RawValue::b64(std::uint64_t{0}));
}

TEST(ExecIrLowering, RejectsScalarImmediateBitsOutsideResolvedWidth) {
  auto module = resolve(R"ptx(
.entry kernel() {
  .reg .u16 %r;
  add.u16 %r, %r, 1;
  exit;
}
)ptx");
  auto& form=source_form<source::AddIntegerNoSat>(module.functions[0].body[0]);
  auto& immediate =
      std::get<ptx_frontend::resolved_ir::ResolvedImmediate>(form.src2.value);
  immediate.bits = 0x10000U;
  const auto program = lower(module);
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code, LoweringErrorCode::malformed_resolved_ir);
}

TEST(ExecIrLowering, LowersEntryParametersWithCanonicalOffsets) {
  const auto program = lower(resolve(R"ptx(
.entry kernel(.param .u32 first, .param .u64 second, .param .u32 third) {
  .reg .u32 %r;
  .reg .u64 %address;
  ld.param.u32 %r, [first];
  ld.param.u64 %address, [second];
  ld.param.u32 %r, [third-4];
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->get().entry_parameter_size, 20U);
  EXPECT_EQ(layout->get().entry_parameters,
            (std::vector<exec_ir::EntryParameterLayout>{
                {.offset = 0U, .size = 4U, .alignment = 4U, .parameter=exec_ir::ParameterId{0}},
                {.offset = 8U, .size = 8U, .alignment = 8U, .parameter=exec_ir::ParameterId{1}},
                {.offset = 16U, .size = 4U, .alignment = 4U, .parameter=exec_ir::ParameterId{2}},
            }));

  const auto first =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  const auto second =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{1}});
  const auto third =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{2}});
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(third);
  const auto& first_form = native_form<exec_ir::LdExplicitScalar>(first->get());
  const auto& second_form = native_form<exec_ir::LdExplicitScalar>(second->get());
  const auto& third_form = native_form<exec_ir::LdExplicitScalar>(third->get());
  EXPECT_EQ(std::get<exec_ir::ParameterId>(std::get<exec_ir::SymbolRef>(first_form.operands.address.base).target),exec_ir::ParameterId{0});
  EXPECT_EQ(std::get<exec_ir::ParameterId>(std::get<exec_ir::SymbolRef>(second_form.operands.address.base).target),exec_ir::ParameterId{1});
  EXPECT_EQ(std::get<exec_ir::ParameterId>(std::get<exec_ir::SymbolRef>(third_form.operands.address.base).target),exec_ir::ParameterId{2});
  EXPECT_EQ(
      third_form.operands.address.offset,
      (exec_ir::AddressOffset{exec_ir::AddressOffsetOperator::Subtract, exec_ir::Immediate{4,exec_ir::DataType::s64,false,4}}));
}

TEST(ExecIrLowering, UsesDeclarationAlignmentNotPointerTargetAlignment) {
  const auto program = lower(resolve(R"ptx(
.entry gemm(
    .param .align 16 .u64 .ptr .global .align 32 A,
    .param .u64 .ptr .global .align 16 B,
    .param .u64 .ptr .global .align 16 C,
    .param .u32 M,
    .param .u32 N,
    .param .u32 K,
    .param .u32 lda,
    .param .u32 ldb,
    .param .u32 ldc) {
  .reg .u32 %r;
  .reg .u64 %address;
  ld.param.u64 %address, [A];
  ld.param.u32 %r, [M];
  ld.param.u32 %r, [ldc-4];
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->get().entry_parameter_size, 48U);
  EXPECT_EQ(layout->get().entry_parameters[0],
            (exec_ir::EntryParameterLayout{
                .offset = 0U, .size = 8U, .alignment = 16U, .parameter=exec_ir::ParameterId{0}}));
  EXPECT_EQ(layout->get().entry_parameters[1],
            (exec_ir::EntryParameterLayout{
                .offset = 8U, .size = 8U, .alignment = 8U, .parameter=exec_ir::ParameterId{1}}));
  EXPECT_EQ(layout->get().entry_parameters[3].offset, 24U);
  EXPECT_EQ(layout->get().entry_parameters[8].offset, 44U);

  const auto address =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{2}});
  ASSERT_TRUE(address);
  const auto& form = native_form<exec_ir::LdExplicitScalar>(address->get());
  EXPECT_EQ(std::get<exec_ir::ParameterId>(std::get<exec_ir::SymbolRef>(form.operands.address.base).target),exec_ir::ParameterId{8});
  EXPECT_EQ(
      form.operands.address.offset,
      (exec_ir::AddressOffset{exec_ir::AddressOffsetOperator::Subtract, exec_ir::Immediate{4,exec_ir::DataType::s64,false,4}}));
}

TEST(ExecIrLowering, LowersOverAlignedByteArraysWithoutTrailingPadding) {
  const auto program = lower(resolve(R"ptx(
.entry kernel(.param .align 16 .b8 bytes[16], .param .u32 count) {
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->get().entry_parameter_size, 20U);
  EXPECT_EQ(layout->get().entry_parameters,
            (std::vector<exec_ir::EntryParameterLayout>{
                {.offset = 0U, .size = 16U, .alignment = 16U, .parameter=exec_ir::ParameterId{0}},
                {.offset = 16U, .size = 4U, .alignment = 4U, .parameter=exec_ir::ParameterId{1}},
            }));
}

TEST(ExecIrLowering, UsesPhysicalByteWidthsForTwoByteDeclarations) {
  const auto program = lower(resolve(R"ptx(
.entry kernel(.param .align 2 .b16 first,
              .param .align 2 .b16 second,
              .param .u32 count) {
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->get().entry_parameter_size, 8U);
  EXPECT_EQ(layout->get().entry_parameters,
            (std::vector<exec_ir::EntryParameterLayout>{
                {.offset = 0U, .size = 2U, .alignment = 2U, .parameter=exec_ir::ParameterId{0}},
                {.offset = 2U, .size = 2U, .alignment = 2U, .parameter=exec_ir::ParameterId{1}},
                {.offset = 4U, .size = 4U, .alignment = 4U, .parameter=exec_ir::ParameterId{2}},
            }));
}

TEST(ExecIrLowering, RetainsEntryLayoutsWithoutFrontendLifetime) {
  const auto program = lowered_entry_parameters_after_frontend_dies();
  ASSERT_TRUE(program);
  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->get().entry_parameter_size, 28U);
  EXPECT_EQ(layout->get().entry_parameters,
            (std::vector<exec_ir::EntryParameterLayout>{
                {.offset = 0U, .size = 16U, .alignment = 16U, .parameter=exec_ir::ParameterId{0}},
                {.offset = 16U, .size = 8U, .alignment = 8U, .parameter=exec_ir::ParameterId{1}},
                {.offset = 24U, .size = 4U, .alignment = 4U, .parameter=exec_ir::ParameterId{2}},
            }));
}

TEST(ExecIrLowering, RejectsMalformedEntryParameterMetadata) {
  const auto source = R"ptx(
.entry first(.param .u32 input, .param .align 16 .b8 bytes[16]) { exit; }
.entry second(.param .u32 input) { exit; }
)ptx";
  const auto expect_invalid = [](ResolvedModule module,std::string_view mutation) {
    SCOPED_TRACE(mutation);
    const auto result = lower(module);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              LoweringErrorCode::malformed_resolved_ir);
  };

  auto duplicate = resolve(source);
  duplicate.functions[0].parameter_declarations.push_back(
      duplicate.functions[0].parameter_declarations.front());
  expect_invalid(std::move(duplicate),"duplicate");

  auto foreign = resolve(source);
  foreign.functions[0].parameter_declarations.front().symbol_id =
      foreign.functions[1].parameter_declarations.front().symbol_id;
  expect_invalid(std::move(foreign),"foreign");

  auto missing = resolve(source);
  missing.functions[0].parameter_declarations.erase(
      missing.functions[0].parameter_declarations.begin());
  expect_invalid(std::move(missing),"missing");

  auto missing_extent = resolve(source);
  missing_extent.functions[0]
      .parameter_declarations.front()
      .byte_extent.reset();
  expect_invalid(std::move(missing_extent),"missing_extent");

  auto non_power_two_alignment = resolve(source);
  non_power_two_alignment.functions[0]
      .parameter_declarations.front()
      .alignment = 3U;
  expect_invalid(std::move(non_power_two_alignment),"non_power_two_alignment");

  auto mismatched_symbol_alignment = resolve(source);
  auto& alignment_symbol = const_cast<ptx_frontend::binding::Symbol&>(
      mismatched_symbol_alignment.symbols.symbol(
          mismatched_symbol_alignment.functions[0]
              .parameter_declarations.front()
              .symbol_id));
  alignment_symbol.address_alignment = 8U;
  expect_invalid(std::move(mismatched_symbol_alignment),"mismatched_symbol_alignment");

  auto invalid_role = resolve(source);
  invalid_role.functions[0].parameter_declarations.front().role =
      static_cast<ptx_frontend::resolved_ir::ParameterDeclarationRole>(255U);
  expect_invalid(std::move(invalid_role),"invalid_role");

  auto device_role_on_entry = resolve(source);
  device_role_on_entry.functions[0].parameter_declarations.front().role =
      ptx_frontend::resolved_ir::ParameterDeclarationRole::DeviceInput;
  expect_invalid(std::move(device_role_on_entry),"device_role_on_entry");

  auto entry_role_on_device = resolve(R"ptx(
.func helper(.param .u32 input) { exit; }
)ptx");
  entry_role_on_device.functions[0].parameter_declarations.front().role =
      ptx_frontend::resolved_ir::ParameterDeclarationRole::EntryInput;
  expect_invalid(std::move(entry_role_on_device),"entry_role_on_device");

  auto reordered_unsized_input = resolve(R"ptx(
.version 9.3
.target sm_100
.func helper(.param .u32 first, .param .b8 bytes[]) { exit; }
)ptx");
  std::swap(reordered_unsized_input.functions[0].parameter_declarations[0],
            reordered_unsized_input.functions[0].parameter_declarations[1]);
  expect_invalid(std::move(reordered_unsized_input),"reordered_unsized_input");

  auto invalid_scope = resolve(source);
  invalid_scope.functions[0].parameter_declarations.front().scope_id =
      invalid_scope.symbols.moduleScope();
  expect_invalid(std::move(invalid_scope),"invalid_scope");

  auto invalid_size = resolve(source);
  invalid_size.functions[0].parameter_declarations.front().byte_extent = 8U;
  expect_invalid(std::move(invalid_size),"invalid_size");

  auto vector_parameter = resolve(source);
  vector_parameter.functions[0].parameter_declarations.front().vector_width =
      2U;
  expect_invalid(std::move(vector_parameter),"vector_parameter");

  auto multi_dimensional = resolve(source);
  multi_dimensional.functions[0]
      .parameter_declarations[1]
      .array_extents.push_back(1U);
  expect_invalid(std::move(multi_dimensional),"multi_dimensional");

  // Deliberately corrupt non-const module storage through the frontend's
  // read-only symbol view to exercise unsupported entry declaration shapes.
  auto vector_symbol_metadata = resolve(source);
  auto& vector_symbol = const_cast<ptx_frontend::binding::Symbol&>(
      vector_symbol_metadata.symbols.symbol(vector_symbol_metadata.functions[0]
                                                .parameter_declarations.front()
                                                .symbol_id));
  vector_symbol.vector_width = 2U;
  expect_invalid(std::move(vector_symbol_metadata),"vector_symbol_metadata");

  auto parameter_group = resolve(source);
  auto& group_symbol = const_cast<
      ptx_frontend::binding::Symbol&>(parameter_group.symbols.symbol(
      parameter_group.functions[0].parameter_declarations.front().symbol_id));
  group_symbol.parameterized_count = 2U;
  expect_invalid(std::move(parameter_group),"parameter_group");

  auto zero_extent = resolve(source);
  zero_extent.functions[0].parameter_declarations[1].array_extents[0] = 0U;
  expect_invalid(std::move(zero_extent),"zero_extent");

  auto cumulative_overflow = resolve(source);
  cumulative_overflow.functions[0].parameter_declarations[1].array_extents[0] =
      std::numeric_limits<std::uint64_t>::max();
  cumulative_overflow.functions[0].parameter_declarations[1].byte_extent =
      std::numeric_limits<std::uint64_t>::max();
  expect_invalid(std::move(cumulative_overflow),"cumulative_overflow");

  auto multiplication_overflow = resolve(R"ptx(
.entry kernel(.param .b16 values[2]) { exit; }
)ptx");
  multiplication_overflow.functions[0]
      .parameter_declarations[0]
      .array_extents[0] = std::numeric_limits<std::uint64_t>::max();
  multiplication_overflow.functions[0].parameter_declarations[0].byte_extent =
      std::numeric_limits<std::uint64_t>::max();
  expect_invalid(std::move(multiplication_overflow),"multiplication_overflow");

  auto unsized = resolve(source);
  unsized.functions[0].parameter_declarations[1].array_extents[0] =
      std::nullopt;
  unsized.functions[0].parameter_declarations[1].byte_extent.reset();
  expect_invalid(std::move(unsized),"unsized");
}

TEST(ExecIrLowering, RejectsEmptyEntryWithoutAnOwnedScope) {
  auto module = resolve(R"ptx(
.entry kernel() {}
)ptx");
  auto& symbol = const_cast<ptx_frontend::binding::Symbol&>(
      module.symbols.symbol(module.functions[0].symbol_id));
  symbol.owned_scope.reset();
  const auto program = lower(module);
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code, LoweringErrorCode::malformed_resolved_ir);
}

TEST(ExecIrLowering, RetainsPrototypeAndDefinitionParameterScopes) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_100
.func (.param .b16 result) helper(
    .param .u32 input, .param .align 8 .b8 bytes[]);
.func (.param .b16 result) helper(
    .param .u32 input, .param .align 8 .b8 bytes[]) {
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  ASSERT_EQ(program->functions().size(),1);
  ASSERT_EQ(program->function_declarations().size(),2);
  const auto& canonical=program->functions()[0];
  ASSERT_TRUE(canonical.body); ASSERT_TRUE(canonical.definition);
  EXPECT_EQ(*canonical.definition,exec_ir::FunctionDeclarationId{1});
  const auto& prototype=program->function_declarations()[0];
  const auto& definition=program->function_declarations()[1];
  EXPECT_TRUE(prototype.is_prototype); EXPECT_FALSE(definition.is_prototype);
  ASSERT_EQ(prototype.parameters.size(),3); ASSERT_EQ(definition.parameters.size(),3);
  for(std::size_t index=0;index<3;++index) {
    EXPECT_NE(prototype.parameters[index],definition.parameters[index]);
    EXPECT_EQ(program->parameters()[prototype.parameters[index].value()].owner_declaration,prototype.id);
    EXPECT_EQ(program->parameters()[definition.parameters[index].value()].owner_declaration,definition.id);
  }
  EXPECT_EQ(canonical.body->entry_parameter_size,0);
  EXPECT_TRUE(canonical.body->entry_parameters.empty());
}

TEST(ExecIrLowering, IgnoresValidNonEntryParameterRolesInLaunchLayouts) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_100
.entry kernel(.param .u32 input) {
  .param .align 8 .b8 staging[4];
  exit;
}
.func (.param .u16 result) helper(.param .u32 input) {
  .param .u16 local[2][3];
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto entry_layout = program->function_layout(common::FunctionId{0});
  const auto device_layout = program->function_layout(common::FunctionId{1});
  ASSERT_TRUE(entry_layout);
  ASSERT_TRUE(device_layout);
  EXPECT_EQ(entry_layout->get().entry_parameter_size, 4U);
  EXPECT_EQ(entry_layout->get().entry_parameters,
            (std::vector<exec_ir::EntryParameterLayout>{
                {.offset = 0U, .size = 4U, .alignment = 4U, .parameter=exec_ir::ParameterId{0}}}));
  EXPECT_EQ(device_layout->get().entry_parameter_size, 0U);
  EXPECT_TRUE(device_layout->get().entry_parameters.empty());
}

TEST(ExecIrLowering, BindsDeviceFunctionParameterAddressesWithoutRequiringExecutionSupport) {
  const auto program = lower(resolve(R"ptx(
.func helper(.param .u32 input) {
  .reg .u32 %result;
  ld.param.u32 %result, [input];
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  ASSERT_EQ(program->parameters().size(),1);
  EXPECT_EQ(program->parameters()[0].role,exec_ir::ParameterDeclarationRole::DeviceInput);
  const auto instruction=program->fetch({common::FunctionId{0},common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& form=native_form<exec_ir::LdExplicitScalar>(instruction->get());
  EXPECT_EQ(form.operands.address.parameter_qualifier,exec_ir::ParameterAddressQualifier::Default);
  EXPECT_EQ(std::get<exec_ir::ParameterId>(std::get<exec_ir::SymbolRef>(form.operands.address.base).target),exec_ir::ParameterId{0});
}

TEST(ExecIrLowering, LowersScalarUnsignedLessThanPredicateComparison) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .u32 %r;
  setp.lt.u32 %p, %r, 1;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto instruction =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& form = native_form<exec_ir::SetpUnsigned>(instruction->get());
  EXPECT_EQ(form.comparison, exec_ir::ComparisonOperator::lt);
  const auto& operands=std::get<exec_ir::SetpUnsigned::SingleOperands>(form.operands);
  ASSERT_TRUE(operands.dst.predicate);
  EXPECT_EQ(operands.dst.predicate->register_ref.slot,common::RegisterSlot{0});
  EXPECT_EQ(operands.src1,(exec_ir::ScalarOperand{reg(1,exec_ir::DataType::u32)}));
  EXPECT_EQ(*exec_ir::immediate_value(std::get<exec_ir::Immediate>(operands.src2)),common::RawValue::b32(std::uint32_t{1}));
}

TEST(ExecIrLowering, LowersWarpSyncImmediateAndRegisterMasks) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .b32 %mask;
  bar.warp.sync 3;
  bar.warp.sync %mask;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  EXPECT_EQ(exec_ir::to_string(*program),
            R"expected(function:0:
  pc:0: bar/bar_warp_sync warp=true sync=true layout=default membermask=immediate{bits=0x3, type=u32, negative=false, integer-source=0x3}
  pc:1: bar/bar_warp_sync warp=true sync=true layout=default membermask=register{slot=register:0, class=General, type=b32, width=none, component=none}
  pc:2: exit/exit_bare layout=default
)expected");

  const auto predicated = lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p;
  @%p bar.warp.sync 1;
  exit;
}
)ptx"));
  ASSERT_TRUE(predicated);

  const auto other_form = lower(resolve(R"ptx(
.entry kernel() {
  bar.sync 0;
  exit;
}
)ptx"));
  ASSERT_TRUE(other_form);
  const auto instruction =
      other_form->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& sync = native_form<exec_ir::BarSync>(instruction->get());
  const auto& operands =
      std::get<exec_ir::BarSync::ImmediateBarrierOperands>(sync.operands);
  EXPECT_EQ(*exec_ir::immediate_value(operands.barrier), common::RawValue::b32(0U));
}

TEST(ExecIrLowering, LowersGeneratedFormsAndRejectsUnsupportedLeaves) {
  const auto lowered_mov_u32 = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %r1;
  exit;
}
)ptx"));
  ASSERT_TRUE(lowered_mov_u32);

  const auto lowered_sub = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  sub.u32 %r0, %r0, %r1;
  exit;
}
)ptx"));
  ASSERT_TRUE(lowered_sub);

  const auto lowered_predicate = lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p<2>;
  mov.pred %p0, %p1;
  exit;
}
)ptx"));
  ASSERT_TRUE(lowered_predicate);

  const auto lowered_vector_layout = lower(resolve(R"ptx(
.entry kernel() {
  .reg .v2 .u32 %vector;
  exit;
}
)ptx"));
  ASSERT_TRUE(lowered_vector_layout);

  auto malformed_module = resolve(R"ptx(
.entry kernel() {
  .reg .b32 %r<2>;
  mov.b32 %r0, %r1;
  exit;
}
)ptx");
  auto& form=source_form<source::MovScalar>(malformed_module.functions[0].body[0]);
  auto& operands=*form.src_mov_source;
  std::get<ptx_frontend::resolved_ir::ResolvedRegisterRef>(operands.value)
      .symbol_id.reset();
  const auto malformed = lower(malformed_module);
  ASSERT_FALSE(malformed);
  EXPECT_EQ(malformed.error().code, LoweringErrorCode::malformed_resolved_ir);

  auto malformed_scalar_member = resolve(R"ptx(
.entry kernel() {
  .reg .b32 %r0;
  mov.b32 %r0, %r0;
  exit;
}
)ptx");
  auto& scalar_form=source_form<source::MovScalar>(malformed_scalar_member.functions[0].body[0]);
  auto& scalar_operands=*scalar_form.src_mov_source;
  std::get<ptx_frontend::resolved_ir::ResolvedRegisterRef>(
      scalar_operands.value)
      .parameterized_index = std::numeric_limits<std::uint32_t>::max();
  const auto malformed_member = lower(malformed_scalar_member);
  ASSERT_FALSE(malformed_member);
  EXPECT_EQ(malformed_member.error().code,
            LoweringErrorCode::malformed_resolved_ir);
}

TEST(ExecIrLowering, BindsTrailingTargetBoundariesAndLowersPredicatedExit) {
  const auto trailing_target = lower(resolve(R"ptx(
.entry kernel() {
  bra trailing;
trailing:
}
)ptx"));
  ASSERT_TRUE(trailing_target);
  const auto trailing_branch=trailing_target->fetch({common::FunctionId{0},common::ProgramCounter{0}});
  ASSERT_TRUE(trailing_branch);
  const auto& trailing_form=native_form<exec_ir::BraDirect>(trailing_branch->get());
  EXPECT_EQ(trailing_form.operands.target.pc.value(),trailing_target->functions()[0].body->instruction_count);

  auto unbound_label_module = resolve(R"ptx(
.entry kernel() {
  bra target;
target:
  exit;
}
)ptx");
  auto& branch=source_form<source::BraDirect>(
      unbound_label_module.functions[0].body[0]);
  branch.target.value.symbol_id.reset();
  const auto unbound_label = lower(unbound_label_module);
  ASSERT_FALSE(unbound_label);
  EXPECT_EQ(unbound_label.error().code,
            LoweringErrorCode::malformed_resolved_ir);

  auto missing_label_position = resolve(R"ptx(
.entry kernel() {
  bra target;
target:
  exit;
}
)ptx");
  missing_label_position.functions[0].label_positions.clear();
  const auto missing_label = lower(missing_label_position);
  ASSERT_FALSE(missing_label);
  EXPECT_EQ(missing_label.error().code,
            LoweringErrorCode::malformed_resolved_ir);

  auto duplicate_label_position = resolve(R"ptx(
.entry kernel() {
  bra target;
target:
  exit;
}
)ptx");
  duplicate_label_position.functions[0].label_positions.push_back(
      duplicate_label_position.functions[0].label_positions.front());
  const auto duplicate_label = lower(duplicate_label_position);
  ASSERT_FALSE(duplicate_label);
  EXPECT_EQ(duplicate_label.error().code,
            LoweringErrorCode::malformed_resolved_ir);

  auto out_of_range_label_position = resolve(R"ptx(
.entry kernel() {
  bra target;
target:
  exit;
}
)ptx");
  out_of_range_label_position.functions[0]
      .label_positions[0]
      .instruction_offset = 3U;
  const auto out_of_range_label = lower(out_of_range_label_position);
  ASSERT_FALSE(out_of_range_label);
  EXPECT_EQ(out_of_range_label.error().code,
            LoweringErrorCode::malformed_resolved_ir);

  const auto predicated_exit = lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p;
  @%p exit;
}
)ptx"));
  ASSERT_TRUE(predicated_exit);
  const auto instruction = predicated_exit->fetch(
      {common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  EXPECT_TRUE((instruction->get()).execution_predicate);
}

TEST(ExecIrLowering, LowersPredicatesForEverySupportedOperation) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .b32 %b<2>;
  .reg .u32 %r<2>;
  @!%p mov.b32 %b1, %b0;
  @%p add.u32 %r1, %r0, 1;
  @%p bra done;
  @!%p exit;
done:
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  for (std::uint32_t pc = 0; pc < 4; ++pc) {
    const auto instruction =
        program->fetch({common::FunctionId{0}, common::ProgramCounter{pc}});
    ASSERT_TRUE(instruction);
    ASSERT_TRUE((instruction->get()).execution_predicate);
    EXPECT_EQ((instruction->get()).execution_predicate->register_ref.slot,
              common::RegisterSlot{0});
  }
  EXPECT_TRUE(
      (
          program->fetch({common::FunctionId{0}, common::ProgramCounter{0}})
              ->get()).execution_predicate
          ->negated);
  EXPECT_FALSE(
      (
          program->fetch({common::FunctionId{0}, common::ProgramCounter{1}})
              ->get()).execution_predicate
          ->negated);
}

TEST(ExecIrLowering, PreservesZeroBodyPrototypes) {
  const auto program = lower(resolve(R"ptx(
.func prototype();
.entry kernel() {
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  EXPECT_EQ(exec_ir::to_string(*program),
            R"expected(function:0: <declaration>
function:1:
  pc:0: exit/exit_bare layout=default
)expected");
}

TEST(ExecIrLowering, LowersGenericAndGlobalScalarMemory) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  .reg .b64 %a;
  ld.u32 %r, [%a];
  ld.global.u32 %r, [%a];
  st.u32 [%a], %r;
  st.global.u32 [%a], %r;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  EXPECT_EQ(exec_ir::to_string(*program),
            R"expected(function:0:
  pc:0: ld/ld_generic_scalar mmio=false semantics=omitted scope=none cache=unspecified type=u32 layout=default dst=register{slot=register:0, class=General, type=u32, width=none, component=none} address=address{base=register{slot=register:1, class=General, type=b64, width=none, component=none}, offset=none, enclosing=Entry, parameter=Default, unified=false, named-index=none}
  pc:1: ld/ld_explicit_scalar mmio=false semantics=omitted scope=none state_space=global cache=unspecified type=u32 layout=default dst=register{slot=register:0, class=General, type=u32, width=none, component=none} address=address{base=register{slot=register:1, class=General, type=b64, width=none, component=none}, offset=none, enclosing=Entry, parameter=Default, unified=false, named-index=none}
  pc:2: st/st_generic_scalar mmio=false semantics=omitted scope=none cache=unspecified type=u32 layout=default address=address{base=register{slot=register:1, class=General, type=b64, width=none, component=none}, offset=none, enclosing=Entry, parameter=Default, unified=false, named-index=none} src=register{slot=register:0, class=General, type=u32, width=none, component=none}
  pc:3: st/st_explicit_scalar mmio=false semantics=omitted scope=none state_space=global cache=unspecified type=u32 layout=default address=address{base=register{slot=register:1, class=General, type=b64, width=none, component=none}, offset=none, enclosing=Entry, parameter=Default, unified=false, named-index=none} src=register{slot=register:0, class=General, type=u32, width=none, component=none}
  pc:4: exit/exit_bare layout=default
)expected");
}

TEST(ExecIrLowering, LowersScalarMemoryFormsAndPreservesOffsets) {
  const auto type = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u64 %r, %a;
  ld.u64 %r, [%a];
  exit;
}
)ptx"));
  ASSERT_TRUE(type);

  const auto space = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  .reg .b64 %a;
  ld.shared.u32 %r, [%a];
  exit;
}
)ptx"));
  ASSERT_TRUE(space);

  const auto offset = lower(resolve(R"ptx(
.entry kernel() {
  .reg .u32 %r;
  .reg .b64 %a;
  ld.u32 %r, [%a+4];
  exit;
}
)ptx"));
  ASSERT_TRUE(offset);
  const auto instruction=offset->fetch({common::FunctionId{0},common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  EXPECT_EQ(native_form<exec_ir::LdGenericScalar>(instruction->get()).operands.address.offset,
      (std::optional{exec_ir::AddressOffset{exec_ir::AddressOffsetOperator::Add,
          exec_ir::Immediate{4,exec_ir::DataType::s64,false,4}}}));
}

TEST(ExecIrLowering, PreservesAllTopologyIdentitiesAndComponents) {
  const std::array names{"tid", "ntid", "ctaid", "nctaid"};
  const std::array ids{exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::Tid,0},
                       exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::NTid,0},
                       exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::CtaId,0},
                       exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::NCtaId,0}};
  for (std::size_t index = 0; index < names.size(); ++index) {
    for (std::uint8_t component = 0; component < 3; ++component) {
      const std::string source = ".entry kernel() { .reg .u32 %r; mov.u32 %r, %" +
          std::string(names[index]) + "." + "xyz"[component] + "; exit; }";
      SCOPED_TRACE(source);
      const auto program = lower(resolve(source));
      ASSERT_TRUE(program);
      const auto instruction =
          program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
      ASSERT_TRUE(instruction);
      const auto& form = native_form<exec_ir::MovScalar>(instruction->get());
      const auto& operands = std::get<exec_ir::MovScalar::ScalarOperands>(form.operands);
      EXPECT_EQ(std::get<exec_ir::SpecialRegisterRef>(operands.src),
                (exec_ir::SpecialRegisterRef{ids[index],component,exec_ir::DataType::u32,4}));
    }
  }
  const auto program = lower(resolve(
      ".entry kernel() { .reg .u32 %r; mov.u32 %r, %laneid; exit; }"));
  ASSERT_TRUE(program);
  const auto instruction =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& form = native_form<exec_ir::MovScalar>(instruction->get());
  const auto& operands = std::get<exec_ir::MovScalar::ScalarOperands>(form.operands);
  EXPECT_EQ(std::get<exec_ir::SpecialRegisterRef>(operands.src),
            (exec_ir::SpecialRegisterRef{exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::LaneId,0},std::nullopt,exec_ir::DataType::u32,1}));
}

TEST(ExecIrLowering, AllocatesDistinctContiguousVectorRegisterMembers) {
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .v4 .u32 %v<2>;
  mov.v4.u32 %v0, %tid;
  mov.v4.u32 %v1, %ntid;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  for (std::uint32_t pc = 0; pc < 2; ++pc) {
    const auto instruction =
        program->fetch({common::FunctionId{0}, common::ProgramCounter{pc}});
    ASSERT_TRUE(instruction);
    const auto& form = native_form<exec_ir::MovV4U32>(instruction->get());
    ASSERT_EQ(form.operands.dst.elements.size(),4);
    EXPECT_EQ(form.operands.dst.elements[0]->slot,common::RegisterSlot{4U*pc});
    EXPECT_EQ(std::get<exec_ir::VectorSpecialRegisterRef>(form.operands.src).register_ref.id, (pc == 0 ? exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::Tid,0}
                                  : exec_ir::SpecialRegisterId{exec_ir::SpecialRegisterKind::NTid,0}));
  }
}

TEST(ExecIrLowering, RejectsMalformedTopologyAndVectorReferences) {
  auto module = resolve(
      ".entry kernel() { .reg .u32 %r; mov.u32 %r, %tid.x; exit; }");
  auto& scalar = source_form<source::MovScalar>(module.functions[0].body[0]);
  auto& source = scalar.src_mov_source->value;
  auto& special = std::get<ptx_frontend::resolved_ir::ResolvedSpecialRegisterRef>(source);
  special.component = static_cast<ptx_frontend::base::VectorComponent>(3U);
  const auto invalid_component = lower(module);
  ASSERT_FALSE(invalid_component);
  EXPECT_EQ(invalid_component.error().code, LoweringErrorCode::malformed_resolved_ir);
  special.component = ptx_frontend::base::VectorComponent::X;
  special.id.index = 1U;
  const auto invalid_identity = lower(module);
  ASSERT_FALSE(invalid_identity);
  EXPECT_EQ(invalid_identity.error().code, LoweringErrorCode::malformed_resolved_ir);

  auto vector_module = resolve(
      ".entry kernel() { .reg .v4 .u32 %v; mov.v4.u32 %v, %tid; exit; }");
  auto& vector = source_form<source::MovV4U32>(vector_module.functions[0].body[0]);
  ASSERT_TRUE(vector.dst.value.source.whole_base);
  vector.dst.value.source.whole_base->value.register_ref.vector_width=2U;
  const auto invalid_vector = lower(vector_module);
  ASSERT_FALSE(invalid_vector);
  EXPECT_EQ(invalid_vector.error().code, LoweringErrorCode::malformed_resolved_ir);
}

TEST(ExecIrLowering, KeepsArchitecturalSpecialRegistersAsExplicitPrerequisites) {
  for (const auto source : {"%clock", "%smid", "%pm1"}) {
    SCOPED_TRACE(source);
    const auto program = lower(resolve(
        ".version 9.3\n.target sm_100\n.entry kernel() { .reg .u32 %r; mov.u32 %r, " +
        std::string(source) + "; exit; }"));
    ASSERT_TRUE(program);
    const auto instruction=program->fetch({common::FunctionId{0},common::ProgramCounter{0}});
    ASSERT_TRUE(instruction);
    const auto& form=native_form<exec_ir::MovScalar>(instruction->get());
    const auto& operands=std::get<exec_ir::MovScalar::ScalarOperands>(form.operands);
    const auto& intrinsic=std::get<exec_ir::SpecialRegisterRef>(operands.src);
    EXPECT_EQ(intrinsic.id.kind,source==std::string_view{"%clock"}?exec_ir::SpecialRegisterKind::Clock:
        source==std::string_view{"%smid"}?exec_ir::SpecialRegisterKind::SmId:exec_ir::SpecialRegisterKind::PerformanceMonitor);
    EXPECT_EQ(intrinsic.id.index,source==std::string_view{"%pm1"}?1:0);
  }
}

TEST(ExecIrLowering, BindsEntryParameterMoveAddressesToAbiOffsets) {
  const auto program = lower(resolve(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.entry kernel(.param .u64 first, .param .u32 second) {
  .reg .u64 %wide;
  .reg .u32 %narrow;
  mov.u64 %wide, first;
  mov.u32 %narrow, second;
  mov.u64 %wide, second+4;
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  for (std::uint32_t pc = 0; pc < 3; ++pc) {
    const auto instruction =
        program->fetch({common::FunctionId{0}, common::ProgramCounter{pc}});
    ASSERT_TRUE(instruction);
    const auto& form = native_form<exec_ir::MovScalar>(instruction->get());
    const auto& operands = std::get<exec_ir::MovScalar::ScalarOperands>(form.operands);
    const auto& symbol=pc<2?std::get<exec_ir::SymbolRef>(operands.src):
        std::get<exec_ir::SymbolRef>(std::get<exec_ir::Address>(operands.src).base);
    EXPECT_EQ(std::get<exec_ir::ParameterId>(symbol.target),exec_ir::ParameterId{pc==0?0U:1U});
    const auto& parameter=program->parameters()[std::get<exec_ir::ParameterId>(symbol.target).value()];
    EXPECT_EQ(parameter.owner_function,common::FunctionId{0});
    EXPECT_EQ(parameter.role,exec_ir::ParameterDeclarationRole::EntryInput);
    if(pc==2) {
      const auto& address=std::get<exec_ir::Address>(operands.src);
      ASSERT_TRUE(address.offset);
      EXPECT_EQ(address.offset->operation,exec_ir::AddressOffsetOperator::Add);
      EXPECT_EQ(*exec_ir::immediate_value(address.offset->value),common::RawValue::b64(std::uint64_t{4}));
    }
  }
}

}  // namespace ptxsim::exec_ir_lowering::test
