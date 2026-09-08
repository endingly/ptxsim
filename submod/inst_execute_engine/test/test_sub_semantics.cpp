#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "../src/semantics/sub_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics::test {
namespace {

using common::RawValue;
using common::RegisterSlot;

/** @brief Make one scalar register-or-immediate operand from exact raw bits. */
auto immediate(std::uint32_t bits) -> exec_ir::ScalarOperand {
  return RawValue::b32(bits);
}

/** @brief Build a dummy wrapping integer form whose fields do not affect arithmetic. */
auto integer_form(exec_ir::DataType type) -> exec_ir::Sub::IntegerNoSat {
  return {type, RegisterSlot{0}, immediate(0), immediate(0)};
}

/** @brief Build a dummy optional-saturation form. */
auto optional_sat_form(exec_ir::DataType type, bool sat)
    -> exec_ir::Sub::OptionalSat {
  return {sat, type, RegisterSlot{0}, immediate(0), immediate(0)};
}

TEST(SubSemantics, IntegerFormsWrapAndOptionalSaturationClamp) {
  const arith::context context;
  EXPECT_EQ(*sub(context, integer_form(exec_ir::DataType::u16),
                 std::uint16_t{0}, std::uint16_t{1}),
            std::numeric_limits<std::uint16_t>::max());
  EXPECT_EQ(*sub(context, integer_form(exec_ir::DataType::u32),
                 std::uint32_t{0}, std::uint32_t{1}),
            std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(*sub(context, integer_form(exec_ir::DataType::u64),
                 std::uint64_t{0}, std::uint64_t{1}),
            std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(*sub(context, integer_form(exec_ir::DataType::s16),
                 std::numeric_limits<std::int16_t>::min(), std::int16_t{1}),
            std::numeric_limits<std::int16_t>::max());
  EXPECT_EQ(*sub(context, integer_form(exec_ir::DataType::s64),
                 std::numeric_limits<std::int64_t>::min(), std::int64_t{1}),
            std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(*sub(context, optional_sat_form(exec_ir::DataType::s32, false),
                 std::numeric_limits<std::int32_t>::min(), std::int32_t{1}),
            std::numeric_limits<std::int32_t>::max());
  EXPECT_EQ(*sub(context, optional_sat_form(exec_ir::DataType::s32, true),
                 std::numeric_limits<std::int32_t>::min(), std::int32_t{1}),
            std::numeric_limits<std::int32_t>::min());
}

TEST(SubSemantics, PackedIntegerLanesNeverBorrowAcrossTheirBoundaries) {
  const arith::context context;
  EXPECT_EQ((*sub(context, optional_sat_form(exec_ir::DataType::u8x4, false),
                  u8x4_t{0x0100'0000U}, u8x4_t{0x0000'0001U}))
                .bits,
            0x0100'00ffU);
  EXPECT_EQ((*sub(context, optional_sat_form(exec_ir::DataType::u8x4, true),
                  u8x4_t{0x0100'0000U}, u8x4_t{0x0000'0001U}))
                .bits,
            0x0100'0000U);
  EXPECT_EQ((*sub(context, optional_sat_form(exec_ir::DataType::s8x4, false),
                  s8x4_t{0x8000'0000U}, s8x4_t{0x0100'0000U}))
                .bits,
            0x7f00'0000U);
  EXPECT_EQ((*sub(context, optional_sat_form(exec_ir::DataType::s8x4, true),
                  s8x4_t{0x8000'0000U}, s8x4_t{0x0100'0000U}))
                .bits,
            0x8000'0000U);
}

TEST(SubSemantics, FloatControlsPreserveRoundingFtzSaturationAndSignedZero) {
  const arith::context context;
  const exec_ir::Sub::FloatF32 nearest{
      exec_ir::RoundingMode::rn, false,        false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Sub::FloatF32 downward{
      exec_ir::RoundingMode::rm, false,        false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Sub::FloatF32 ftz{
      exec_ir::RoundingMode::rn, true,         false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Sub::FloatF32 sat{
      exec_ir::RoundingMode::rn, false,        true,
      RegisterSlot{0},           immediate(0), immediate(0)};
  EXPECT_EQ((*sub(context, nearest, arith::float32_t::from_bits(0x3f80'0000U),
                  arith::float32_t::from_bits(0x3300'0000U)))
                .bits(),
            0x3f80'0000U);
  EXPECT_EQ((*sub(context, downward, arith::float32_t::from_bits(0x3f80'0000U),
                  arith::float32_t::from_bits(0x3300'0000U)))
                .bits(),
            0x3f7f'ffffU);
  EXPECT_EQ((*sub(context, ftz, arith::float32_t::from_bits(0x0000'0001U),
                  arith::float32_t::from_bits(0U)))
                .bits(),
            0U);
  EXPECT_EQ((*sub(context, sat, arith::float32_t::from_bits(0xbf80'0000U),
                  arith::float32_t::from_bits(0U)))
                .bits(),
            0U);
  EXPECT_EQ((*sub(context, nearest, arith::float32_t::from_bits(0x8000'0000U),
                  arith::float32_t::from_bits(0U)))
                .bits(),
            0x8000'0000U);
}

TEST(SubSemantics, FloatingFormsCoverPackedLowPrecisionF64AndMixedPaths) {
  const arith::context context;
  const exec_ir::Sub::FloatF32x2 f32x2{exec_ir::RoundingMode::rn, false,
                                       RegisterSlot{0}, RegisterSlot{1},
                                       RegisterSlot{2}};
  const exec_ir::Sub::FloatF64 f64{exec_ir::RoundingMode::rn, RegisterSlot{0},
                                   immediate(0), immediate(0)};
  const exec_ir::Sub::Half half{exec_ir::RoundingMode::rn,
                                false,
                                false,
                                exec_ir::DataType::f16x2,
                                RegisterSlot{0},
                                RegisterSlot{1},
                                RegisterSlot{2}};
  const exec_ir::Sub::Bfloat bfloat{exec_ir::RoundingMode::rn,
                                    exec_ir::DataType::bf16, RegisterSlot{0},
                                    RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Sub::MixedF32 mixed{
      exec_ir::RoundingMode::rn, false,           exec_ir::DataType::bf16,
      RegisterSlot{0},           RegisterSlot{1}, RegisterSlot{2}};
  EXPECT_EQ((*sub(context, f32x2,
                  arith::float32x2_t::from_bits(0x4000'0000'3f80'0000ULL),
                  arith::float32x2_t::from_bits(0x3f80'0000'4000'0000ULL)))
                .bits(),
            0x3f80'0000'bf80'0000ULL);
  EXPECT_EQ(
      (*sub(context, f64, arith::float64_t::from_bits(0x4000'0000'0000'0000ULL),
            arith::float64_t::from_bits(0x3ff0'0000'0000'0000ULL)))
          .bits(),
      0x3ff0'0000'0000'0000ULL);
  EXPECT_EQ((*sub(context, half, arith::float16x2_t::from_bits(0x4000'3c00U),
                  arith::float16x2_t::from_bits(0x3c00'4000U)))
                .bits(),
            0x3c00'bc00U);
  EXPECT_EQ((*sub(context, bfloat, arith::bfloat16_t::from_bits(0x40a0U),
                  arith::bfloat16_t::from_bits(0x4000U)))
                .bits(),
            0x4040U);
  EXPECT_EQ((*sub(context, mixed, arith::bfloat16_t::from_bits(0x40a0U),
                  arith::float32_t::from_bits(0x4000'0000U)))
                .bits(),
            0x4040'0000U);
}

TEST(SubSemantics, RejectsNonPtxRoundingBeforeLanePreparation) {
  const exec_ir::Sub::FloatF32 invalid{exec_ir::RoundingMode::rzi,
                                       false,
                                       false,
                                       RegisterSlot{0},
                                       immediate(0),
                                       immediate(0)};
  const exec_ir::Sub::Half invalid_half{exec_ir::RoundingMode::rz,
                                        false,
                                        false,
                                        exec_ir::DataType::f16,
                                        RegisterSlot{0},
                                        RegisterSlot{1},
                                        RegisterSlot{2}};
  EXPECT_FALSE(valid_sub(invalid));
  EXPECT_FALSE(valid_sub(invalid_half));
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::detail::semantics::test
