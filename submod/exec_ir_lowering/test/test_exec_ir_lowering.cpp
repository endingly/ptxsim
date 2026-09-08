#include <gtest/gtest.h>

#include <array>
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

using ptx_frontend::resolved_ir::Mov;
using ptx_frontend::resolved_ir::ResolvedModule;

auto resolve(std::string_view source) -> ResolvedModule {
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
  const auto program = lower(resolve(R"ptx(
.entry kernel() {
  .reg .b32 %r;
  .reg .b64 %a;
  ld.global.v8.b32 {%r, _, _, _, _, _, _, _}, [%a-32];
  ld.global.u32 %r, [-4];
  exit;
}
)ptx"));
  ASSERT_TRUE(program);
  const auto text = exec_ir::to_string(*program);
  EXPECT_NE(text.find("-" + common::to_string(
                                common::RawValue::b64(std::uint64_t{32}))),
            std::string::npos);
  EXPECT_NE(text.find("_"), std::string::npos);
}

}  // namespace

TEST(ExecIrLowering, LowersBoundProgramsWithoutFrontendLifetime) {
  auto program_result = lowered_after_frontend_dies();
  ASSERT_TRUE(program_result);
  const auto& program = *program_result;

  EXPECT_EQ(exec_ir::to_string(program),
            "gpc0  [func:0 pc:0]  "
            "@!predicate:0 mov.b32 register:2, register:1\n"
            "gpc1  [func:0 pc:1]  "
            "@predicate:0 add.u32 register:5, register:3, b32:0x00000007\n"
            "gpc2  [func:0 pc:2]  "
            "bra pc:5\n"
            "gpc3  [func:0 pc:3]  "
            "add.u32 register:3, register:3, register:4\n"
            "gpc4  [func:0 pc:4]  "
            "add.u32 register:6, register:3, register:4\n"
            "gpc5  [func:0 pc:5]  "
            "exit\n"
            "gpc6  [func:1 pc:0]  "
            "mov.b32 register:1, register:0\n"
            "gpc7  [func:1 pc:1]  "
            "exit");
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
            "gpc0  [func:0 pc:0]  "
            "mov.b32 register:0, register:0\n"
            "gpc1  [func:0 pc:1]  "
            "exit");
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
  const auto& mov = std::get<exec_ir::Mov>(instruction->get());
  const auto& form = std::get<exec_ir::Mov::Scalar>(mov.variant);
  const auto& operands =
      std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
  EXPECT_EQ(std::get<exec_ir::SpecialRegisterRef>(operands.src),
            (exec_ir::SpecialRegisterRef{
                .id = exec_ir::kThreadIdSpecialRegister,
                .component = 0U,
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
  const auto& resolved_mov = std::get<Mov>(module.functions[0].body[0]);
  const auto& resolved_form = std::get<Mov::Scalar>(resolved_mov.variant);
  const auto& resolved_operands =
      std::get<Mov::Scalar::ScalarOperands>(resolved_form.operands);
  const auto& immediate =
      std::get<ptx_frontend::resolved_ir::ResolvedImmediate>(
          resolved_operands.src.value);
  EXPECT_EQ(immediate.type, ptx_frontend::base::ScalarType::B64);
  EXPECT_EQ(immediate.bits, 0U);
  EXPECT_FALSE(immediate.is_negative);

  const auto program = lower(module);
  ASSERT_TRUE(program);
  const auto instruction =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& mov = std::get<exec_ir::Mov>(instruction->get());
  const auto& form = std::get<exec_ir::Mov::Scalar>(mov.variant);
  const auto& operands =
      std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
  EXPECT_EQ(form.type, exec_ir::DataType::b64);
  EXPECT_EQ(operands.dst, common::RegisterSlot{0});
  EXPECT_EQ(std::get<common::RawValue>(operands.src),
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
  auto& add =
      std::get<ptx_frontend::resolved_ir::Add>(module.functions[0].body[0]);
  auto& form =
      std::get<ptx_frontend::resolved_ir::Add::IntegerNoSat>(add.variant);
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
                {.offset = 0U, .size = 4U, .alignment = 4U},
                {.offset = 8U, .size = 8U, .alignment = 8U},
                {.offset = 16U, .size = 4U, .alignment = 4U},
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
  const auto& first_form = std::get<exec_ir::Ld::ExplicitScalar>(
      std::get<exec_ir::Ld>(first->get()).variant);
  const auto& second_form = std::get<exec_ir::Ld::ExplicitScalar>(
      std::get<exec_ir::Ld>(second->get()).variant);
  const auto& third_form = std::get<exec_ir::Ld::ExplicitScalar>(
      std::get<exec_ir::Ld>(third->get()).variant);
  EXPECT_EQ(first_form.address,
            (exec_ir::Address{common::RawValue::b64(std::uint64_t{0})}));
  EXPECT_EQ(second_form.address,
            (exec_ir::Address{common::RawValue::b64(std::uint64_t{8})}));
  EXPECT_EQ(std::get<common::RawValue>(third_form.address.base),
            common::RawValue::b64(std::uint64_t{16}));
  EXPECT_EQ(
      third_form.address.offset,
      (exec_ir::AddressOffset{true, common::RawValue::b64(std::uint64_t{4})}));
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
                .offset = 0U, .size = 8U, .alignment = 16U}));
  EXPECT_EQ(layout->get().entry_parameters[1],
            (exec_ir::EntryParameterLayout{
                .offset = 8U, .size = 8U, .alignment = 8U}));
  EXPECT_EQ(layout->get().entry_parameters[3].offset, 24U);
  EXPECT_EQ(layout->get().entry_parameters[8].offset, 44U);

  const auto address =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{2}});
  ASSERT_TRUE(address);
  const auto& form = std::get<exec_ir::Ld::ExplicitScalar>(
      std::get<exec_ir::Ld>(address->get()).variant);
  EXPECT_EQ(std::get<common::RawValue>(form.address.base),
            common::RawValue::b64(std::uint64_t{44}));
  EXPECT_EQ(
      form.address.offset,
      (exec_ir::AddressOffset{true, common::RawValue::b64(std::uint64_t{4})}));
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
                {.offset = 0U, .size = 16U, .alignment = 16U},
                {.offset = 16U, .size = 4U, .alignment = 4U},
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
                {.offset = 0U, .size = 2U, .alignment = 2U},
                {.offset = 2U, .size = 2U, .alignment = 2U},
                {.offset = 4U, .size = 4U, .alignment = 4U},
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
                {.offset = 0U, .size = 16U, .alignment = 16U},
                {.offset = 16U, .size = 8U, .alignment = 8U},
                {.offset = 24U, .size = 4U, .alignment = 4U},
            }));
}

TEST(ExecIrLowering, RejectsMalformedEntryParameterMetadata) {
  const auto source = R"ptx(
.entry first(.param .u32 input, .param .align 16 .b8 bytes[16]) { exit; }
.entry second(.param .u32 input) { exit; }
)ptx";
  const auto expect_invalid = [](ResolvedModule module) {
    const auto result = lower(module);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              LoweringErrorCode::invalid_entry_parameter_layout);
  };

  auto duplicate = resolve(source);
  duplicate.functions[0].parameter_declarations.push_back(
      duplicate.functions[0].parameter_declarations.front());
  expect_invalid(std::move(duplicate));

  auto foreign = resolve(source);
  foreign.functions[0].parameter_declarations.front().symbol_id =
      foreign.functions[1].parameter_declarations.front().symbol_id;
  expect_invalid(std::move(foreign));

  auto missing = resolve(source);
  missing.functions[0].parameter_declarations.erase(
      missing.functions[0].parameter_declarations.begin());
  expect_invalid(std::move(missing));

  auto missing_extent = resolve(source);
  missing_extent.functions[0]
      .parameter_declarations.front()
      .byte_extent.reset();
  expect_invalid(std::move(missing_extent));

  auto non_power_two_alignment = resolve(source);
  non_power_two_alignment.functions[0]
      .parameter_declarations.front()
      .alignment = 3U;
  expect_invalid(std::move(non_power_two_alignment));

  auto mismatched_symbol_alignment = resolve(source);
  auto& alignment_symbol = const_cast<ptx_frontend::binding::Symbol&>(
      mismatched_symbol_alignment.symbols.symbol(
          mismatched_symbol_alignment.functions[0]
              .parameter_declarations.front()
              .symbol_id));
  alignment_symbol.address_alignment = 8U;
  expect_invalid(std::move(mismatched_symbol_alignment));

  auto invalid_role = resolve(source);
  invalid_role.functions[0].parameter_declarations.front().role =
      static_cast<ptx_frontend::resolved_ir::ParameterDeclarationRole>(255U);
  expect_invalid(std::move(invalid_role));

  auto device_role_on_entry = resolve(source);
  device_role_on_entry.functions[0].parameter_declarations.front().role =
      ptx_frontend::resolved_ir::ParameterDeclarationRole::DeviceInput;
  expect_invalid(std::move(device_role_on_entry));

  auto entry_role_on_device = resolve(R"ptx(
.func helper(.param .u32 input) { exit; }
)ptx");
  entry_role_on_device.functions[0].parameter_declarations.front().role =
      ptx_frontend::resolved_ir::ParameterDeclarationRole::EntryInput;
  expect_invalid(std::move(entry_role_on_device));

  auto reordered_unsized_input = resolve(R"ptx(
.version 9.3
.target sm_100
.func helper(.param .u32 first, .param .b8 bytes[]) { exit; }
)ptx");
  std::swap(reordered_unsized_input.functions[0].parameter_declarations[0],
            reordered_unsized_input.functions[0].parameter_declarations[1]);
  expect_invalid(std::move(reordered_unsized_input));

  auto invalid_scope = resolve(source);
  invalid_scope.functions[0].parameter_declarations.front().scope_id =
      invalid_scope.symbols.moduleScope();
  expect_invalid(std::move(invalid_scope));

  auto invalid_size = resolve(source);
  invalid_size.functions[0].parameter_declarations.front().byte_extent = 8U;
  expect_invalid(std::move(invalid_size));

  auto vector_parameter = resolve(source);
  vector_parameter.functions[0].parameter_declarations.front().vector_width =
      2U;
  expect_invalid(std::move(vector_parameter));

  auto multi_dimensional = resolve(source);
  multi_dimensional.functions[0]
      .parameter_declarations[1]
      .array_extents.push_back(1U);
  expect_invalid(std::move(multi_dimensional));

  // Deliberately corrupt non-const module storage through the frontend's
  // read-only symbol view to exercise unsupported entry declaration shapes.
  auto vector_symbol_metadata = resolve(source);
  auto& vector_symbol = const_cast<ptx_frontend::binding::Symbol&>(
      vector_symbol_metadata.symbols.symbol(vector_symbol_metadata.functions[0]
                                                .parameter_declarations.front()
                                                .symbol_id));
  vector_symbol.vector_width = 2U;
  expect_invalid(std::move(vector_symbol_metadata));

  auto parameter_group = resolve(source);
  auto& group_symbol = const_cast<
      ptx_frontend::binding::Symbol&>(parameter_group.symbols.symbol(
      parameter_group.functions[0].parameter_declarations.front().symbol_id));
  group_symbol.parameterized_count = 2U;
  expect_invalid(std::move(parameter_group));

  auto zero_extent = resolve(source);
  zero_extent.functions[0].parameter_declarations[1].array_extents[0] = 0U;
  expect_invalid(std::move(zero_extent));

  auto cumulative_overflow = resolve(source);
  cumulative_overflow.functions[0].parameter_declarations[1].array_extents[0] =
      std::numeric_limits<std::uint64_t>::max();
  cumulative_overflow.functions[0].parameter_declarations[1].byte_extent =
      std::numeric_limits<std::uint64_t>::max();
  expect_invalid(std::move(cumulative_overflow));

  auto multiplication_overflow = resolve(R"ptx(
.entry kernel(.param .b16 values[2]) { exit; }
)ptx");
  multiplication_overflow.functions[0]
      .parameter_declarations[0]
      .array_extents[0] = std::numeric_limits<std::uint64_t>::max();
  multiplication_overflow.functions[0].parameter_declarations[0].byte_extent =
      std::numeric_limits<std::uint64_t>::max();
  expect_invalid(std::move(multiplication_overflow));

  auto unsized = resolve(source);
  unsized.functions[0].parameter_declarations[1].array_extents[0] =
      std::nullopt;
  unsized.functions[0].parameter_declarations[1].byte_extent.reset();
  expect_invalid(std::move(unsized));
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
  for (const auto function : {common::FunctionId{0}, common::FunctionId{1}}) {
    const auto layout = program->function_layout(function);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->get().entry_parameter_size, 0U);
    EXPECT_TRUE(layout->get().entry_parameters.empty());
  }
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
                {.offset = 0U, .size = 4U, .alignment = 4U}}));
  EXPECT_EQ(device_layout->get().entry_parameter_size, 0U);
  EXPECT_TRUE(device_layout->get().entry_parameters.empty());
}

TEST(ExecIrLowering, KeepsDeviceFunctionParametersUnsupported) {
  const auto program = lower(resolve(R"ptx(
.func helper(.param .u32 input) {
  .reg .u32 %result;
  ld.param.u32 %result, [input];
  exit;
}
)ptx"));
  ASSERT_FALSE(program);
  EXPECT_EQ(program.error().code, LoweringErrorCode::unsupported_operand);
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
  const auto& setp = std::get<exec_ir::Setp>(instruction->get());
  const auto& form = std::get<exec_ir::Setp::LtU32>(setp.variant);
  EXPECT_EQ(form.comparison, exec_ir::ComparisonOperator::lt);
  EXPECT_EQ(form.dst, (exec_ir::Predicate{common::RegisterSlot{0}}));
  EXPECT_EQ(form.src1, (exec_ir::ScalarOperand{common::RegisterSlot{1}}));
  EXPECT_EQ(form.src2, (exec_ir::ScalarOperand{common::RawValue::b32(1U)}));
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
            "gpc0  [func:0 pc:0]  "
            "bar.warp.sync b32:0x00000003\n"
            "gpc1  [func:0 pc:1]  "
            "bar.warp.sync register:0\n"
            "gpc2  [func:0 pc:2]  "
            "exit");

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
  const auto& barrier = std::get<exec_ir::Bar>(instruction->get());
  const auto& sync = std::get<exec_ir::Bar::Sync>(barrier.variant);
  const auto& operands =
      std::get<exec_ir::Bar::Sync::ImmediateBarrierOperands>(sync.operands);
  EXPECT_EQ(operands.barrier, common::RawValue::b32(0U));
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
  auto& mov = std::get<Mov>(malformed_module.functions[0].body[0]);
  auto& form = std::get<Mov::Scalar>(mov.variant);
  auto& operands = std::get<Mov::Scalar::ScalarOperands>(form.operands);
  std::get<ptx_frontend::resolved_ir::ResolvedRegisterRef>(operands.src.value)
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
  auto& scalar_mov =
      std::get<Mov>(malformed_scalar_member.functions[0].body[0]);
  auto& scalar_form = std::get<Mov::Scalar>(scalar_mov.variant);
  auto& scalar_operands =
      std::get<Mov::Scalar::ScalarOperands>(scalar_form.operands);
  std::get<ptx_frontend::resolved_ir::ResolvedRegisterRef>(
      scalar_operands.src.value)
      .parameterized_index = std::numeric_limits<std::uint32_t>::max();
  const auto malformed_member = lower(malformed_scalar_member);
  ASSERT_FALSE(malformed_member);
  EXPECT_EQ(malformed_member.error().code,
            LoweringErrorCode::malformed_resolved_ir);
}

TEST(ExecIrLowering, RejectsTrailingTargetsAndLowersPredicatedExit) {
  const auto trailing_target = lower(resolve(R"ptx(
.entry kernel() {
  bra trailing;
trailing:
}
)ptx"));
  ASSERT_FALSE(trailing_target);
  EXPECT_EQ(trailing_target.error().code,
            LoweringErrorCode::invalid_branch_target);
  EXPECT_EQ(trailing_target.error().instruction, 0U);

  auto unbound_label_module = resolve(R"ptx(
.entry kernel() {
  bra target;
target:
  exit;
}
)ptx");
  auto& branch = std::get<ptx_frontend::resolved_ir::Bra>(
      unbound_label_module.functions[0].body[0]);
  std::get<ptx_frontend::resolved_ir::Bra::Direct>(branch.variant)
      .target.value.symbol_id.reset();
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
  EXPECT_TRUE(exec_ir::execution_predicate(instruction->get()));
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
    ASSERT_TRUE(exec_ir::execution_predicate(instruction->get()));
    EXPECT_EQ(exec_ir::execution_predicate(instruction->get())->source,
              common::RegisterSlot{0});
  }
  EXPECT_TRUE(
      exec_ir::execution_predicate(
          program->fetch({common::FunctionId{0}, common::ProgramCounter{0}})
              ->get())
          ->negated);
  EXPECT_FALSE(
      exec_ir::execution_predicate(
          program->fetch({common::FunctionId{0}, common::ProgramCounter{1}})
              ->get())
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
            "gpc0  [func:1 pc:0]  "
            "exit");
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
            "gpc0  [func:0 pc:0]  "
            "ld.u32 register:0, [register:1]\n"
            "gpc1  [func:0 pc:1]  "
            "ld.global.u32 register:0, [register:1]\n"
            "gpc2  [func:0 pc:2]  "
            "st.u32 [register:1], register:0\n"
            "gpc3  [func:0 pc:3]  "
            "st.global.u32 [register:1], register:0\n"
            "gpc4  [func:0 pc:4]  "
            "exit");
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
  EXPECT_NE(
      exec_ir::to_string(*offset).find(
          "+" + common::to_string(common::RawValue::b64(std::uint64_t{4}))),
      std::string::npos);
}

TEST(ExecIrLowering, PreservesAllTopologyIdentitiesAndComponents) {
  const std::array names{"tid", "ntid", "ctaid", "nctaid"};
  const std::array ids{exec_ir::kThreadIdSpecialRegister,
                       exec_ir::kThreadCountSpecialRegister,
                       exec_ir::kCtaIdSpecialRegister,
                       exec_ir::kCtaCountSpecialRegister};
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
      const auto& form = std::get<exec_ir::Mov::Scalar>(
          std::get<exec_ir::Mov>(instruction->get()).variant);
      const auto& operands = std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
      EXPECT_EQ(std::get<exec_ir::SpecialRegisterRef>(operands.src),
                (exec_ir::SpecialRegisterRef{ids[index], component}));
    }
  }
  const auto program = lower(resolve(
      ".entry kernel() { .reg .u32 %r; mov.u32 %r, %laneid; exit; }"));
  ASSERT_TRUE(program);
  const auto instruction =
      program->fetch({common::FunctionId{0}, common::ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  const auto& form = std::get<exec_ir::Mov::Scalar>(
      std::get<exec_ir::Mov>(instruction->get()).variant);
  const auto& operands = std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
  EXPECT_EQ(std::get<exec_ir::SpecialRegisterRef>(operands.src),
            (exec_ir::SpecialRegisterRef{exec_ir::kLaneIdSpecialRegister, std::nullopt}));
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
    const auto& form = std::get<exec_ir::Mov::V4U32>(
        std::get<exec_ir::Mov>(instruction->get()).variant);
    EXPECT_EQ(form.dst.register_slot, common::RegisterSlot{4U * pc});
    EXPECT_EQ(form.src.id, pc == 0 ? exec_ir::kThreadIdSpecialRegister
                                  : exec_ir::kThreadCountSpecialRegister);
  }
}

TEST(ExecIrLowering, RejectsMalformedTopologyAndVectorReferences) {
  auto module = resolve(
      ".entry kernel() { .reg .u32 %r; mov.u32 %r, %tid.x; exit; }");
  auto& scalar = std::get<Mov::Scalar>(std::get<Mov>(module.functions[0].body[0]).variant);
  auto& source = std::get<Mov::Scalar::ScalarOperands>(scalar.operands).src.value;
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
  auto& vector = std::get<Mov::V4U32>(
      std::get<Mov>(vector_module.functions[0].body[0]).variant);
  vector.dst.value.register_ref.vector_width = 2U;
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
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code, LoweringErrorCode::unsupported_operand);
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
    const auto& form = std::get<exec_ir::Mov::Scalar>(
        std::get<exec_ir::Mov>(instruction->get()).variant);
    const auto& operands = std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
    const auto& address = std::get<exec_ir::Address>(operands.src);
    EXPECT_EQ(std::get<common::RawValue>(address.base),
              common::RawValue::b64(std::uint64_t{pc == 0 ? 0U : 8U}));
    if (pc == 2) {
      ASSERT_TRUE(address.offset);
      EXPECT_FALSE(address.offset->subtract);
      EXPECT_EQ(address.offset->value, common::RawValue::b64(std::uint64_t{4}));
    } else {
      EXPECT_FALSE(address.offset);
    }
  }
}

}  // namespace ptxsim::exec_ir_lowering::test
