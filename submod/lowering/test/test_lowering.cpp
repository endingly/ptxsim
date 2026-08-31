#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include <ptxsim/bootstrap/thread_bootstrap.hpp>
#include <ptxsim/lowering/lowering.hpp>
#include <ptxsim/state/thread_state.hpp>

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

TEST(ModuleLowering, LowersSingleElementParameterizedRegisters) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .pred %p<1>;
  .reg .u32 %r<1>;
  .reg .s32 %s<1>;
  .reg .s64 %d;
  @%p0 mov.u32 %r0, 1;
  mul.wide.s32 %d, %s0, 2;
}
)ptx");

  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  ASSERT_EQ(image->functions()[0].registers.size(), 4U);
  EXPECT_EQ(image->functions()[0].registers[0].slot.value(), 0U);
  EXPECT_EQ(image->functions()[0].registers[3].slot.value(), 3U);
}

TEST(ModuleLowering, DistinguishesParameterizedOneFromScalarDeclaration) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .u32 %scalar;
  .reg .u32 %parameterized<1>;
  mov.u32 %scalar, 1;
  mov.u32 %parameterized0, 2;
}
)ptx");

  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  ASSERT_EQ(image->functions()[0].registers.size(), 2U);
  EXPECT_EQ(
      std::get<exec_ir::MovInst>(image->instructions()[0]).dest.slot.value(),
      0U);
  EXPECT_EQ(
      std::get<exec_ir::MovInst>(image->instructions()[1]).dest.slot.value(),
      1U);
}

TEST(ModuleLowering, RejectsMissingOrSpuriousParameterizedIndex) {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .u32 %scalar;
  .reg .u32 %parameterized<1>;
  mov.u32 %scalar, 1;
  mov.u32 %parameterized0, 2;
}
)ptx";
  const auto lower_mutated = [&](auto mutate) {
    ptx_frontend::PtxSyntaxParser parser{std::string(source)};
    auto ast = parser.parseModule();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().front().message;
    mutate(*resolved);
    return lower_module(*ast, *resolved, "parameterized-mutation.ptx");
  };
  const auto destination = [](ptx_frontend::resolved_ir::ResolvedModule& module,
                              std::size_t instruction)
      -> ptx_frontend::resolved_ir::ResolvedRegisterRef& {
    auto& mov = std::get<ptx_frontend::resolved_ir::Mov>(
        module.functions[0].body[instruction]);
    auto& scalar =
        std::get<ptx_frontend::resolved_ir::Mov::Scalar>(mov.variant);
    return std::get<ptx_frontend::resolved_ir::Mov::Scalar::ScalarOperands>(
               scalar.operands)
        .dst.value;
  };

  const auto missing = lower_mutated([&](auto& module) {
    destination(module, 1).parameterized_index.reset();
  });
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error().code,
            LoweringDiagnosticCode::malformed_resolved_ir);
  EXPECT_EQ(missing.error().operand_or_control_detail,
            "parameterized register lacks an index");

  const auto spurious = lower_mutated(
      [&](auto& module) { destination(module, 0).parameterized_index = 0; });
  ASSERT_FALSE(spurious.has_value());
  EXPECT_EQ(spurious.error().code,
            LoweringDiagnosticCode::malformed_resolved_ir);
  EXPECT_EQ(spurious.error().operand_or_control_detail,
            "scalar register has an index");
}

TEST(ModuleLowering, RejectsAddressSize32WithoutWidening) {
  const auto symbol = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 32
.global .u32 value;
.visible .entry kernel() { .reg .u32 %r0; ld.global.u32 %r0, [value]; }
)ptx");
  ASSERT_FALSE(symbol.has_value());
  EXPECT_EQ(symbol.error().code,
            LoweringDiagnosticCode::unsupported_ptx_feature);
  EXPECT_EQ(symbol.error().instruction_context, "ld");
  EXPECT_EQ(symbol.error().unsupported_feature, "address size");
  EXPECT_EQ(symbol.error().operand_or_control_detail,
            "M2 supports .address_size 64 only");

  const auto register_base = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 32
.visible .entry kernel() {
  .reg .u32 %r0, %r1;
  ld.global.u32 %r0, [%r1];
}
)ptx");
  ASSERT_FALSE(register_base.has_value());
  EXPECT_EQ(register_base.error().code,
            LoweringDiagnosticCode::unsupported_ptx_feature);
  EXPECT_EQ(register_base.error().instruction_context, "ld");
  EXPECT_EQ(register_base.error().unsupported_feature, "address size");
}

TEST(ModuleLowering, RejectsMissingOrMalformedAddressSizeAtBoundary) {
  ptx_frontend::PtxSyntaxParser parser(
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ast->items.erase(ast->items.begin() + 2);

  const auto missing = lower_module(*ast, *resolved, "missing.ptx");
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error().code,
            LoweringDiagnosticCode::malformed_resolved_ir);
  EXPECT_EQ(missing.error().instruction_context, "module");
  EXPECT_EQ(missing.error().unsupported_feature, "address size");
  EXPECT_EQ(missing.error().operand_or_control_detail,
            "missing .address_size directive");

  ptx_frontend::PtxSyntaxParser malformed_parser(
      ".version 8.0\n.target sm_80\n.address_size 64\n"
      ".visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }");
  auto malformed_ast = malformed_parser.parseModule();
  ASSERT_TRUE(malformed_ast.has_value())
      << malformed_ast.diagnostics.front().message;
  const auto malformed_resolved =
      ptx_frontend::resolved_ir::resolveModule(*malformed_ast);
  ASSERT_TRUE(malformed_resolved.has_value())
      << malformed_resolved.error().front().message;
  auto* directive =
      std::get_if<ptx_frontend::syntax_ast::AstAddressSizeDirective>(
          &malformed_ast->items[2]);
  ASSERT_NE(directive, nullptr);
  directive->bit_width.text = "16";

  const auto malformed =
      lower_module(*malformed_ast, *malformed_resolved, "malformed.ptx");
  ASSERT_FALSE(malformed.has_value());
  EXPECT_EQ(malformed.error().code,
            LoweringDiagnosticCode::malformed_resolved_ir);
  EXPECT_EQ(malformed.error().instruction_context, "module");
  EXPECT_EQ(malformed.error().unsupported_feature, "address size");
  EXPECT_EQ(malformed.error().operand_or_control_detail,
            "invalid .address_size value");
}

TEST(ModuleLowering, LowersAddressSize64MemoryOperands) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.global .u32 value;
.visible .entry kernel() {
  .reg .u32 %r0;
  .reg .u64 %rd0;
  ld.global.u32 %r0, [value];
  ld.global.u32 %r0, [%rd0];
}
)ptx");
  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  EXPECT_EQ(std::get<exec_ir::LoadInst>(image->instructions()[0]).address.width,
            exec_ir::AddressWidth::bits64);
  EXPECT_EQ(std::get<exec_ir::LoadInst>(image->instructions()[1]).address.width,
            exec_ir::AddressWidth::bits64);
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

TEST(ModuleLowering, RejectsOutOfRangeFrontendIdentitiesWithoutThrowing) {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() { .reg .u32 %r0; mov.u32 %r0, 1; }
)ptx";
  const auto lower_mutated = [&](auto mutate) {
    ptx_frontend::PtxSyntaxParser parser{std::string(source)};
    auto ast = parser.parseModule();
    EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
    EXPECT_TRUE(resolved.has_value()) << resolved.error().front().message;
    mutate(*resolved);
    return lower_module(*ast, *resolved, "identity-mutation.ptx");
  };
  const auto expect_malformed = [](const auto& image) {
    ASSERT_FALSE(image.has_value());
    EXPECT_EQ(image.error().code,
              LoweringDiagnosticCode::malformed_resolved_ir);
  };

  EXPECT_NO_THROW({
    const auto image = lower_mutated([](auto& module) {
      module.functions[0].symbol_id = ptx_frontend::binding::SymbolId{
          std::numeric_limits<std::uint32_t>::max()};
    });
    expect_malformed(image);
  });
  EXPECT_NO_THROW({
    const auto image = lower_mutated([](auto& module) {
      const auto function_symbol = module.functions[0].symbol_id;
      const auto function_scope =
          module.symbols.symbols()[function_symbol.value].owned_scope;
      ASSERT_TRUE(function_scope.has_value());
      auto& scopes = const_cast<std::vector<ptx_frontend::binding::Scope>&>(
          module.symbols.scopes());
      scopes[function_scope->value].parent = ptx_frontend::binding::ScopeId{
          std::numeric_limits<std::uint32_t>::max()};
    });
    expect_malformed(image);
  });
  EXPECT_NO_THROW({
    const auto image = lower_mutated([](auto& module) {
      auto& mov =
          std::get<ptx_frontend::resolved_ir::Mov>(module.functions[0].body[0]);
      auto& scalar =
          std::get<ptx_frontend::resolved_ir::Mov::Scalar>(mov.variant);
      auto& destination =
          std::get<ptx_frontend::resolved_ir::Mov::Scalar::ScalarOperands>(
              scalar.operands)
              .dst.value;
      destination.symbol_id = ptx_frontend::binding::SymbolId{
          std::numeric_limits<std::uint32_t>::max()};
    });
    expect_malformed(image);
  });
}

TEST(ModuleLowering, PreservesProgramVerifierFailureDetails) {
  const auto image = lower_source(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  bra end;
end:
}
)ptx");

  ASSERT_FALSE(image.has_value());
  EXPECT_EQ(image.error().code,
            LoweringDiagnosticCode::lowering_invariant_violation);
  EXPECT_EQ(image.error().function_context, "kernel");
  ASSERT_TRUE(image.error().source_location.has_value());
  EXPECT_EQ(image.error().source_location->file, "lowering-test.ptx");
  ASSERT_TRUE(image.error().operand_or_control_detail.has_value());
  EXPECT_NE(image.error().operand_or_control_detail->find(
                "code=branch_target_out_of_range"),
            std::string::npos);
  EXPECT_NE(image.error().operand_or_control_detail->find("pc=0"),
            std::string::npos);
  EXPECT_NE(image.error().operand_or_control_detail->find("index=1"),
            std::string::npos);
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

TEST(ModuleLowering, LoweredImageOutlivesFrontendAndInitializesThreadState) {
  std::optional<program::ProgramImage> image;
  {
    std::string source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.global .u32 value;
.visible .entry kernel() {
  .reg .pred %p;
  .reg .u32 %r0;
  ld.global.u32 %r0, [value];
  loop:
  @!%p bra loop;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    auto lowered = lower_module(*ast, *resolved, "lifetime.ptx");
    ASSERT_TRUE(lowered.has_value()) << to_string(lowered.error());
    image.emplace(std::move(*lowered));
  }

  ASSERT_TRUE(image.has_value());
  EXPECT_TRUE(program::verify(*image).has_value());
  EXPECT_FALSE(image->instructions().empty());
  EXPECT_FALSE(program::dump(*image).empty());
  ASSERT_EQ(image->functions().size(), 1U);
  ASSERT_EQ(image->entry_points().size(), 1U);
  const auto& entry = image->functions()[image->entry_points()[0].value()];
  EXPECT_EQ(entry.id, image->entry_points()[0]);
  EXPECT_EQ(entry.begin_pc.value(), 0U);
  EXPECT_EQ(entry.end_pc.value(), image->instructions().size());
  EXPECT_EQ(image->source_locations_by_pc().size(),
            image->instructions().size());
  EXPECT_EQ(image->source_locations()[0].file, "lifetime.ptx");
  ASSERT_EQ(image->symbols().size(), 1U);
  const auto& branch =
      std::get<exec_ir::BranchInst>(image->instructions().back());
  EXPECT_EQ(branch.target.pc.value(), 1U);

  const auto thread =
      bootstrap::create_entry_thread(*image, common::ThreadId{7}, entry.id);
  ASSERT_TRUE(thread.has_value());
  EXPECT_EQ(thread->status(), state::ThreadStatus::ready);
  EXPECT_EQ(thread->current_function(), entry.id);
  EXPECT_EQ(thread->current_pc(), entry.begin_pc);
  EXPECT_EQ(thread->registers().size(), entry.registers.size());
  ASSERT_FALSE(thread->registers().read(common::RegisterSlot{0}).has_value());
  EXPECT_EQ(thread->registers().read(common::RegisterSlot{0}).error().code,
            state::RegisterErrorCode::uninitialized_read);
}

}  // namespace
}  // namespace ptxsim::lowering
