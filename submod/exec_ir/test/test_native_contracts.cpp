#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

#include <ptxsim/exec_ir/instruction_base.gen.hpp>
#include <ptxsim/exec_ir/form_contracts.gen.hpp>

namespace ptxsim::exec_ir::test {
namespace {
TEST(NativeContractSemantics, SignedVectorImmediateBoundsDoNotPromoteNegativeSources) {
  const contract::SignedVectorImmediateRange range{-8, 7};
  static_assert(std::same_as<decltype(range.minimum), std::int64_t>);
  static_assert(std::same_as<decltype(range.maximum), std::int64_t>);
  for (const std::int64_t value : {-8, -1, 0, 7})
    EXPECT_TRUE(range.minimum <= value && value <= range.maximum);
  for (const std::int64_t value : {-9, 8})
    EXPECT_FALSE(range.minimum <= value && value <= range.maximum);
}

TEST(NativeContractSemantics, EveryExactFormHasCompleteOwnedLayoutsAndBindings) {
  std::size_t layouts = 0;
  std::size_t bindings = 0;
  std::size_t signed_ranges = 0;
  for (std::size_t index = 0; index < instruction_kind_count; ++index) {
    const auto* descriptor = form_contract(static_cast<Kind>(index));
    ASSERT_NE(descriptor, nullptr);
    ASSERT_FALSE(descriptor->layouts.empty());
    layouts += descriptor->layouts.size();
    for (const auto& layout : descriptor->layouts) {
      for (const auto& field : layout.fields) {
        ASSERT_TRUE(field.binding.has_value());
        ++bindings;
        const auto& target = field.binding->target_field_id;
        EXPECT_EQ(target.role, field.field);
        EXPECT_EQ(target.scope, contract::FieldReferenceScope::SelectedLayoutOperand);
        EXPECT_EQ(target.layouts.size(), 1);
        if (field.binding->vector_signed_immediate_range) {
          ++signed_ranges;
          const auto& range = *field.binding->vector_signed_immediate_range;
          EXPECT_LT(range.minimum, 0);
          EXPECT_GE(range.maximum, 0);
          EXPECT_TRUE(range.minimum <= -1 && -1 <= range.maximum);
        }
      }
    }
  }
  EXPECT_EQ(layouts, 7457);
  EXPECT_EQ(bindings, 31154);
  EXPECT_GT(signed_ranges, 0);
  EXPECT_EQ(form_contract(static_cast<Kind>(instruction_kind_count)), nullptr);
}
}  // namespace
}  // namespace ptxsim::exec_ir::test
