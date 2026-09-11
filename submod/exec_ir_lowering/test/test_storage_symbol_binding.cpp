#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

namespace ptxsim::exec_ir_lowering::test {
namespace {

/** @brief Resolve a small symbol-address program for mutation validation. */
auto storage_module() -> ptx_frontend::resolved_ir::ResolvedModule {
  ptx_frontend::PtxSyntaxParser parser(R"ptx(
.global .align 8 .u32 data;
.entry kernel() {
  .reg .u64 %pointer;
  mov.u64 %pointer, data;
  exit;
}
)ptx");
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast);
  const auto module = ptx_frontend::resolved_ir::resolveModule(*ast);
  EXPECT_TRUE(module);
  return module ? std::move(*module)
                : ptx_frontend::resolved_ir::ResolvedModule{};
}

/** @brief Return the mutable resolved symbol source of the first MOV instruction. */
auto mov_symbol(ptx_frontend::resolved_ir::ResolvedModule& module)
    -> ptx_frontend::resolved_ir::ResolvedSymbolRef& {
  auto& mov =
      std::get<ptx_frontend::resolved_ir::Mov>(module.functions[0].body[0]);
  auto& form = std::get<ptx_frontend::resolved_ir::Mov::Scalar>(mov.variant);
  auto& operands =
      std::get<ptx_frontend::resolved_ir::Mov::Scalar::ScalarOperands>(
          form.operands);
  return std::get<ptx_frontend::resolved_ir::ResolvedSymbolRef>(
      operands.src.value);
}

}  // namespace

TEST(StorageSymbolBinding, RejectsUnknownAndRegisterSymbolIdentities) {
  auto unknown = storage_module();
  mov_symbol(unknown).symbol_id = ptx_frontend::binding::SymbolId{99999};
  const auto unknown_result = lower(unknown);
  ASSERT_FALSE(unknown_result);
  EXPECT_EQ(unknown_result.error().code,
            LoweringErrorCode::malformed_resolved_ir);

  auto register_identity = storage_module();
  const auto register_symbol = register_identity.functions[0].symbol_id;
  mov_symbol(register_identity).symbol_id = register_symbol;
  const auto register_result = lower(register_identity);
  ASSERT_FALSE(register_result);
  EXPECT_EQ(register_result.error().code,
            LoweringErrorCode::malformed_resolved_ir);
}

}  // namespace ptxsim::exec_ir_lowering::test
