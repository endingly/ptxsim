#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include <ptxsim/lowering/lowering.hpp>

namespace ptxsim::lowering {
namespace {

auto lower_source(std::string source) {
  ptx_frontend::PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  if (!ast)
    return std::expected<program::ProgramImage, LoweringDiagnostic>{
        std::unexpected(LoweringDiagnostic{
            .code = LoweringDiagnosticCode::frontend_invalid_input})};
  auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  EXPECT_TRUE(resolved.has_value()) << resolved.error().front().message;
  if (!resolved)
    return std::expected<program::ProgramImage, LoweringDiagnostic>{
        std::unexpected(LoweringDiagnostic{
            .code = LoweringDiagnosticCode::frontend_invalid_input})};
  return lower_module(*ast, *resolved, "lowering-test.ptx");
}

TEST(ModuleLowering, LowersBranchesParameterizedRegistersAndGuards) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry first() {
  .reg .pred %p0, %p1;
  .reg .u32 %r<4>;
  mov.u32 %r0, 1;
  mov.pred %p1, %p0;
  @%p1 bra forward;
  add.u32 %r1, %r0, 2;
forward:
  sub.s32 %r2, %r1, 1;
  @!%p1 bra forward;
}
)ptx");

  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  ASSERT_EQ(image->functions().size(), 1U);
  EXPECT_EQ(image->functions()[0].registers.size(), 6U);
  EXPECT_EQ(image->instructions().size(), 6U);
  EXPECT_EQ(image->entry_points().size(), 1U);
  EXPECT_EQ(image->source_locations_by_pc().size(), 6U);
  EXPECT_EQ(image->source_locations()[0].file, "lowering-test.ptx");
  EXPECT_EQ(image->functions()[0].registers[2].slot.value(), 2U);
  EXPECT_EQ(image->functions()[0].registers[5].slot.value(), 5U);

  const auto& move = std::get<exec_ir::MovInst>(image->instructions()[0]);
  EXPECT_EQ(move.dest.slot.value(), 2U);
  const auto& add =
      std::get<exec_ir::IntegerBinaryInst>(image->instructions()[3]);
  const auto& sub =
      std::get<exec_ir::IntegerBinaryInst>(image->instructions()[4]);
  EXPECT_EQ(add.dest.slot.value(), 3U);
  EXPECT_EQ(sub.dest.slot.value(), 4U);

  const auto& forward = std::get<exec_ir::BranchInst>(image->instructions()[2]);
  EXPECT_EQ(forward.target.pc.value(), 4U);
  ASSERT_TRUE(forward.guard.has_value());
  EXPECT_EQ(forward.guard->predicate.value(), 1U);
  EXPECT_FALSE(forward.guard->negated);
  const auto& backward =
      std::get<exec_ir::BranchInst>(image->instructions()[5]);
  EXPECT_EQ(backward.target.pc.value(), 4U);
  ASSERT_TRUE(backward.guard.has_value());
  EXPECT_EQ(backward.guard->predicate.value(), 1U);
  EXPECT_TRUE(backward.guard->negated);
  EXPECT_EQ(image->source_locations_by_pc()[5]->value(), 5U);
  EXPECT_GT(image->source_locations()[5].line, 0U);
  EXPECT_GT(image->source_locations()[5].column, 0U);
}

TEST(ModuleLowering, LowersLabelsInsideNestedBlocks) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .u32 %r0;
  {
    loop:
    mov.u32 %r0, 1;
    bra loop;
  }
}
)ptx");

  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  ASSERT_EQ(image->instructions().size(), 2U);
  EXPECT_EQ(
      std::get<exec_ir::BranchInst>(image->instructions()[1]).target.pc.value(),
      0U);
}

TEST(ModuleLowering, LowersMultipleFunctionsAndMemorySymbols) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.global .u32 value;
.const .u32 constant;
.visible .entry first() {
  .reg .u32 %r0, %r1;
  ld.global.u32 %r0, [value];
  st.global.u32 [value+4], %r0;
  ld.const.u32 %r1, [constant];
}
.func second() {
  .reg .u32 %r0, %r1;
  .reg .u64 %rd0;
  mov.u32 %r0, 3;
  mul.wide.u32 %rd0, %r0, 4;
  and.b32 %r1, %r0, 1;
}
)ptx");

  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  EXPECT_EQ(image->functions().size(), 2U);
  EXPECT_EQ(image->symbols().size(), 2U);
  EXPECT_EQ(image->instructions().size(), 6U);
  EXPECT_EQ(image->functions()[0].begin_pc.value(), 0U);
  EXPECT_EQ(image->functions()[1].begin_pc.value(), 3U);
  const auto& store = std::get<exec_ir::StoreInst>(image->instructions()[1]);
  const auto* store_base = std::get_if<common::SymbolId>(&store.address.base);
  ASSERT_NE(store_base, nullptr);
  EXPECT_EQ(store_base->value(), 0U);
  EXPECT_EQ(store.address.byte_offset, 4);
  const auto& constant = std::get<exec_ir::LoadInst>(image->instructions()[2]);
  EXPECT_EQ(constant.space, exec_ir::MemorySpace::constant);
  const auto* constant_base =
      std::get_if<common::SymbolId>(&constant.address.base);
  ASSERT_NE(constant_base, nullptr);
  EXPECT_EQ(constant_base->value(), 1U);
}

TEST(ModuleLowering, RejectsUnsupportedLegalForms) {
  const auto unsupported_type = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { .reg .u16 %r0, %r1; add.u16 %r0, %r1, 1; }
)ptx");
  ASSERT_FALSE(unsupported_type.has_value());
  EXPECT_EQ(unsupported_type.error().code,
            LoweringDiagnosticCode::unsupported_type_combination);

  const auto unsupported_control = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { L: bra.uni L; }
)ptx");
  ASSERT_FALSE(unsupported_control.has_value());
  EXPECT_EQ(unsupported_control.error().code,
            LoweringDiagnosticCode::unsupported_ptx_variant);

  const auto unsupported_instruction = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { ret; }
)ptx");
  ASSERT_FALSE(unsupported_instruction.has_value());
  EXPECT_EQ(unsupported_instruction.error().instruction_context, "ret");
  EXPECT_EQ(unsupported_instruction.error().function_context, "kernel");
}

TEST(ModuleLowering, CopiesTheProvidedSourceFileName) {
  std::string file = "owned-name.ptx";
  ptx_frontend::PtxSyntaxParser parser(
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }");
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto image = lower_module(*ast, *resolved, file);
  ASSERT_TRUE(image.has_value()) << to_string(image.error());

  file[0] = 'X';
  EXPECT_EQ(image->source_locations()[0].file, "owned-name.ptx");
  EXPECT_TRUE(program::verify(*image).has_value());
}

TEST(ModuleLowering, LowersRemainingInitialInstructionForms) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .u16 %h0;
  .reg .u32 %r0, %r1, %r2;
  .reg .s32 %s0;
  .reg .u64 %d0;
  .reg .s64 %sd0;
  mov.b16 %h0, 1;
  mov.b64 %d0, 1;
  mul.lo.u32 %r1, %r0, 2;
  mul.hi.u32 %r1, %r0, 2;
  mul.wide.u32 %d0, %r0, 2;
  mul.wide.s32 %sd0, %s0, 2;
  or.b32 %r1, %r0, 1;
  xor.b32 %r2, %r1, 1;
}
)ptx");

  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  EXPECT_EQ(image->instructions().size(), 8U);
  EXPECT_TRUE(std::holds_alternative<exec_ir::IntegerMulInst>(
      image->instructions()[2]));
  EXPECT_TRUE(
      std::holds_alternative<exec_ir::BitInst>(image->instructions()[6]));
}

TEST(ModuleLowering, IsDeterministicForTheSameSource) {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }
)ptx";
  const auto first = lower_source(std::string(source));
  const auto second = lower_source(std::string(source));
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(program::dump(*first), program::dump(*second));
}

TEST(ModuleLowering, RejectsAstResolvedOpcodeMismatchWithFunctionContext) {
  ptx_frontend::PtxSyntaxParser parser(
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto* function =
      std::get_if<ptx_frontend::syntax_ast::AstFunction>(&ast->items[3]);
  ASSERT_NE(function, nullptr);
  auto* instruction =
      std::get_if<ptx_frontend::syntax_ast::AstInstruction>(&function->body[1]);
  ASSERT_NE(instruction, nullptr);
  instruction->opcode.syntax.text = "xor";

  const auto image = lower_module(*ast, *resolved, "mismatch.ptx");
  ASSERT_FALSE(image.has_value());
  EXPECT_EQ(image.error().code, LoweringDiagnosticCode::malformed_resolved_ir);
  EXPECT_EQ(image.error().function_context, "kernel");
}

TEST(ModuleLowering, ReportsFunctionForMalformedLabelPlacement) {
  ptx_frontend::PtxSyntaxParser parser(
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .entry kernel() { L: bra L; }");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto* function =
      std::get_if<ptx_frontend::syntax_ast::AstFunction>(&ast->items[3]);
  ASSERT_NE(function, nullptr);
  auto* label =
      std::get_if<ptx_frontend::syntax_ast::AstLabel>(&function->body[0]);
  ASSERT_NE(label, nullptr);
  label->name.syntax.text = "missing";

  const auto image = lower_module(*ast, *resolved, "placement.ptx");
  ASSERT_FALSE(image.has_value());
  EXPECT_EQ(image.error().code, LoweringDiagnosticCode::malformed_resolved_ir);
  EXPECT_EQ(image.error().function_context, "kernel");
}

TEST(ModuleLowering, RejectsVectorRegisterDeclarationsWithFunctionContext) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { .reg .v2 .u32 pair; }
)ptx");

  ASSERT_FALSE(image.has_value());
  EXPECT_EQ(image.error().code,
            LoweringDiagnosticCode::unsupported_ptx_feature);
  EXPECT_EQ(image.error().function_context, "kernel");
  EXPECT_EQ(image.error().instruction_context, "register declaration");
}

TEST(ModuleLowering, RejectsParameterizedDataAddresses) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.global .u32 value<2>;
.visible .entry kernel() { .reg .u32 %r0; ld.global.u32 %r0, [value0]; }
)ptx");

  ASSERT_FALSE(image.has_value());
  EXPECT_EQ(image.error().code,
            LoweringDiagnosticCode::unsupported_ptx_feature);
  EXPECT_EQ(image.error().function_context, "kernel");
}

}  // namespace
}  // namespace ptxsim::lowering
