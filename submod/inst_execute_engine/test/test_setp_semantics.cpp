#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "../src/semantics/setp_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics::test {
namespace {

TEST(SetpSemantics, ComparesUnsignedAndSignedBoundaryValues) {
  const arith::context context;
  EXPECT_TRUE(*compare(context, exec_ir::ComparisonOperator::lt,
                       std::uint32_t{0},
                       std::numeric_limits<std::uint32_t>::max()));
  EXPECT_FALSE(*compare(context, exec_ir::ComparisonOperator::lt,
                        std::numeric_limits<std::uint32_t>::max(),
                        std::uint32_t{0}));
  EXPECT_TRUE(*compare(context, exec_ir::ComparisonOperator::ge,
                       std::numeric_limits<std::int32_t>::min(),
                       std::numeric_limits<std::int32_t>::min()));
  EXPECT_TRUE(*compare(context, exec_ir::ComparisonOperator::ge,
                       std::numeric_limits<std::int32_t>::max(),
                       std::int32_t{-1}));
  EXPECT_TRUE(*compare(context, exec_ir::ComparisonOperator::eq,
                       std::uint32_t{7}, std::uint32_t{7}));
}

TEST(SetpSemantics, CombinesEveryBooleanTruthTableEntry) {
  EXPECT_FALSE(*combine(exec_ir::BooleanOperator::and_, false, false));
  EXPECT_FALSE(*combine(exec_ir::BooleanOperator::and_, false, true));
  EXPECT_FALSE(*combine(exec_ir::BooleanOperator::and_, true, false));
  EXPECT_TRUE(*combine(exec_ir::BooleanOperator::and_, true, true));
  EXPECT_FALSE(*combine(exec_ir::BooleanOperator::or_, false, false));
  EXPECT_TRUE(*combine(exec_ir::BooleanOperator::or_, false, true));
  EXPECT_TRUE(*combine(exec_ir::BooleanOperator::or_, true, false));
  EXPECT_TRUE(*combine(exec_ir::BooleanOperator::or_, true, true));
  EXPECT_FALSE(*combine(exec_ir::BooleanOperator::xor_, false, false));
  EXPECT_TRUE(*combine(exec_ir::BooleanOperator::xor_, false, true));
  EXPECT_TRUE(*combine(exec_ir::BooleanOperator::xor_, true, false));
  EXPECT_FALSE(*combine(exec_ir::BooleanOperator::xor_, true, true));
}

TEST(SetpSemantics, RejectsUnknownControls) {
  const arith::context context;
  EXPECT_FALSE(compare(context, static_cast<exec_ir::ComparisonOperator>(99),
                       std::uint32_t{0}, std::uint32_t{0}));
  EXPECT_FALSE(
      combine(static_cast<exec_ir::BooleanOperator>(99), false, false));
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::detail::semantics::test
