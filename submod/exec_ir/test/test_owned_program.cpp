#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/exec_ir/model/data_movement/mov.gen.hpp>

namespace ptxsim::exec_ir::test {
namespace {
/** @brief Construct one typed scalar slot view for an owned input graph. */
[[nodiscard]] auto reg(std::uint32_t slot, DataType type) -> RegisterRef {
  return {.slot = common::RegisterSlot{slot}, .declared_type = type};
}

/** @brief Build one real definition and one independent body-less callable. */
[[nodiscard]] auto definition() -> ProgramDefinition {
  ProgramDefinition input;
  input.function_declarations = {
      {.id = FunctionDeclarationId{0}, .function = common::FunctionId{0},
       .name = "body"},
      {.id = FunctionDeclarationId{1}, .function = common::FunctionId{1},
       .name = "external", .is_prototype = true}};
  input.functions = {
      {.id = common::FunctionId{0}, .name = "body",
       .declarations = {FunctionDeclarationId{0}},
       .definition = FunctionDeclarationId{0},
       .body = FunctionLayout{.id = common::FunctionId{0}, .instruction_count = 1,
                              .register_widths = {common::RawWidth::b64,
                                                  common::RawWidth::b32,
                                                  common::RawWidth::b32}}},
      {.id = common::FunctionId{1}, .name = "external",
       .declarations = {FunctionDeclarationId{1}}}};
  input.instructions.push_back(std::make_unique<MovScalar>(
      MovScalar::PackOperands{reg(0, DataType::b64),
                              RegisterVector{{reg(1, DataType::b32),
                                              reg(2, DataType::b32)}}},
      MovScalar::Controls{DataType::b64}));
  input.call_target_sets = {{.id = CallTargetSetId{0},
                             .owner_declaration = FunctionDeclarationId{0},
                             .targets = {common::FunctionId{1}, common::FunctionId{1}}}};
  input.aliases = {{"external_alias", common::FunctionId{1}}};
  input.source_profiles = {{.target_options = {"sm_90", "map_f64_to_f32"}}};
  return input;
}

TEST(OwnedProgram, FreezeCopiesRetainedMutableAliasesAndNestedMetadata) {
  auto input = definition();
  auto* instruction = static_cast<MovScalar*>(input.instructions.front().get());
  auto* nested = &std::get<MovScalar::PackOperands>(instruction->operands).src.elements[0];
  auto& targets = input.call_target_sets.front().targets;
  auto& profile = input.source_profiles.front().target_options.front();
  const auto program = ExecutableProgram::create(input);
  ASSERT_TRUE(program.has_value());
  const common::CodeLocation location{common::FunctionId{0}, common::ProgramCounter{0}};
  const auto before = program->fetch(location);
  ASSERT_TRUE(before.has_value());
  const auto rendered = to_string(before->get());

  instruction->type = DataType::b32;
  (*nested)->slot = common::RegisterSlot{99};
  targets[0] = common::FunctionId{0};
  profile = "changed";
  input.functions.front().name = "changed";

  const auto* frozen = before->get().get_if<MovScalar>();
  ASSERT_NE(frozen, nullptr);
  EXPECT_NE(frozen, instruction);
  EXPECT_EQ(frozen->type, DataType::b64);
  const auto& elements = std::get<MovScalar::PackOperands>(frozen->operands).src.elements;
  EXPECT_NE(&elements.front(), nested);
  ASSERT_TRUE(elements.front().has_value());
  EXPECT_EQ(elements.front()->slot, common::RegisterSlot{1});
  EXPECT_EQ(program->call_target_sets().front().targets.front(), common::FunctionId{1});
  EXPECT_EQ(program->source_profiles().front().target_options.front(), "sm_90");
  EXPECT_EQ(program->functions().front().name, "body");
  EXPECT_EQ(to_string(before->get()), rendered);
}

TEST(OwnedProgram, SourceDeathAndProgramCopiesRetainOneConstSnapshot) {
  const auto program = [] {
    auto input = definition();
    return ExecutableProgram::create(input);
  }();
  ASSERT_TRUE(program.has_value());
  const auto copy = *program;
  const common::CodeLocation location{common::FunctionId{0}, common::ProgramCounter{0}};
  const auto left = program->fetch(location);
  const auto right = copy.fetch(location);
  ASSERT_TRUE(left.has_value());
  ASSERT_TRUE(right.has_value());
  EXPECT_EQ(&left->get(), &right->get());
  EXPECT_EQ(program->functions().data(), copy.functions().data());
  EXPECT_TRUE(structurally_valid(right->get()));
  EXPECT_FALSE(to_string(copy).empty());
  static_assert(std::is_const_v<std::remove_reference_t<decltype(right->get())>>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(copy.functions().front())>>);
}

TEST(OwnedProgram, BodylessCallableHasIdentityAndStructuredFetchFailure) {
  const auto program = ExecutableProgram::create(definition());
  ASSERT_TRUE(program.has_value());
  const auto external = program->function_record(common::FunctionId{1});
  ASSERT_TRUE(external.has_value());
  EXPECT_FALSE(external->get().body.has_value());
  EXPECT_EQ(program->aliases().front().function, external->get().id);
  const auto fetch = program->fetch({common::FunctionId{1}, common::ProgramCounter{0}});
  ASSERT_FALSE(fetch.has_value());
  EXPECT_EQ(fetch.error().code, ProgramErrorCode::nonexecutable_function);
}
}  // namespace
}  // namespace ptxsim::exec_ir::test
