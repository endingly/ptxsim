#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.gen.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptxsim::exec_ir::test {
namespace {

using common::CodeLocation;
using common::FunctionId;
using common::ProgramCounter;
using common::RawValue;
using common::RawWidth;
using common::RegisterSlot;

static_assert(std::variant_size_v<Instruction> ==
              static_cast<std::size_t>(Op::count));
static_assert(InstructionAlternative<Add>);
static_assert(InstructionAlternative<And>);
static_assert(Add::opcode == Op::add);
static_assert(And::opcode == Op::and_);

/** @brief Build a generated scalar move instruction. */
auto mov(std::optional<Predicate> predicate, DataType type, RegisterSlot dst,
         RegisterSlot src) -> Instruction {
  Mov::Scalar::ScalarOperands operands{dst, src};
  Mov::Scalar form{type, operands};
  return Mov{std::move(predicate), Mov::Variant{form}};
}

/** @brief Build a generated integer add instruction. */
auto add(std::optional<Predicate> predicate, DataType type, RegisterSlot dst,
         B32Operand lhs, B32Operand rhs) -> Instruction {
  Add::IntegerNoSat form{type, dst, std::move(lhs), std::move(rhs)};
  return Add{std::move(predicate), Add::Variant{form}};
}

/** @brief Build a generated scalar load instruction. */
auto load(std::optional<Predicate> predicate, DataType type, AddressSpace space,
          RegisterSlot dst, RegisterSlot address) -> Instruction {
  if (space == AddressSpace::generic) {
    Ld::GenericScalar form{MemoryConsistency::omitted,
                           MemoryScope::none,
                           false,
                           CacheOperator::unspecified,
                           type,
                           dst,
                           address};
    return Ld{std::move(predicate), Ld::Variant{form}};
  }
  Ld::ExplicitScalar form{space,
                          CacheOperator::unspecified,
                          MemoryConsistency::omitted,
                          MemoryScope::none,
                          false,
                          type,
                          dst,
                          address};
  return Ld{std::move(predicate), Ld::Variant{form}};
}

/** @brief Build a generated scalar store instruction. */
auto store(std::optional<Predicate> predicate, DataType type,
           AddressSpace space, RegisterSlot address, RegisterSlot src)
    -> Instruction {
  if (space == AddressSpace::generic) {
    St::GenericScalar form{MemoryConsistency::omitted,
                           MemoryScope::none,
                           false,
                           CacheOperator::unspecified,
                           type,
                           address,
                           src};
    return St{std::move(predicate), St::Variant{form}};
  }
  St::ExplicitScalar form{space,
                          CacheOperator::unspecified,
                          MemoryConsistency::omitted,
                          MemoryScope::none,
                          false,
                          type,
                          address,
                          src};
  return St{std::move(predicate), St::Variant{form}};
}

/** @brief Build a generated warp-synchronization instruction. */
auto bar(std::optional<Predicate> predicate, B32Operand mask) -> Instruction {
  Bar::WarpSync form{std::move(mask)};
  return Bar{std::move(predicate), Bar::Variant{form}};
}

/** @brief Build a generated direct branch instruction. */
auto bra(std::optional<Predicate> predicate, ProgramCounter target)
    -> Instruction {
  Bra::Direct form{false, target};
  return Bra{std::move(predicate), Bra::Variant{form}};
}

/** @brief Build a generated exit instruction. */
auto exit(std::optional<Predicate> predicate = std::nullopt) -> Instruction {
  return Exit{std::move(predicate), Exit::Variant{Exit::Bare{}}};
}

auto valid_definition() -> ProgramDefinition {
  return {
      .instructions = {mov(std::nullopt, DataType::b32, RegisterSlot{1},
                           RegisterSlot{0}),
                       add(Predicate{RegisterSlot{2}, true}, DataType::u32,
                           RegisterSlot{1}, B32Operand{RegisterSlot{1}},
                           B32Operand{RawValue::b32(1U)}),
                       bra(std::nullopt, ProgramCounter{0}), exit()},
      .functions = {{FunctionId{0},
                     0,
                     3,
                     {RawWidth::b32, RawWidth::b32, RawWidth::pred}},
                    {FunctionId{1}, 3, 1, {}}},
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
  EXPECT_TRUE(std::holds_alternative<Mov>(first->get()));
  EXPECT_TRUE(std::holds_alternative<Exit>(second->get()));
  EXPECT_EQ(op(first->get()), Op::mov);
  EXPECT_EQ(op(second->get()), Op::exit);
}
}  // namespace

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

  auto invalid_width = valid_definition();
  invalid_width.functions[0].register_widths[0] = static_cast<RawWidth>(99);
  const auto width_result = ExecutableProgram::create(std::move(invalid_width));
  ASSERT_FALSE(width_result);
  EXPECT_EQ(width_result.error().code,
            ProgramErrorCode::invalid_register_width);
  EXPECT_EQ(width_result.error().actual, static_cast<RawWidth>(99));
}

TEST(ExecutableProgram, PrintsCanonicalExecutableProgram) {
  const auto program = make_program();

  EXPECT_EQ(to_string(program),
            "gpc0  [func:0 pc:0]  "
            "mov.b32 register:1, register:0\n"
            "gpc1  [func:0 pc:1]  "
            "@!predicate:2 add.u32 register:1, register:1, b32:0x00000001\n"
            "gpc2  [func:0 pc:2]  "
            "bra pc:0\n"
            "gpc3  [func:1 pc:0]  "
            "exit");
}

TEST(ExecutableProgram, PrintsScalarLoadStore) {
  const auto program = ExecutableProgram::create({
      .instructions = {load(std::nullopt, DataType::u32, AddressSpace::generic,
                            RegisterSlot{0}, RegisterSlot{1}),
                       store(std::nullopt, DataType::u32, AddressSpace::global,
                             RegisterSlot{1}, RegisterSlot{0}),
                       exit()},
      .functions = {{FunctionId{0}, 0, 3, {RawWidth::b32, RawWidth::b64}}},
  });
  ASSERT_TRUE(program);
  EXPECT_EQ(to_string(*program),
            "gpc0  [func:0 pc:0]  "
            "ld.u32 register:0, [register:1]\n"
            "gpc1  [func:0 pc:1]  "
            "st.global.u32 [register:1], register:0\n"
            "gpc2  [func:0 pc:2]  "
            "exit");
}

TEST(ExecutableProgram, PrintsWarpSync) {
  const auto program = ExecutableProgram::create({
      .instructions = {bar(std::nullopt, B32Operand{RawValue::b32(3U)}),
                       exit()},
      .functions = {{FunctionId{0}, 0, 2, {RawWidth::b32}}},
  });
  ASSERT_TRUE(program);
  EXPECT_EQ(op(program->fetch({FunctionId{0}, ProgramCounter{0}})->get()),
            Op::bar);
  EXPECT_EQ(to_string(*program),
            "gpc0  [func:0 pc:0]  "
            "bar.warp.sync b32:0x00000003\n"
            "gpc1  [func:0 pc:1]  "
            "exit");
}

TEST(ExecutableProgram, AcceptsAndPrintsDeclarationOnlyOpcode) {
  const Sub::IntegerNoSat form{DataType::u32, RegisterSlot{0},
                               ScalarOperand{RegisterSlot{1}},
                               ScalarOperand{RegisterSlot{2}}};
  const auto program = ExecutableProgram::create({
      .instructions = {Sub{Predicate{RegisterSlot{3}}, Sub::Variant{form}}},
      .functions = {{FunctionId{0},
                     0,
                     1,
                     {RawWidth::b32, RawWidth::b32, RawWidth::b32,
                      RawWidth::pred}}},
  });
  ASSERT_TRUE(program);
  const auto instruction = program->fetch({FunctionId{0}, ProgramCounter{0}});
  ASSERT_TRUE(instruction);
  EXPECT_EQ(op(instruction->get()), Op::sub);
  EXPECT_EQ(to_string(*program),
            "gpc0  [func:0 pc:0]  "
            "@predicate:3 sub.u32 register:0, register:1, register:2");
}

TEST(InstructionDiagnostic, FormatsDeclarationOnlyOpcodeWithoutValidation) {
  const Sub::IntegerNoSat form{DataType::u32, RegisterSlot{0},
                               ScalarOperand{RegisterSlot{1}},
                               ScalarOperand{RawValue::b32(1U)}};
  EXPECT_EQ(to_string(Instruction{Sub{std::nullopt, Sub::Variant{form}}}),
            "sub.u32 register:0, register:1, b32:0x00000001");
  EXPECT_EQ(to_string(bar(std::nullopt, B32Operand{RawValue::b32(3U)})),
            "bar.warp.sync b32:0x00000003");
}

TEST(InstructionDiagnostic, FormatsStableModifierAliases) {
  EXPECT_EQ(to_string(Predicate{RegisterSlot{3}, false}), "predicate:3");
  EXPECT_EQ(to_string(Predicate{RegisterSlot{3}, true}), "!predicate:3");
  EXPECT_EQ(to_string(BooleanOperator::and_), "and");
  EXPECT_EQ(to_string(MemoryConsistency::volatile_), "volatile");
  EXPECT_EQ(to_string(AddressSpace::const_), "const");
  EXPECT_EQ(to_string(AddressSpace::param_entry), "param::entry");
  EXPECT_EQ(to_string(AddressSpace::param_func), "param::func");
  EXPECT_EQ(to_string(MbarrierPhaseType::conditional),
            "phase_type::conditional");
  EXPECT_EQ(to_string(MbarrierPhaseType::primary), "phase_type::primary");
  EXPECT_EQ(to_string(MbarrierLayout::v0), "layout::v0");
  EXPECT_EQ(to_string(MbarrierLayout::v1), "layout::v1");
  EXPECT_EQ(to_string(AsyncProxyKind::async_shared_cta), "async.shared::cta");
  EXPECT_EQ(to_string(ProxyKindPair::async_generic), "async::generic");
  EXPECT_EQ(to_string(static_cast<DataType>(255)), "<invalid>");
}

}  // namespace ptxsim::exec_ir::test
