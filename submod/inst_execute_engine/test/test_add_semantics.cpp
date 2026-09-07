#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "../src/semantics/add_semantics.hpp"

namespace ptxsim::inst_execute_engine::detail::semantics::test {
namespace {

using common::RawValue;
using common::RegisterSlot;

/** @brief Make one scalar register-or-immediate operand from exact raw bits. */
auto immediate(std::uint32_t bits) -> exec_ir::ScalarOperand {
  return RawValue::b32(bits);
}

/** @brief Build a dummy integer form whose fields do not affect pure arithmetic. */
auto integer_form(exec_ir::DataType type) -> exec_ir::Add::IntegerNoSat {
  return {type, RegisterSlot{0}, immediate(0), immediate(0)};
}

/** @brief Build a dummy saturating integer form. */
auto sat_form(exec_ir::DataType type) -> exec_ir::Add::Sat {
  return {type, RegisterSlot{0}, immediate(0), immediate(0)};
}

/** @brief Build a dummy packed integer form with the requested saturation bit. */
auto packed_form(exec_ir::DataType type, bool sat)
    -> exec_ir::Add::PackedOptionalSat {
  return {sat, type, RegisterSlot{0}, immediate(0), immediate(0)};
}

/** @brief Check that one codec is a lossless architectural bit round-trip. */
template <ValueCodec T>
void expect_codec_round_trip(common::RawValue raw) {
  const auto decoded = value_codec<T>::decode(raw);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(value_codec<T>::encode(*decoded), raw);
}

TEST(AddSemantics, IntegerFormsWrapAndSaturateAtEveryDeclaredWidth) {
  const arith::context context;
  EXPECT_EQ(*add(context, integer_form(exec_ir::DataType::u16),
                 std::numeric_limits<std::uint16_t>::max(), std::uint16_t{1}),
            std::uint16_t{0});
  EXPECT_EQ(*add(context, integer_form(exec_ir::DataType::u32),
                 std::numeric_limits<std::uint32_t>::max(), std::uint32_t{1}),
            std::uint32_t{0});
  EXPECT_EQ(*add(context, integer_form(exec_ir::DataType::u64),
                 std::numeric_limits<std::uint64_t>::max(), std::uint64_t{1}),
            std::uint64_t{0});
  EXPECT_EQ(*add(context, integer_form(exec_ir::DataType::s16),
                 std::numeric_limits<std::int16_t>::max(), std::int16_t{1}),
            std::numeric_limits<std::int16_t>::min());
  EXPECT_EQ(*add(context, integer_form(exec_ir::DataType::s32),
                 std::numeric_limits<std::int32_t>::max(), std::int32_t{1}),
            std::numeric_limits<std::int32_t>::min());
  EXPECT_EQ(*add(context, integer_form(exec_ir::DataType::s64),
                 std::numeric_limits<std::int64_t>::max(), std::int64_t{1}),
            std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(*add(context, sat_form(exec_ir::DataType::u32),
                 std::numeric_limits<std::uint32_t>::max(), std::uint32_t{1}),
            std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(*add(context, sat_form(exec_ir::DataType::s32),
                 std::numeric_limits<std::int32_t>::max(), std::int32_t{1}),
            std::numeric_limits<std::int32_t>::max());
}

TEST(AddSemantics, PackedIntegerLanesNeverCarryAcrossTheirBoundaries) {
  const arith::context context;
  EXPECT_EQ((*add(context, integer_form(exec_ir::DataType::u16x2),
                  u16x2_t{0x0001'ffffU}, u16x2_t{0x0001'0001U}))
                .bits,
            0x0002'0000U);
  EXPECT_EQ((*add(context, integer_form(exec_ir::DataType::s16x2),
                  s16x2_t{0x7fff'7fffU}, s16x2_t{0x0001'0001U}))
                .bits,
            0x8000'8000U);
  EXPECT_EQ((*add(context, sat_form(exec_ir::DataType::u16x2),
                  u16x2_t{0x0001'ffffU}, u16x2_t{0x0001'0001U}))
                .bits,
            0x0002'ffffU);
  EXPECT_EQ((*add(context, sat_form(exec_ir::DataType::s16x2),
                  s16x2_t{0x7fff'7fffU}, s16x2_t{0x0001'0001U}))
                .bits,
            0x7fff'7fffU);
  EXPECT_EQ((*add(context, packed_form(exec_ir::DataType::u8x4, true),
                  u8x4_t{0xffff'ff01U}, u8x4_t{0x0101'0102U}))
                .bits,
            0xffff'ff03U);
  EXPECT_EQ((*add(context, packed_form(exec_ir::DataType::s8x4, false),
                  s8x4_t{0x7f7f'7f7fU}, s8x4_t{0x0101'0101U}))
                .bits,
            0x8080'8080U);
  EXPECT_EQ((*add(context, sat_form(exec_ir::DataType::s16x2),
                  s16x2_t{0x7fff'7fffU}, s16x2_t{0x0001'0001U}))
                .bits,
            0x7fff'7fffU);
}

TEST(AddSemantics, FloatControlsUsePtxBitResultsRatherThanHostEvaluation) {
  const arith::context context;
  const exec_ir::Add::FloatF32 nearest{
      exec_ir::RoundingMode::rn, false,        false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Add::FloatF32 upward{
      exec_ir::RoundingMode::rp, false,        false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Add::FloatF32 toward_zero{
      exec_ir::RoundingMode::rz, false,        false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Add::FloatF32 downward{
      exec_ir::RoundingMode::rm, false,        false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Add::FloatF32 ftz_sat{
      exec_ir::RoundingMode::rn, true,         true,
      RegisterSlot{0},           immediate(0), immediate(0)};
  const exec_ir::Add::FloatF32 ftz{
      exec_ir::RoundingMode::rn, true,         false,
      RegisterSlot{0},           immediate(0), immediate(0)};
  EXPECT_EQ((*add(context, nearest, arith::float32_t::from_bits(0x3f80'0000U),
                  arith::float32_t::from_bits(0x3380'0000U)))
                .bits(),
            0x3f80'0000U);
  EXPECT_EQ((*add(context, upward, arith::float32_t::from_bits(0x3f80'0000U),
                  arith::float32_t::from_bits(0x3380'0000U)))
                .bits(),
            0x3f80'0001U);
  EXPECT_EQ(
      (*add(context, toward_zero, arith::float32_t::from_bits(0x3f80'0000U),
            arith::float32_t::from_bits(0x3380'0000U)))
          .bits(),
      0x3f80'0000U);
  EXPECT_EQ((*add(context, downward, arith::float32_t::from_bits(0x3f80'0000U),
                  arith::float32_t::from_bits(0x3380'0000U)))
                .bits(),
            0x3f80'0000U);
  EXPECT_EQ((*add(context, ftz_sat, arith::float32_t::from_bits(0x7f80'0000U),
                  arith::float32_t::from_bits(0U)))
                .bits(),
            0x3f80'0000U);
  EXPECT_EQ((*add(context, ftz_sat, arith::float32_t::from_bits(0x7fc0'0001U),
                  arith::float32_t::from_bits(0U)))
                .bits(),
            0U);
  EXPECT_EQ((*add(context, nearest, arith::float32_t::from_bits(0x8000'0000U),
                  arith::float32_t::from_bits(0x8000'0000U)))
                .bits(),
            0x8000'0000U);
  EXPECT_EQ((*add(context, ftz, arith::float32_t::from_bits(0x0000'0001U),
                  arith::float32_t::from_bits(0U)))
                .bits(),
            0U);
}

TEST(AddSemantics, FloatingFormsCoverPackedHalfBfloatF64AndMixedPaths) {
  const arith::context context;
  const exec_ir::Add::FloatF32x2 f32x2{exec_ir::RoundingMode::rn, false,
                                       RegisterSlot{0}, RegisterSlot{1},
                                       RegisterSlot{2}};
  const exec_ir::Add::FloatF32x2 f32x2_ftz{exec_ir::RoundingMode::rn, true,
                                           RegisterSlot{0}, RegisterSlot{1},
                                           RegisterSlot{2}};
  const exec_ir::Add::FloatF64 f64_upward{
      exec_ir::RoundingMode::rp, RegisterSlot{0}, immediate(0), immediate(0)};
  const exec_ir::Add::FloatF64 f64_nearest{
      exec_ir::RoundingMode::rn, RegisterSlot{0}, immediate(0), immediate(0)};
  const exec_ir::Add::FloatF64 f64_toward_zero{
      exec_ir::RoundingMode::rz, RegisterSlot{0}, immediate(0), immediate(0)};
  const exec_ir::Add::FloatF64 f64_downward{
      exec_ir::RoundingMode::rm, RegisterSlot{0}, immediate(0), immediate(0)};
  const exec_ir::Add::Half half{exec_ir::RoundingMode::rn,
                                false,
                                false,
                                exec_ir::DataType::f16x2,
                                RegisterSlot{0},
                                RegisterSlot{1},
                                RegisterSlot{2}};
  const exec_ir::Add::Bfloat bfloat{exec_ir::RoundingMode::rn,
                                    exec_ir::DataType::bf16x2, RegisterSlot{0},
                                    RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Add::Half half_scalar{exec_ir::RoundingMode::rn,
                                       false,
                                       false,
                                       exec_ir::DataType::f16,
                                       RegisterSlot{0},
                                       RegisterSlot{1},
                                       RegisterSlot{2}};
  const exec_ir::Add::Half half_ftz{exec_ir::RoundingMode::rn,
                                    true,
                                    false,
                                    exec_ir::DataType::f16,
                                    RegisterSlot{0},
                                    RegisterSlot{1},
                                    RegisterSlot{2}};
  const exec_ir::Add::Half half_ftz_sat{exec_ir::RoundingMode::rn,
                                        true,
                                        true,
                                        exec_ir::DataType::f16,
                                        RegisterSlot{0},
                                        RegisterSlot{1},
                                        RegisterSlot{2}};
  const exec_ir::Add::Bfloat bfloat_scalar{
      exec_ir::RoundingMode::rn, exec_ir::DataType::bf16, RegisterSlot{0},
      RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Add::MixedF32 mixed_half{
      exec_ir::RoundingMode::rn, false,           exec_ir::DataType::f16,
      RegisterSlot{0},           RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Add::MixedF32 mixed_bfloat{
      exec_ir::RoundingMode::rp, false,           exec_ir::DataType::bf16,
      RegisterSlot{0},           RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Add::MixedF32 mixed_nearest{
      exec_ir::RoundingMode::rn, false,           exec_ir::DataType::bf16,
      RegisterSlot{0},           RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Add::MixedF32 mixed_toward_zero{
      exec_ir::RoundingMode::rz, false,           exec_ir::DataType::bf16,
      RegisterSlot{0},           RegisterSlot{1}, RegisterSlot{2}};
  const exec_ir::Add::MixedF32 mixed_downward{
      exec_ir::RoundingMode::rm, false,           exec_ir::DataType::bf16,
      RegisterSlot{0},           RegisterSlot{1}, RegisterSlot{2}};
  EXPECT_EQ((*add(context, f32x2,
                  arith::float32x2_t::from_bits(0xbf80'0000'3f80'0000ULL),
                  arith::float32x2_t::from_bits(0x8000'0000'3f80'0000ULL)))
                .bits(),
            0xbf80'0000'4000'0000ULL);
  EXPECT_EQ((*add(context, f32x2_ftz,
                  arith::float32x2_t::from_bits(0x3f80'0000'0000'0001ULL),
                  arith::float32x2_t::from_bits(0U)))
                .bits(),
            0x3f80'0000'0000'0000ULL);
  EXPECT_EQ((*add(context, f64_upward,
                  arith::float64_t::from_bits(0x3ff0'0000'0000'0000ULL),
                  arith::float64_t::from_bits(0x3ca0'0000'0000'0000ULL)))
                .bits(),
            0x3ff0'0000'0000'0001ULL);
  EXPECT_EQ((*add(context, f64_nearest,
                  arith::float64_t::from_bits(0x3ff0'0000'0000'0000ULL),
                  arith::float64_t::from_bits(0x3ca0'0000'0000'0000ULL)))
                .bits(),
            0x3ff0'0000'0000'0000ULL);
  EXPECT_EQ((*add(context, f64_toward_zero,
                  arith::float64_t::from_bits(0x3ff0'0000'0000'0000ULL),
                  arith::float64_t::from_bits(0x3ca0'0000'0000'0000ULL)))
                .bits(),
            0x3ff0'0000'0000'0000ULL);
  EXPECT_EQ((*add(context, f64_downward,
                  arith::float64_t::from_bits(0x3ff0'0000'0000'0000ULL),
                  arith::float64_t::from_bits(0x3ca0'0000'0000'0000ULL)))
                .bits(),
            0x3ff0'0000'0000'0000ULL);
  EXPECT_EQ((*add(context, half, arith::float16x2_t::from_bits(0x8000'3c00U),
                  arith::float16x2_t::from_bits(0x8000'4000U)))
                .bits(),
            0x8000'4200U);
  EXPECT_EQ((*add(context, bfloat, arith::bfloat16x2_t::from_bits(0x8000'3f80U),
                  arith::bfloat16x2_t::from_bits(0x8000'4000U)))
                .bits(),
            0x8000'4040U);
  EXPECT_EQ((*add(context, half_scalar, arith::float16_t::from_bits(0x3c00U),
                  arith::float16_t::from_bits(0x4000U)))
                .bits(),
            0x4200U);
  EXPECT_EQ((*add(context, half_ftz, arith::float16_t::from_bits(0x0001U),
                  arith::float16_t::from_bits(0U)))
                .bits(),
            0U);
  EXPECT_EQ((*add(context, half_ftz_sat, arith::float16_t::from_bits(0x7e01U),
                  arith::float16_t::from_bits(0U)))
                .bits(),
            0U);
  EXPECT_EQ((*add(context, bfloat_scalar, arith::bfloat16_t::from_bits(0x3f80U),
                  arith::bfloat16_t::from_bits(0x4000U)))
                .bits(),
            0x4040U);
  EXPECT_EQ((*add(context, mixed_half, arith::float16_t::from_bits(0x3c00U),
                  arith::float32_t::from_bits(0x4000'0000U)))
                .bits(),
            0x4040'0000U);
  EXPECT_EQ((*add(context, mixed_bfloat, arith::bfloat16_t::from_bits(0x3f80U),
                  arith::float32_t::from_bits(0x3380'0000U)))
                .bits(),
            0x3f80'0001U);
  EXPECT_EQ((*add(context, mixed_nearest, arith::bfloat16_t::from_bits(0x3f80U),
                  arith::float32_t::from_bits(0x3380'0000U)))
                .bits(),
            0x3f80'0000U);
  EXPECT_EQ(
      (*add(context, mixed_toward_zero, arith::bfloat16_t::from_bits(0x3f80U),
            arith::float32_t::from_bits(0x3380'0000U)))
          .bits(),
      0x3f80'0000U);
  EXPECT_EQ(
      (*add(context, mixed_downward, arith::bfloat16_t::from_bits(0x3f80U),
            arith::float32_t::from_bits(0x3380'0000U)))
          .bits(),
      0x3f80'0000U);
}

TEST(AddSemantics, RejectsNonPtxRoundingBeforeLanePreparation) {
  const exec_ir::Add::FloatF32 invalid{exec_ir::RoundingMode::rzi,
                                       false,
                                       false,
                                       RegisterSlot{0},
                                       immediate(0),
                                       immediate(0)};
  const exec_ir::Add::Half invalid_half{exec_ir::RoundingMode::rz,
                                        false,
                                        false,
                                        exec_ir::DataType::f16,
                                        RegisterSlot{0},
                                        RegisterSlot{1},
                                        RegisterSlot{2}};
  EXPECT_FALSE(valid_add(invalid));
  EXPECT_FALSE(valid_add(invalid_half));
}

TEST(AddSemantics, CodecsPreserveBitsAndRejectTheWrongWidth) {
  static_assert(
      ValueCodec<std::uint16_t> && ValueCodec<std::int16_t> &&
      ValueCodec<std::uint32_t> && ValueCodec<std::int32_t> &&
      ValueCodec<std::uint64_t> && ValueCodec<std::int64_t> &&
      ValueCodec<arith::float16_t> && ValueCodec<arith::bfloat16_t> &&
      ValueCodec<arith::float32_t> && ValueCodec<arith::float64_t> &&
      ValueCodec<u16x2_t> && ValueCodec<s16x2_t> && ValueCodec<u8x4_t> &&
      ValueCodec<s8x4_t> && ValueCodec<arith::float16x2_t> &&
      ValueCodec<arith::bfloat16x2_t> && ValueCodec<arith::float32x2_t>);
  expect_codec_round_trip<std::uint16_t>(RawValue::b16(std::uint16_t{0x1234U}));
  expect_codec_round_trip<std::int16_t>(RawValue::b16(std::uint16_t{0x8000U}));
  expect_codec_round_trip<std::uint32_t>(RawValue::b32(0x1234'5678U));
  expect_codec_round_trip<std::int32_t>(RawValue::b32(0x8000'0000U));
  expect_codec_round_trip<std::uint64_t>(
      RawValue::b64(std::uint64_t{0x0123'4567'89ab'cdefULL}));
  expect_codec_round_trip<std::int64_t>(
      RawValue::b64(std::uint64_t{0x8000'0000'0000'0000ULL}));
  expect_codec_round_trip<arith::float16_t>(
      RawValue::b16(std::uint16_t{0x7e01U}));
  expect_codec_round_trip<arith::bfloat16_t>(
      RawValue::b16(std::uint16_t{0x7fc1U}));
  expect_codec_round_trip<arith::float32_t>(RawValue::b32(0x7fc0'0001U));
  expect_codec_round_trip<arith::float64_t>(
      RawValue::b64(std::uint64_t{0x7ff8'0000'0000'0001ULL}));
  expect_codec_round_trip<u16x2_t>(RawValue::b32(0x1234'5678U));
  expect_codec_round_trip<s16x2_t>(RawValue::b32(0x8000'7fffU));
  expect_codec_round_trip<u8x4_t>(RawValue::b32(0x1234'5678U));
  expect_codec_round_trip<s8x4_t>(RawValue::b32(0x807f'00ffU));
  expect_codec_round_trip<arith::float16x2_t>(RawValue::b32(0x7e01'3c00U));
  expect_codec_round_trip<arith::bfloat16x2_t>(RawValue::b32(0x7fc1'3f80U));
  expect_codec_round_trip<arith::float32x2_t>(
      RawValue::b64(std::uint64_t{0x7fc0'0001'3f80'0000ULL}));
  const auto signed_value =
      value_codec<std::int16_t>::decode(RawValue::b16(std::uint16_t{0x8000U}));
  ASSERT_TRUE(signed_value);
  EXPECT_EQ(*signed_value, std::numeric_limits<std::int16_t>::min());
  EXPECT_EQ(value_codec<arith::float32_t>::encode(
                arith::float32_t::from_bits(0x8000'0000U)),
            RawValue::b32(0x8000'0000U));
  EXPECT_FALSE(value_codec<arith::float64_t>::decode(RawValue::b32(0U)));
}

}  // namespace
}  // namespace ptxsim::inst_execute_engine::detail::semantics::test
