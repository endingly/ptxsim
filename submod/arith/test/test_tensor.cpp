#include <gtest/gtest.h>

#include <array>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {

TEST(TensorArithmetic, LogicalTensorMma) {
  context c;
  tensor::tile<1, 1, float32_t> a{{float32_t::from_bits(0x40000000)}},
      b{{float32_t::from_bits(0x40400000)}},
      z{{float32_t::from_bits(0x3f800000)}};
  auto r = tensor::mma<float32_t>(c, a, b, z);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->value(0, 0).bits(), 0x40e00000u);
  EXPECT_TRUE(r->status.model_dependent);
}

TEST(TensorArithmetic, LogicalTilesUseAscendingKOrder) {
  context c;
  const tensor::tile<2, 3, float32_t> a{
      {float32_t::from_bits(0x3f800000), float32_t::from_bits(0x40000000),
       float32_t::from_bits(0x40400000), float32_t::from_bits(0x40800000),
       float32_t::from_bits(0x40a00000), float32_t::from_bits(0x40c00000)}};
  const tensor::tile<3, 2, float32_t> b{
      {float32_t::from_bits(0x40e00000), float32_t::from_bits(0x41000000),
       float32_t::from_bits(0x41100000), float32_t::from_bits(0x41200000),
       float32_t::from_bits(0x41300000), float32_t::from_bits(0x41400000)}};
  const tensor::tile<2, 2, float32_t> z{
      {float32_t::from_bits(0x3f800000), float32_t::from_bits(0x3f800000),
       float32_t::from_bits(0x3f800000), float32_t::from_bits(0x3f800000)}};
  const auto r = tensor::mma<float32_t>(c, a, b, z);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->value(0, 0).bits(), 0x426c0000u);  // 59
  EXPECT_EQ(r->value(0, 1).bits(), 0x42820000u);  // 65
  EXPECT_EQ(r->value(1, 0).bits(), 0x430c0000u);  // 140
  EXPECT_EQ(r->value(1, 1).bits(), 0x431b0000u);  // 155

  const tensor::tile<2, 3, float16_t> order_a{
      {float16_t::from_bits(0x6c00), float16_t::from_bits(0xec00),
       float16_t::from_bits(0x3c00), float16_t::from_bits(0x6c00),
       float16_t::from_bits(0xec00), float16_t::from_bits(0x3c00)}};
  const tensor::tile<3, 2, float16_t> order_b{
      {float16_t::from_bits(0x3c00), float16_t::from_bits(0x3c00),
       float16_t::from_bits(0x3c00), float16_t::from_bits(0x3c00),
       float16_t::from_bits(0x3c00), float16_t::from_bits(0x3c00)}};
  const auto ordered = tensor::mma<float16_t>(c, order_a, order_b,
                                              tensor::tile<2, 2, float16_t>{});
  ASSERT_TRUE(ordered);
  EXPECT_EQ(ordered->value(0, 0).bits(), 0x3c00u);
  EXPECT_EQ(ordered->value(0, 1).bits(), 0x3c00u);
  EXPECT_EQ(ordered->value(1, 0).bits(), 0x3c00u);
  EXPECT_EQ(ordered->value(1, 1).bits(), 0x3c00u);
}

TEST(TensorArithmetic, MatrixViewRejectsNonemptyNullData) {
  context c;
  const tensor::matrix_view<const float32_t> a{nullptr, 1, 1, 1},
      b{nullptr, 1, 1, 1}, z{nullptr, 1, 1, 1};
  tensor::matrix_view<float32_t> d{nullptr, 1, 1, 1};
  EXPECT_EQ(tensor::mma<float32_t>(c, a, b, z, d).error(),
            arithmetic_error::invalid_tensor_shape);
}

TEST(TensorArithmetic, LowPrecisionWidensProductsAndSupportsCapabilities) {
  context c;
  const auto one = float32_t::from_bits(0x3f800000);
  const auto a8 =
      cvt<float8_e4m3_t>(c, float32_t::from_bits(0x3f900000));  // 1.125
  ASSERT_TRUE(a8);
  tensor::tile<1, 1, float8_e4m3_t> a{{a8->value}}, b{{a8->value}};
  tensor::tile<1, 1, float32_t> z{{float32_t{}}};
  auto fp8 = tensor::mma<float32_t>(c, a, b, z);
  ASSERT_TRUE(fp8);
  auto widened = cvt<float32_t>(c, a8->value);
  ASSERT_TRUE(widened);
  auto expected = fma(c, widened->value, widened->value, float32_t{});
  ASSERT_TRUE(expected);
  EXPECT_EQ(fp8->value(0, 0),
            expected->value);  // never round the product back to FP8
  EXPECT_EQ(fp8->value(0, 0).bits(), 0x3fa20000u);

  tensor::tile<1, 1, float16_t> h{{float16_t::from_bits(0x4000)}},
      h3{{float16_t::from_bits(0x4200)}}, h1{{float16_t::from_bits(0x3c00)}};
  auto h16 = tensor::mma<float16_t>(c, h, h3, h1);
  auto h32 = tensor::mma<float32_t>(c, h, h3, z);
  ASSERT_TRUE(h16);
  ASSERT_TRUE(h32);
  EXPECT_EQ(h16->value(0, 0).bits(), 0x4700);
  EXPECT_EQ(h32->value(0, 0).bits(), 0x40c00000);

  const auto e5 = cvt<float8_e5m2_t>(c, one);
  const auto e2 = cvt<float6_e2m3_t>(c, one);
  const auto e3 = cvt<float6_e3m2_t>(c, one);
  const auto e1 = cvt<float4_e2m1_t>(c, one);
  ASSERT_TRUE(e5 && e2 && e3 && e1);
  EXPECT_TRUE((tensor::mma<float32_t>(
      c, tensor::tile<1, 1, float8_e5m2_t>{{e5->value}},
      tensor::tile<1, 1, float8_e5m2_t>{{e5->value}}, z)));
  EXPECT_TRUE((tensor::mma<float32_t>(
      c, tensor::tile<1, 1, float6_e2m3_t>{{e2->value}},
      tensor::tile<1, 1, float6_e2m3_t>{{e2->value}}, z)));
  EXPECT_TRUE((tensor::mma<float32_t>(
      c, tensor::tile<1, 1, float6_e3m2_t>{{e3->value}},
      tensor::tile<1, 1, float6_e3m2_t>{{e3->value}}, z)));
  EXPECT_TRUE((tensor::mma<float32_t>(
      c, tensor::tile<1, 1, float4_e2m1_t>{{e1->value}},
      tensor::tile<1, 1, float4_e2m1_t>{{e1->value}}, z)));

  auto tf32 = cvt<tfloat32_t>(c, one);
  ASSERT_TRUE(tf32);
  const auto bf16_one = tensor::mma<float32_t>(
      c, tensor::tile<1, 1, bfloat16_t>{{bfloat16_t::from_bits(0x3f80)}},
      tensor::tile<1, 1, bfloat16_t>{{bfloat16_t::from_bits(0x3f80)}}, z);
  const auto tf32_one =
      tensor::mma<float32_t>(c, tensor::tile<1, 1, tfloat32_t>{{tf32->value}},
                             tensor::tile<1, 1, tfloat32_t>{{tf32->value}}, z);
  ASSERT_TRUE(bf16_one && tf32_one);
  EXPECT_EQ(bf16_one->value(0, 0).bits(), 0x3f800000u);
  EXPECT_EQ(tf32_one->value(0, 0).bits(), 0x3f800000u);

  const auto one8 = float8_e4m3_t::from_bits(0x38);
  const auto one5 = float8_e5m2_t::from_bits(0x3c);
  const auto one6e2 = float6_e2m3_t::from_bits(0x08);
  const auto one6e3 = float6_e3m2_t::from_bits(0x0c);
  const auto one4 = float4_e2m1_t::from_bits(0x02);
  const auto one32 = float32_t::from_bits(0x3f800000);
  const auto bf = tensor::mma<float32_t>(
      c, tensor::tile<1, 1, bfloat16_t>{{bfloat16_t::from_bits(0x3fc0)}},
      tensor::tile<1, 1, bfloat16_t>{{bfloat16_t::from_bits(0x3fc0)}}, z);
  ASSERT_TRUE(bf);
  EXPECT_EQ(bf->value(0, 0).bits(), 0x40100000u);
  for (const auto value :
       {tensor::mma<float32_t>(c, tensor::tile<1, 1, float8_e4m3_t>{{one8}},
                               tensor::tile<1, 1, float8_e4m3_t>{{one8}}, z),
        tensor::mma<float32_t>(c, tensor::tile<1, 1, float8_e5m2_t>{{one5}},
                               tensor::tile<1, 1, float8_e5m2_t>{{one5}}, z),
        tensor::mma<float32_t>(c, tensor::tile<1, 1, float6_e2m3_t>{{one6e2}},
                               tensor::tile<1, 1, float6_e2m3_t>{{one6e2}}, z),
        tensor::mma<float32_t>(c, tensor::tile<1, 1, float6_e3m2_t>{{one6e3}},
                               tensor::tile<1, 1, float6_e3m2_t>{{one6e3}}, z),
        tensor::mma<float32_t>(c, tensor::tile<1, 1, float4_e2m1_t>{{one4}},
                               tensor::tile<1, 1, float4_e2m1_t>{{one4}}, z)}) {
    ASSERT_TRUE(value);
    EXPECT_EQ(value->value(0, 0).bits(), one32.bits());
  }
  tensor::tile<1, 1, float64_t> d2{
      {float64_t::from_bits(0x4000000000000000ULL)}},
      d3{{float64_t::from_bits(0x4008000000000000ULL)}},
      d1{{float64_t::from_bits(0x3ff0000000000000ULL)}};
  auto f64 = tensor::mma<float64_t>(c, d2, d3, d1);
  ASSERT_TRUE(f64);
  EXPECT_EQ(f64->value(0, 0).bits(), 0x401c000000000000ULL);
  tensor::tile<1, 1, int32_t> i2{{2}}, i3{{3}}, i1{{1}};
  auto integer = tensor::mma<int32_t>(c, i2, i3, i1);
  ASSERT_TRUE(integer);
  EXPECT_EQ(integer->value(0, 0), 7);
}

TEST(TensorArithmetic, BlockScalesControlsViewsAndModelStatus) {
  context c;
  tensor::tile<1, 1, float8_e4m3_t> a{{float8_e4m3_t::from_bits(0x38)}},
      b{{float8_e4m3_t::from_bits(0x38)}};
  tensor::tile<1, 1, float32_t> z{};
  const std::array scale_a{ufloat8_e8m0_t::from_bits(128)};  // 2
  const std::array scale_b{ufloat8_e8m0_t::from_bits(127)};  // 1
  ASSERT_TRUE(cvt<float32_t>(c, a(0, 0)));
  ASSERT_TRUE(cvt<float32_t>(c, scale_a[0]));
  EXPECT_EQ(cvt<float32_t>(c, a(0, 0))->value.bits(), 0x3f800000u);
  EXPECT_EQ(cvt<float32_t>(c, scale_a[0])->value.bits(), 0x40000000u);
  auto scaled =
      tensor::mma<float32_t>(c, a, b, z, {}, tensor::block_scale_view{scale_a},
                             tensor::block_scale_view{scale_b});
  ASSERT_TRUE(scaled);
  EXPECT_EQ(scaled->value(0, 0).bits(), 0x40000000u);
  EXPECT_TRUE(scaled->status.model_dependent);
  EXPECT_EQ(tensor::mma<float32_t>(
                c, a, b, z, {.product_subnormal = subnormal_mode::flush_input})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);
  const std::array invalid_scale{ufloat8_e8m0_t::from_bits(127),
                                 ufloat8_e8m0_t::from_bits(127)};
  EXPECT_EQ(tensor::mma<float32_t>(c, a, b, z, {},
                                   tensor::block_scale_view{invalid_scale},
                                   tensor::block_scale_view{scale_b})
                .error(),
            arithmetic_error::invalid_scale_layout);
  model_profile profile{};
  profile.tensor.model_dependent = false;
  auto defined = tensor::mma<float32_t>(context{profile}, a, b, z);
  ASSERT_TRUE(defined);
  EXPECT_FALSE(defined->status.model_dependent);

  std::array<float32_t, 1> av{float32_t::from_bits(0x40000000)},
      bv{float32_t::from_bits(0x40400000)},
      cv{float32_t::from_bits(0x3f800000)}, dv{};
  auto view = tensor::mma<float32_t>(
      c, tensor::matrix_view<const float32_t>{av.data(), 1, 1, 1},
      tensor::matrix_view<const float32_t>{bv.data(), 1, 1, 1},
      tensor::matrix_view<const float32_t>{cv.data(), 1, 1, 1},
      tensor::matrix_view<float32_t>{dv.data(), 1, 1, 1});
  ASSERT_TRUE(view);
  EXPECT_EQ(dv[0].bits(), 0x40e00000u);
}

TEST(TensorArithmetic, BlockScalesViewsAndInvalidLayouts) {
  context c;
  const tensor::tile<2, 3, float8_e4m3_t> a{
      {float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
       float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
       float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38)}};
  const tensor::tile<3, 2, float8_e4m3_t> b{
      {float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
       float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38),
       float8_e4m3_t::from_bits(0x38), float8_e4m3_t::from_bits(0x38)}};
  const tensor::tile<2, 2, float32_t> z{};
  const std::array ue8_a{ufloat8_e8m0_t::from_bits(127),
                         ufloat8_e8m0_t::from_bits(128),
                         ufloat8_e8m0_t::from_bits(129)};
  const std::array ue8_b{ufloat8_e8m0_t::from_bits(127),
                         ufloat8_e8m0_t::from_bits(128)};
  const std::array ue4_a{ufloat7_e4m3_t::from_bits(0x38),
                         ufloat7_e4m3_t::from_bits(0x40),
                         ufloat7_e4m3_t::from_bits(0x48)};
  const std::array ue4_b{ufloat7_e4m3_t::from_bits(0x38),
                         ufloat7_e4m3_t::from_bits(0x40)};
  for (const auto scaled :
       {tensor::mma<float32_t>(c, a, b, z, {},
                               tensor::block_scale_view{ue8_a, 2},
                               tensor::block_scale_view{ue8_b, 3}),
        tensor::mma<float32_t>(c, a, b, z, {},
                               tensor::block_scale_view{ue4_a, 2},
                               tensor::block_scale_view{ue4_b, 3})}) {
    ASSERT_TRUE(scaled);
    EXPECT_EQ(scaled->value(0, 0).bits(), 0x40c00000u);
    EXPECT_EQ(scaled->value(0, 1).bits(), 0x40e00000u);
    EXPECT_EQ(scaled->value(1, 0).bits(), 0x41600000u);
    EXPECT_EQ(scaled->value(1, 1).bits(), 0x41900000u);
  }
  EXPECT_EQ(
      tensor::mma<float32_t>(c, a, b, z, {}, tensor::block_scale_view{ue8_a, 0},
                             tensor::block_scale_view{ue8_b, 3})
          .error(),
      arithmetic_error::invalid_scale_layout);

  std::array<float32_t, 8> av{{float32_t::from_bits(0x3f800000),
                               float32_t::from_bits(0x40000000),
                               {},
                               {},
                               float32_t::from_bits(0x40400000),
                               float32_t::from_bits(0x40800000)}};
  std::array<float32_t, 9> bv{{float32_t::from_bits(0x40a00000),
                               float32_t::from_bits(0x40c00000),
                               {},
                               float32_t::from_bits(0x40e00000),
                               float32_t::from_bits(0x41000000),
                               {},
                               float32_t::from_bits(0x41100000),
                               float32_t::from_bits(0x41200000)}};
  std::array<float32_t, 6> cv{{float32_t::from_bits(0x3f800000),
                               float32_t::from_bits(0x3f800000),
                               {},
                               float32_t::from_bits(0x3f800000),
                               float32_t::from_bits(0x3f800000)}};
  std::array<float32_t, 6> dv{};
  const auto view = tensor::mma<float32_t>(
      c, tensor::matrix_view<const float32_t>{av.data(), 2, 2, 4},
      tensor::matrix_view<const float32_t>{bv.data(), 2, 2, 3},
      tensor::matrix_view<const float32_t>{cv.data(), 2, 2, 3},
      tensor::matrix_view<float32_t>{dv.data(), 2, 2, 3});
  ASSERT_TRUE(view);
  EXPECT_EQ(dv[0].bits(), 0x41a00000u);  // 20
  EXPECT_EQ(dv[1].bits(), 0x41b80000u);  // 23
  EXPECT_EQ(dv[3].bits(), 0x42300000u);  // 44
  EXPECT_EQ(dv[4].bits(), 0x424c0000u);  // 51
  EXPECT_EQ(tensor::mma<float32_t>(
                c, tensor::matrix_view<const float32_t>{av.data(), 2, 2, 1},
                tensor::matrix_view<const float32_t>{bv.data(), 2, 2, 3},
                tensor::matrix_view<const float32_t>{cv.data(), 2, 2, 3},
                tensor::matrix_view<float32_t>{dv.data(), 2, 2, 3})
                .error(),
            arithmetic_error::invalid_tensor_shape);
  EXPECT_EQ(tensor::mma<float32_t>(
                c, tensor::matrix_view<const float32_t>{av.data(), 2, 2, 4},
                tensor::matrix_view<const float32_t>{bv.data(), 1, 2, 3},
                tensor::matrix_view<const float32_t>{cv.data(), 2, 2, 3},
                tensor::matrix_view<float32_t>{dv.data(), 2, 2, 3})
                .error(),
            arithmetic_error::invalid_tensor_shape);
}

TEST(TensorArithmetic, TensorNanSubnormalAndControls) {
  context c;
  const tensor::tile<1, 1, float32_t> one{{float32_t::from_bits(0x3f800000)}},
      zero{}, subnormal{{float32_t::from_bits(1)}},
      nan{{float32_t::from_bits(0x7fc00001)}};
  const auto preserved = tensor::mma<float32_t>(c, subnormal, one, zero);
  ASSERT_TRUE(preserved);
  EXPECT_EQ(preserved->value(0, 0).bits(), 1u);
  const auto nan_result = tensor::mma<float32_t>(c, nan, one, zero);
  ASSERT_TRUE(nan_result);
  EXPECT_TRUE(is_nan(nan_result->value(0, 0)));
  EXPECT_EQ(tensor::mma<float32_t>(
                c, one, one, zero,
                {.accumulator_rounding = rounding_mode::nearest_away})
                .error(),
            arithmetic_error::unsupported_rounding);
  EXPECT_EQ(tensor::mma<float32_t>(c, one, one, zero,
                                   {.saturation = saturation_mode::finite})
                .error(),
            arithmetic_error::unsupported_saturation);
}

}  // namespace ptxsim::arith::test
