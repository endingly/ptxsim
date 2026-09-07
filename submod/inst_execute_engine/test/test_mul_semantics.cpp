#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "../src/semantics/mul_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics::test {
namespace {

using common::RegisterSlot;

/** @brief Build a dummy fixed-rounding f32 form whose operand fields are unused. */
auto f32_form() -> exec_ir::Mul::RnF32 {
  return {RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2}};
}

/** @brief Build a dummy low unsigned form whose operand fields are unused. */
auto low_form() -> exec_ir::Mul::LoU32 {
  return {RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2}};
}

/** @brief Build a dummy high unsigned form whose operand fields are unused. */
auto high_form() -> exec_ir::Mul::HiU32 {
  return {RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2}};
}

/** @brief Build a dummy wide unsigned form whose operand fields are unused. */
auto wide_unsigned_form() -> exec_ir::Mul::WideU32 {
  return {RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2}};
}

/** @brief Build a dummy wide signed form whose operand fields are unused. */
auto wide_signed_form() -> exec_ir::Mul::WideS32 {
  return {RegisterSlot{0}, RegisterSlot{1}, RegisterSlot{2}};
}

TEST(MulSemantics, F32UsesProjectedRoundToNearestControl) {
  const arith::context context;
  const auto form = f32_form();
  const auto product = mul(context, form, arith::float32_t::from_bits(0x3fc00000U),
                           arith::float32_t::from_bits(0x40000000U));

  ASSERT_TRUE(product);
  EXPECT_EQ(product->bits(), 0x40400000U);
  EXPECT_TRUE(valid_mul(form));
}

TEST(MulSemantics, IntegerProductPartsPreserveBoundaryBits) {
  const arith::context context;
  const auto maximum = std::numeric_limits<std::uint32_t>::max();

  EXPECT_EQ(*mul(context, low_form(), maximum, std::uint32_t{2}),
            0xfffffffeU);
  EXPECT_EQ(*mul(context, high_form(), maximum, std::uint32_t{2}),
            std::uint32_t{1});
  EXPECT_EQ(*mul(context, wide_unsigned_form(), maximum, maximum),
            0xfffffffe00000001ULL);
  EXPECT_EQ(*mul(context, wide_signed_form(),
                 std::numeric_limits<std::int32_t>::min(), std::int32_t{-1}),
            std::int64_t{2147483648});
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::detail::semantics::test
