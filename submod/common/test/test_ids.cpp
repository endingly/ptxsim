#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include <ptxsim/common/ids.hpp>

namespace ptxsim::common::test {
namespace {

template <typename Id>
void check_id(std::string_view prefix) {
  const Id first{7};
  const Id second{9};
  EXPECT_EQ(first.value(), 7u);
  EXPECT_EQ(first, Id{7});
  EXPECT_NE(first, second);
  EXPECT_LT(first, second);
  EXPECT_EQ(to_string(first), std::string{prefix} + ":7");
}

template <typename Id>
constexpr void check_properties() {
  static_assert(std::is_trivially_copyable_v<Id>);
  static_assert(std::is_constructible_v<Id, std::uint32_t>);
  static_assert(!std::is_convertible_v<std::uint32_t, Id>);
}

TEST(CoreIds, ConstructCompareAndFormat) {
  check_properties<ProgramCounter>();
  check_properties<FunctionId>();
  check_properties<RegisterSlot>();
  check_properties<SymbolId>();
  check_properties<LabelId>();
  check_properties<SourceLocationId>();
  check_properties<SpecialRegisterId>();

  static_assert(!std::is_constructible_v<FunctionId, ProgramCounter>);
  static_assert(!std::is_convertible_v<ProgramCounter, FunctionId>);

  check_id<ProgramCounter>("pc");
  check_id<FunctionId>("function");
  check_id<RegisterSlot>("register");
  check_id<SymbolId>("symbol");
  check_id<LabelId>("label");
  check_id<SourceLocationId>("source");
  check_id<SpecialRegisterId>("special-register");

  EXPECT_EQ(
      to_string(ProgramCounter{std::numeric_limits<std::uint32_t>::max()}),
      "pc:4294967295");
}

}  // namespace
}  // namespace ptxsim::common::test
