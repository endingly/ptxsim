#include <gtest/gtest.h>

#include <array>
#include <limits>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {

static_assert(tensor_capability_v<float16_t, float16_t, float16_t, float16_t>);
static_assert(tensor_capability_v<float32_t, float16_t, float16_t, float32_t>);
static_assert(tensor_capability_v<float32_t, bfloat16_t, bfloat16_t, float32_t>);
static_assert(tensor_capability_v<float32_t, tfloat32_t, tfloat32_t, float32_t>);
static_assert(tensor_capability_v<float16_t, float8_e4m3_t, float6_e3m2_t,
                                  float16_t>);
static_assert(tensor_capability_v<float32_t, float4_e2m1_t, float8_e5m2_t,
                                  float32_t>);
static_assert(tensor_capability_v<float64_t, float64_t, float64_t, float64_t>);
static_assert(tensor_capability_v<int32_t, uint8_t, int8_t, int32_t>);
static_assert(tensor_capability_v<int32_t, int8_t, uint8_t, int32_t>);
static_assert(tensor_capability_v<float32_t, float4_e2m1_t, float4_e2m1_t,
                                  float32_t, tensor_scale_model::two_x>);
static_assert(!tensor_capability_v<float32_t, float32_t, float32_t, float32_t>);
static_assert(!tensor_capability_v<int32_t, int32_t, int32_t, int32_t>);
static_assert(!tensor_capability_v<int32_t, uint16_t, int8_t, int32_t>);
static_assert(!tensor_capability_v<float32_t, float4_e2m1_t, float8_e4m3_t,
                                   float32_t, tensor_scale_model::two_x>);

TEST(TensorArithmetic, CombinationTableAndWidenedLowProducts) {
  context c;
  const tensor::tile<1, 1, float16_t> h{{float16_t::from_bits(0x4000)}},
      h3{{float16_t::from_bits(0x4200)}};
  const tensor::tile<1, 1, float32_t> z{};
  const auto f16_to_f32 = tensor::mma<float32_t>(c, h, h3, z);
  ASSERT_TRUE(f16_to_f32);
  EXPECT_EQ(f16_to_f32->value(0, 0).bits(), 0x40c00000u);

  const tensor::tile<1, 1, float8_e4m3_t> e4{
      {float8_e4m3_t::from_bits(0x39)}};  // 1.125
  const tensor::tile<1, 1, float6_e3m2_t> e3{
      {float6_e3m2_t::from_bits(0x0c)}};  // 1
  const auto low = tensor::mma<float32_t>(c, e4, e3, z);
  ASSERT_TRUE(low);
  EXPECT_EQ(low->value(0, 0).bits(), 0x3f900000u);
}

TEST(TensorArithmetic, IntegerMmaWidensSignednessAndReportsRangeSeparately) {
  context c;
  const tensor::tile<1, 1, uint8_t> u{{255}};
  const tensor::tile<1, 1, int8_t> s{{-2}};
  const tensor::tile<1, 1, int32_t> c0{{10}};
  const auto mixed = tensor::mma<int32_t>(c, u, s, c0);
  ASSERT_TRUE(mixed);
  EXPECT_EQ(mixed->value(0, 0), -500);
  EXPECT_FALSE(mixed->status.inexact);
  EXPECT_FALSE(mixed->status.overflow);

  const tensor::tile<1, 1, uint8_t> one{{1}};
  const tensor::tile<1, 1, int8_t> one_s{{1}};
  const tensor::tile<1, 1, int32_t> top{{std::numeric_limits<int32_t>::max()}};
  const auto wrapped = tensor::mma<int32_t>(c, one, one_s, top);
  ASSERT_TRUE(wrapped);
  EXPECT_EQ(wrapped->value(0, 0), std::numeric_limits<int32_t>::min());
  EXPECT_TRUE(wrapped->status.overflow);
  EXPECT_FALSE(wrapped->status.inexact);
  EXPECT_FALSE(wrapped->status.saturated);
  const auto saturated = tensor::mma<int32_t>(
      c, one, one_s, top, {.saturation = saturation_mode::type_range});
  ASSERT_TRUE(saturated);
  EXPECT_EQ(saturated->value(0, 0), std::numeric_limits<int32_t>::max());
  EXPECT_TRUE(saturated->status.overflow);
  EXPECT_TRUE(saturated->status.saturated);
}

TEST(TensorArithmetic, IntegerMmaAccumulatesWholeKDimensionBeforeNarrowing) {
  context c;
  const tensor::tile<1, 2, uint8_t> a{{1, 1}};
  const tensor::tile<2, 1, int8_t> b{{1, -1}};
  const tensor::tile<1, 1, int32_t> top{{std::numeric_limits<int32_t>::max()}};
  const auto tile_result = tensor::mma<int32_t>(c, a, b, top);
  ASSERT_TRUE(tile_result);
  EXPECT_EQ(tile_result->value(0, 0), std::numeric_limits<int32_t>::max());
  EXPECT_FALSE(tile_result->status.overflow);
  EXPECT_FALSE(tile_result->status.saturated);

  const uint8_t a_data[] = {1, 1};
  const int8_t b_data[] = {1, -1};
  const int32_t c_data[] = {std::numeric_limits<int32_t>::max()};
  int32_t d_data[1] = {};
  const auto view_result = tensor::mma<int32_t>(
      c, tensor::matrix_view<const uint8_t>{a_data, 1, 2, 2},
      tensor::matrix_view<const int8_t>{b_data, 2, 1, 1},
      tensor::matrix_view<const int32_t>{c_data, 1, 1, 1},
      tensor::matrix_view<int32_t>{d_data, 1, 1, 1});
  ASSERT_TRUE(view_result);
  EXPECT_EQ(d_data[0], std::numeric_limits<int32_t>::max());
  EXPECT_FALSE(view_result->overflow);
}

TEST(TensorArithmetic, BlockScalesAreAxisAwareForNonSquareB) {
  context c;
  const tensor::tile<2, 4, float8_e4m3_t> a{{
      float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
      float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
      float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
      float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38)}};
  const tensor::tile<4, 3, float8_e5m2_t> b{{
      float8_e5m2_t::from_bits(0x3c), float8_e5m2_t::from_bits(0x3c),
      float8_e5m2_t::from_bits(0x3c), float8_e5m2_t::from_bits(0x3c),
      float8_e5m2_t::from_bits(0x3c), float8_e5m2_t::from_bits(0x3c),
      float8_e5m2_t::from_bits(0x3c), float8_e5m2_t::from_bits(0x3c),
      float8_e5m2_t::from_bits(0x3c), float8_e5m2_t::from_bits(0x3c),
      float8_e5m2_t::from_bits(0x3c), float8_e5m2_t::from_bits(0x3c)}};
  const tensor::tile<2, 3, float32_t> z{};
  const std::array scales_a{ufloat8_e8m0_t::from_bits(127),
                            ufloat8_e8m0_t::from_bits(128),
                            ufloat8_e8m0_t::from_bits(129),
                            ufloat8_e8m0_t::from_bits(130)};
  // chunks x N: all columns use 1 for k[0,1], then 2 for k[2,3].
  const std::array scales_b{ufloat8_e8m0_t::from_bits(127),
                            ufloat8_e8m0_t::from_bits(127),
                            ufloat8_e8m0_t::from_bits(127),
                            ufloat8_e8m0_t::from_bits(128),
                            ufloat8_e8m0_t::from_bits(128),
                            ufloat8_e8m0_t::from_bits(128)};
  const auto r = tensor::mma<float32_t>(
      c, a, b, z, {},
      tensor::block_scale_view{scales_a,
                               {tensor::scale_axis::row_chunks, 2, 2,
                                tensor_scale_model::one_x}},
      tensor::block_scale_view{scales_b,
                               {tensor::scale_axis::column_chunks, 2, 2,
                                tensor_scale_model::one_x}});
  ASSERT_TRUE(r);
  for (std::size_t column = 0; column != 3; ++column) {
    EXPECT_EQ(r->value(0, column).bits(), 0x41200000u);  // 10
    EXPECT_EQ(r->value(1, column).bits(), 0x42200000u);  // 40
  }

  const auto bad_axis = tensor::mma<float32_t>(
      c, a, b, z, {},
      tensor::block_scale_view{scales_a,
                               {tensor::scale_axis::column_chunks, 2, 2,
                                tensor_scale_model::one_x}},
      tensor::block_scale_view{scales_b,
                               {tensor::scale_axis::column_chunks, 2, 2,
                                tensor_scale_model::one_x}});
  EXPECT_EQ(bad_axis.error(), arithmetic_error::invalid_scale_layout);
}

TEST(TensorArithmetic, ScaleModelsAndCompensatingExponents) {
  context c;
  const tensor::tile<1, 1, float8_e4m3_t> maximum{
      {float8_e4m3_t::from_bits(0x77)}};  // 240
  const tensor::tile<1, 1, float8_e5m2_t> one{
      {float8_e5m2_t::from_bits(0x3c)}};
  const tensor::tile<1, 1, float32_t> z{};
  const std::array high{ufloat8_e8m0_t::from_bits(254)};  // 2^127
  const std::array low{ufloat8_e8m0_t::from_bits(0)};     // 2^-127
  const auto row = tensor::block_scale_layout{
      tensor::scale_axis::row_chunks, 1, 1, tensor_scale_model::one_x};
  const auto col = tensor::block_scale_layout{
      tensor::scale_axis::column_chunks, 1, 1, tensor_scale_model::one_x};
  const auto no_premature_overflow = tensor::mma<float32_t>(
      c, maximum, one, z, {}, tensor::block_scale_view{high, row},
      tensor::block_scale_view{low, col});
  ASSERT_TRUE(no_premature_overflow);
  EXPECT_EQ(no_premature_overflow->value(0, 0).bits(), 0x43700000u);

  const tensor::tile<1, 1, float8_e5m2_t> tiny{
      {float8_e5m2_t::from_bits(0x01)}};  // 2^-16
  const auto no_premature_underflow = tensor::mma<float32_t>(
      c, tiny, tiny, z, {}, tensor::block_scale_view{low, row},
      tensor::block_scale_view{high, col});
  ASSERT_TRUE(no_premature_underflow);
  EXPECT_EQ(no_premature_underflow->value(0, 0).bits(), 0x2f800000u);

  const tensor::tile<1, 1, float4_e2m1_t> e2m1{
      {float4_e2m1_t::from_bits(0x02)}};
  const std::array ue4{ufloat7_e4m3_t::from_bits(0x38)};
  const auto four_x = tensor::mma<float32_t>(
      c, e2m1, e2m1, z, {},
      tensor::block_scale_view{ue4,
                               {tensor::scale_axis::row_chunks, 1, 1,
                                tensor_scale_model::four_x}},
      tensor::block_scale_view{ue4,
                               {tensor::scale_axis::column_chunks, 1, 1,
                                tensor_scale_model::four_x}});
  ASSERT_TRUE(four_x);
  EXPECT_EQ(four_x->value(0, 0).bits(), 0x3f800000u);
  const auto illegal_ue4 = tensor::mma<float32_t>(
      c, e2m1, e2m1, z, {},
      tensor::block_scale_view{ue4,
                               {tensor::scale_axis::row_chunks, 1, 1,
                                tensor_scale_model::one_x}},
      tensor::block_scale_view{ue4,
                               {tensor::scale_axis::column_chunks, 1, 1,
                                tensor_scale_model::one_x}});
  EXPECT_EQ(illegal_ue4.error(), arithmetic_error::invalid_scale_layout);
}

TEST(TensorArithmetic, ScaleNanPropagatesThroughTensorStatus) {
  context c;
  const tensor::tile<1, 1, float8_e4m3_t> one{{float8_e4m3_t::from_bits(0x38)}};
  const tensor::tile<1, 1, float8_e5m2_t> one_b{{float8_e5m2_t::from_bits(0x3c)}};
  const tensor::tile<1, 1, float32_t> zero{};
  const std::array nan_scale{ufloat8_e8m0_t::from_bits(0xff)};
  const auto row = tensor::block_scale_layout{
      tensor::scale_axis::row_chunks, 1, 1, tensor_scale_model::one_x};
  const auto col = tensor::block_scale_layout{
      tensor::scale_axis::column_chunks, 1, 1, tensor_scale_model::one_x};
  const auto result = tensor::mma<float32_t>(
      c, one, one_b, zero, {}, tensor::block_scale_view{nan_scale, row},
      tensor::block_scale_view{nan_scale, col});
  ASSERT_TRUE(result);
  EXPECT_TRUE(is_nan(result->value(0, 0)));
  // UE8M0's sole NaN is quiet in this model: it propagates without signaling
  // invalid, while a signaling source format would set tensor_status.invalid.
  EXPECT_FALSE(result->status.invalid);
}

TEST(TensorArithmetic, UnknownTensorProfileIsRejected) {
  model_profile profile{};
  profile.tensor.model = tensor_model::unavailable;
  const context c{profile};
  const tensor::tile<1, 1, float16_t> a{{float16_t::from_bits(0x3c00)}};
  const tensor::tile<1, 1, float16_t> b{{float16_t::from_bits(0x3c00)}};
  const tensor::tile<1, 1, float16_t> z{};
  EXPECT_EQ(tensor::mma<float16_t>(c, a, b, z).error(),
            arithmetic_error::unsupported_model_profile);
}

TEST(TensorArithmetic, ScaledMacRoundsOnlyAfterCombiningProductAndAccumulator) {
  context c;
  const tensor::tile<1, 1, float8_e4m3_t> one{
      {float8_e4m3_t::from_bits(0x38)}};
  const auto row = tensor::block_scale_layout{
      tensor::scale_axis::row_chunks, 1, 1, tensor_scale_model::one_x};
  const auto col = tensor::block_scale_layout{
      tensor::scale_axis::column_chunks, 1, 1, tensor_scale_model::one_x};
  // 2^127 * 2 = 2^128.  That product is individually outside F32, but
  // adding -max_f32 exactly leaves 2^104 (finite).  An encoded-product FMA
  // would instead observe +Inf and fail this vector.
  const std::array scale_127{ufloat8_e8m0_t::from_bits(254)};
  const std::array scale_1{ufloat8_e8m0_t::from_bits(128)};
  const tensor::tile<1, 1, float32_t> neg_max{
      {float32_t::from_bits(0xff7fffffu)}};
  const auto finite_cancellation = tensor::mma<float32_t>(
      c, one, one, neg_max, {}, tensor::block_scale_view{scale_127, row},
      tensor::block_scale_view{scale_1, col});
  ASSERT_TRUE(finite_cancellation);
  EXPECT_EQ(finite_cancellation->value(0, 0).bits(), 0x73800000u);  // 2^104
  EXPECT_FALSE(finite_cancellation->status.overflow);

  // A product far below the F32 subnormal range does not disappear before C:
  // the final value is C, but final rounding is inexact.
  const tensor::tile<1, 1, float8_e5m2_t> tiny{
      {float8_e5m2_t::from_bits(0x01)}};
  const std::array scale_min{ufloat8_e8m0_t::from_bits(0)};
  const tensor::tile<1, 1, float32_t> one_c{
      {float32_t::from_bits(0x3f800000u)}};
  const auto tiny_plus_one = tensor::mma<float32_t>(
      c, tiny, tiny, one_c, {}, tensor::block_scale_view{scale_min, row},
      tensor::block_scale_view{scale_min, col});
  ASSERT_TRUE(tiny_plus_one);
  EXPECT_EQ(tiny_plus_one->value(0, 0).bits(), 0x3f800000u);
  EXPECT_TRUE(tiny_plus_one->status.inexact);

  // 1 + 2^-24 is the F32 halfway case.  Nearest-even stays at one; upward
  // directed rounding advances exactly one ULP.
  const std::array scale_tie{ufloat8_e8m0_t::from_bits(103)};
  const std::array scale_one{ufloat8_e8m0_t::from_bits(127)};
  const auto nearest = tensor::mma<float32_t>(
      c, one, one, one_c, {}, tensor::block_scale_view{scale_tie, row},
      tensor::block_scale_view{scale_one, col});
  const auto upward = tensor::mma<float32_t>(
      c, one, one, one_c,
      {.accumulator_rounding = rounding_mode::toward_positive},
      tensor::block_scale_view{scale_tie, row},
      tensor::block_scale_view{scale_one, col});
  ASSERT_TRUE(nearest && upward);
  EXPECT_EQ(nearest->value(0, 0).bits(), 0x3f800000u);
  EXPECT_EQ(upward->value(0, 0).bits(), 0x3f800001u);
  EXPECT_TRUE(nearest->status.inexact);
  EXPECT_TRUE(upward->status.inexact);

  const tensor::tile<1, 1, float32_t> minus_one{
      {float32_t::from_bits(0xbf800000u)}};
  const auto exact_zero = tensor::mma<float32_t>(
      c, one, one, minus_one, {}, tensor::block_scale_view{scale_one, row},
      tensor::block_scale_view{scale_one, col});
  const auto negative_zero = tensor::mma<float32_t>(
      c, one, one, minus_one,
      {.accumulator_rounding = rounding_mode::toward_negative},
      tensor::block_scale_view{scale_one, row},
      tensor::block_scale_view{scale_one, col});
  ASSERT_TRUE(exact_zero && negative_zero);
  EXPECT_EQ(exact_zero->value(0, 0).bits(), 0u);
  EXPECT_EQ(negative_zero->value(0, 0).bits(), 0x80000000u);
}

TEST(TensorArithmetic, MatrixViewRejectsInvalidShape) {
  context c;
  const tensor::matrix_view<const float16_t> a{nullptr, 1, 1, 1},
      b{nullptr, 1, 1, 1}, z{nullptr, 1, 1, 1};
  tensor::matrix_view<float16_t> d{nullptr, 1, 1, 1};
  EXPECT_EQ(tensor::mma<float16_t>(c, a, b, z, d).error(),
            arithmetic_error::invalid_tensor_shape);
}

}  // namespace ptxsim::arith::test
