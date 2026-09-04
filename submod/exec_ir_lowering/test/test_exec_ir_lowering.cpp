#include <gtest/gtest.h>

#include <limits>
#include <string_view>
#include <utility>

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
  ASSERT_FALSE(other_form);
  EXPECT_EQ(other_form.error().code, LoweringErrorCode::unsupported_operand);
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

  const auto unsupported_predicate = lower(resolve(R"ptx(
.entry kernel() {
  .reg .pred %p<2>;
  mov.pred %p0, %p1;
  exit;
}
)ptx"));
  ASSERT_FALSE(unsupported_predicate);
  EXPECT_EQ(unsupported_predicate.error().code,
            LoweringErrorCode::unsupported_operand);

  const auto unsupported_vector_layout = lower(resolve(R"ptx(
.entry kernel() {
  .reg .v2 .u32 %vector;
  exit;
}
)ptx"));
  ASSERT_FALSE(unsupported_vector_layout);
  EXPECT_EQ(unsupported_vector_layout.error().code,
            LoweringErrorCode::unsupported_type);

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

TEST(ExecIrLowering, LowersScalarMemoryFormsAndRejectsOffsets) {
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
  ASSERT_FALSE(offset);
  EXPECT_EQ(offset.error().code, LoweringErrorCode::unsupported_operand);
}

}  // namespace ptxsim::exec_ir_lowering::test
