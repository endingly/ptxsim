#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <limits>
#include <thread>
#include <type_traits>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {

TEST(ScalarArithmetic, GenericScalarAndInteger) {
  context c;
  auto f = add(c, float32_t::from_bits(0x3f800000),
               float32_t::from_bits(0x40000000));
  ASSERT_TRUE(f);
  EXPECT_EQ(f->value.bits(), 0x40400000u);
  auto i = add(c, int32_t{0x7fffffff}, int32_t{1});
  ASSERT_TRUE(i);
  EXPECT_EQ(std::bit_cast<uint32_t>(i->value), 0x80000000u);
  EXPECT_EQ(bit_reverse<uint8_t>(1), 0x80);
}

TEST(ScalarArithmetic, ControlsRejectUnsupported) {
  context c;
  EXPECT_FALSE(add(c, float16_t::from_bits(0x3c00),
                   float16_t::from_bits(0x3c00),
                   {.rounding = rounding_mode::toward_zero}));
}

TEST(ScalarArithmetic, BfloatFmaIsSingleRounding) {
  context c;
  const auto r =
      fma(c, bfloat16_t::from_bits(0x3f82), bfloat16_t::from_bits(0x3fa0),
          bfloat16_t::from_bits(0x0001));
  ASSERT_TRUE(r);
  EXPECT_EQ(r->value.bits(), 0x3fa3u);
}

TEST(ScalarArithmetic, ThreadLocalState) {
  context c;
  auto worker = [&](rounding_mode m, uint32_t expected) {
    for (int i = 0; i < 1000; ++i) {
      auto r = add(c, float32_t::from_bits(0x3f800000),
                   float32_t::from_bits(0x33800000), {.rounding = m});
      ASSERT_TRUE(r);
      EXPECT_EQ(r->value.bits(), expected);
    }
  };
  std::thread a(worker, rounding_mode::toward_positive, 0x3f800001),
      b(worker, rounding_mode::toward_negative, 0x3f800000);
  a.join();
  b.join();
}

TEST(ScalarArithmetic, CompareAndBorrowEdges) {
  context c;
  EXPECT_TRUE(compare(c, float32_t::from_bits(0xbf800000),
                      float32_t::from_bits(0x3f800000),
                      {.relation = comparison_relation::less})
                  .value);
  EXPECT_TRUE(
      compare(c, float32_t::from_bits(0x80000000), float32_t{}, {}).value);
  EXPECT_FALSE(compare(c, float32_t::from_bits(0x7fc00000), float32_t{},
                       {.nan = nan_comparison_mode::ordered})
                   .value);
  auto r =
      sub_with_borrow(uint32_t{}, std::numeric_limits<uint32_t>::max(), true);
  EXPECT_TRUE(r.status.borrow);
  EXPECT_EQ(r.value, 0u);
}

TEST(ScalarArithmetic, IntegerControlsProductsAndExtendedPrecision) {
  context c;
  const auto sat_add = add(c, std::numeric_limits<int8_t>::max(), int8_t{1},
                           {.overflow = integer_overflow_mode::saturate});
  ASSERT_TRUE(sat_add);
  EXPECT_EQ(sat_add->value, std::numeric_limits<int8_t>::max());
  EXPECT_TRUE(sat_add->status.overflow);
  const auto sat_mul = mul(c, int8_t{100}, int8_t{2},
                           {.overflow = integer_overflow_mode::saturate});
  ASSERT_TRUE(sat_mul);
  EXPECT_EQ(sat_mul->value, std::numeric_limits<int8_t>::max());
  auto high = mul(c, int8_t{-2}, int8_t{2}, {.part = product_part::high});
  ASSERT_TRUE(high);
  EXPECT_EQ(high->value, int8_t{-1});
  auto wide = mul<integer_wide_t<int8_t>>(c, int8_t{-100}, int8_t{2},
                                          {.part = product_part::wide});
  ASSERT_TRUE(wide);
  EXPECT_EQ(wide->value, -200);
  auto wide64 =
      mul<integer_wide_t<int64_t>>(c, std::numeric_limits<int64_t>::max(),
                                   int64_t{2}, {.part = product_part::wide});
  ASSERT_TRUE(wide64);
  EXPECT_EQ(wide64->value,
            integer_wide_t<int64_t>(std::numeric_limits<int64_t>::max()) * 2);
  EXPECT_EQ(mul(c, int8_t{2}, int8_t{3}, {.part = product_part::wide}).error(),
            arithmetic_error::unsupported_type_combination);
  EXPECT_EQ(mul(c, int8_t{2}, int8_t{3},
                {.part = product_part::high,
                 .overflow = integer_overflow_mode::saturate})
                .error(),
            arithmetic_error::unsupported_overflow_mode);
  auto mad_wide = mad<integer_wide_t<int8_t>>(
      c, int8_t{10}, int8_t{20}, int16_t{5}, {.part = product_part::wide});
  ASSERT_TRUE(mad_wide);
  EXPECT_EQ(mad_wide->value, 205);
  const auto add_cc = add_with_carry(uint8_t{255}, uint8_t{255}, true);
  EXPECT_EQ(add_cc.value, uint8_t{255});
  EXPECT_TRUE(add_cc.status.carry);
  const auto sub_cc = sub_with_borrow(uint8_t{}, uint8_t{}, true);
  EXPECT_EQ(sub_cc.value, uint8_t{255});
  EXPECT_TRUE(sub_cc.status.borrow);
  const auto mad_cc = mad_with_carry(uint8_t{255}, uint8_t{255}, uint8_t{255});
  EXPECT_EQ(mad_cc.value, uint8_t{});
  EXPECT_TRUE(mad_cc.status.carry);
  EXPECT_EQ(div(c, int8_t{1}, int8_t{}).error(),
            arithmetic_error::division_by_zero);
  auto rem_min = rem(c, std::numeric_limits<int8_t>::min(), int8_t{-1});
  ASSERT_TRUE(rem_min);
  EXPECT_EQ(rem_min->value, 0);
  auto neg_min = neg(c, std::numeric_limits<int8_t>::min(),
                     {.overflow = integer_overflow_mode::saturate});
  ASSERT_TRUE(neg_min);
  EXPECT_EQ(neg_min->value, std::numeric_limits<int8_t>::max());
  auto distance = sad(c, int8_t{-128}, int8_t{127}, int8_t{1},
                      {.overflow = integer_overflow_mode::saturate});
  ASSERT_TRUE(distance);
  EXPECT_EQ(distance->value, std::numeric_limits<int8_t>::max());
}

TEST(ScalarArithmetic, MadHighSaturatesOnlyFinalSignedResult) {
  context c;
  const product_control high_saturate{
      .part = product_part::high,
      .overflow = integer_overflow_mode::saturate};

  const auto positive = mad(c, std::numeric_limits<int32_t>::max(),
                            std::numeric_limits<int32_t>::max(),
                            std::numeric_limits<int32_t>::max(), high_saturate);
  ASSERT_TRUE(positive);
  EXPECT_EQ(positive->value, std::numeric_limits<int32_t>::max());
  EXPECT_TRUE(positive->status.overflow);

  const auto negative = mad(c, std::numeric_limits<int32_t>::min(),
                            std::numeric_limits<int32_t>::max(),
                            std::numeric_limits<int32_t>::min(), high_saturate);
  ASSERT_TRUE(negative);
  EXPECT_EQ(negative->value, std::numeric_limits<int32_t>::min());
  EXPECT_TRUE(negative->status.overflow);

  EXPECT_EQ(mul(c, std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::max(), high_saturate)
                .error(),
            arithmetic_error::unsupported_overflow_mode);

  EXPECT_EQ(mad(c, int8_t{2}, int8_t{3}, int8_t{4}, high_saturate).error(),
            arithmetic_error::unsupported_overflow_mode);
  EXPECT_EQ(mad(c, int16_t{2}, int16_t{3}, int16_t{4}, high_saturate).error(),
            arithmetic_error::unsupported_overflow_mode);
  EXPECT_EQ(mad(c, int64_t{2}, int64_t{3}, int64_t{4}, high_saturate).error(),
            arithmetic_error::unsupported_overflow_mode);
  EXPECT_EQ(
      mad(c, uint32_t{2}, uint32_t{3}, uint32_t{4}, high_saturate).error(),
      arithmetic_error::unsupported_overflow_mode);
}

TEST(ScalarArithmetic, EightBitIntegerAndBitBoundaries) {
  context c;
  for (unsigned a = 0; a != 256; ++a) {
    const auto x = static_cast<uint8_t>(a);
    EXPECT_EQ(popcount(x), std::popcount(x));
    EXPECT_EQ(count_leading_zeros(x), std::countl_zero(x));
    EXPECT_EQ(find_most_significant(x), x == 0 ? -1 : 7 - std::countl_zero(x));
    uint8_t reversed{};
    for (unsigned bit = 0; bit != 8; ++bit)
      reversed |= ((x >> bit) & 1u) << (7 - bit);
    EXPECT_EQ(bit_reverse(x), reversed);
    for (unsigned b = 0; b != 256; ++b) {
      const auto y = static_cast<uint8_t>(b);
      auto sum = add(c, x, y);
      auto difference = sub(c, x, y);
      auto product = mul(c, x, y);
      const auto sx = std::bit_cast<int8_t>(x);
      const auto sy = std::bit_cast<int8_t>(y);
      auto signed_wide =
          mul<integer_wide_t<int8_t>>(c, sx, sy, {.part = product_part::wide});
      ASSERT_TRUE(sum);
      ASSERT_TRUE(difference);
      ASSERT_TRUE(product);
      ASSERT_TRUE(signed_wide);
      EXPECT_EQ(sum->value, static_cast<uint8_t>(a + b));
      EXPECT_EQ(difference->value, static_cast<uint8_t>(a - b));
      EXPECT_EQ(product->value, static_cast<uint8_t>(a * b));
      EXPECT_EQ(signed_wide->value,
                static_cast<int16_t>(sx) * static_cast<int16_t>(sy));
    }
    for (unsigned offset = 0; offset != 12; ++offset)
      for (unsigned width = 0; width != 12; ++width) {
        const uint16_t mask = width >= 8   ? 0xff
                              : width == 0 ? 0
                                           : (uint16_t{1} << width) - 1;
        const uint8_t extract = offset >= 8 ? 0 : (x >> offset) & mask;
        EXPECT_EQ(bit_extract(x, offset, width), extract);
        const uint8_t inserted =
            offset >= 8 || width == 0
                ? x
                : static_cast<uint8_t>((x & ~(mask << offset)) |
                                       ((uint8_t{0xa5} & mask) << offset));
        EXPECT_EQ(bit_insert(x, uint8_t{0xa5}, offset, width), inserted);
      }
    for (unsigned shift = 0; shift != 40; ++shift) {
      const unsigned s = shift % 8;
      const uint8_t expected = s ? (x >> s) | (uint8_t{0x5a} << (8 - s)) : x;
      EXPECT_EQ(funnel_shift(x, uint8_t{0x5a}, shift), expected);
    }
  }
  EXPECT_EQ(popcount(int8_t{-1}), 8u);
}

TEST(ScalarArithmetic, CapabilityConstrainedMixedFma) {
  static_assert(operation_capability<scalar_operation::fma, float32_t,
                                     float16_t, float16_t, float32_t>::value);
  static_assert(!scalar_addable<float8_e4m3_t>);
  context c;
  auto r = fma<float32_t>(c, float16_t::from_bits(0x4000),
                          float16_t::from_bits(0x4200),
                          float32_t::from_bits(0x3f800000));
  ASSERT_TRUE(r);
  EXPECT_EQ(r->value.bits(), 0x40e00000u);
  auto sum = add<float32_t>(c, float16_t::from_bits(0x4000),
                            float32_t::from_bits(0x3f800000));
  ASSERT_TRUE(sum);
  EXPECT_EQ(sum->value.bits(), 0x40400000u);
}

TEST(ScalarArithmetic, FmaControlRegressions) {
  context c;
  const auto min_normal = float32_t::from_bits(0x00800000);
  auto ftz = fma(c, min_normal, float32_t::from_bits(0x3f000000), float32_t{},
                 {.subnormal = subnormal_mode::flush_input_and_output});
  ASSERT_TRUE(ftz);
  EXPECT_EQ(ftz->value.bits(), 0u);
  EXPECT_EQ(fma(c, float64_t::from_bits(0x3ff0000000000000ULL),
                float64_t::from_bits(0x3ff0000000000000ULL), float64_t{},
                {.subnormal = subnormal_mode::flush_output})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);
}

TEST(ScalarArithmetic, FloatingControlCapabilityMatrix) {
  static_assert(floating_operation_control_capability<
                scalar_operation::add, float16_t>::supports(
                saturation_mode::zero_to_one));
  static_assert(floating_operation_control_capability<
                scalar_operation::fma, bfloat16_t>::supports(
                activation_mode::relu));
  static_assert(!floating_operation_control_capability<
                scalar_operation::fma, float16_t>::supports(
                {.saturation = saturation_mode::zero_to_one,
                 .activation = activation_mode::relu}));
  static_assert(!floating_operation_control_capability<
                scalar_operation::sqrt, float32_t>::supports(
                saturation_mode::zero_to_one));
  static_assert(!floating_operation_control_capability<
                scalar_operation::div, float32_t>::supports(
                saturation_mode::zero_to_one));

  context c;
  const auto h_one = float16_t::from_bits(0x3c00);
  const auto h_two = float16_t::from_bits(0x4000);
  EXPECT_EQ(add(c, h_one, h_two,
                {.saturation = saturation_mode::zero_to_one})
                ->value.bits(),
            h_one.bits());
  EXPECT_TRUE(add(c, h_one, h_one,
                  {.subnormal = subnormal_mode::flush_input_and_output}));
  EXPECT_EQ(add(c, h_one, h_one,
                {.subnormal = subnormal_mode::flush_input})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);

  const auto bf_one = bfloat16_t::from_bits(0x3f80);
  const auto bf_minus_two = bfloat16_t::from_bits(0xc000);
  EXPECT_EQ(fma(c, bf_one, bf_one, bf_minus_two,
                {.activation = activation_mode::relu})
                ->value.bits(),
            0u);
  EXPECT_EQ(fma(c, float16_t::from_bits(0x7e01), h_one, h_one,
                {.activation = activation_mode::relu})
                ->value.bits(),
            0x7e00u);
  EXPECT_EQ(fma(c, bfloat16_t::from_bits(0x7fc1), bf_one, bf_one,
                {.activation = activation_mode::relu})
                ->value.bits(),
            0x7fc0u);
  EXPECT_EQ(fma(c, h_one, h_one, h_one,
                {.saturation = saturation_mode::zero_to_one,
                 .activation = activation_mode::relu})
                .error(),
            arithmetic_error::unsupported_activation);
  EXPECT_EQ(add(c, bf_one, bf_one,
                {.subnormal = subnormal_mode::flush_input_and_output})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);

  const auto f_one = float32_t::from_bits(0x3f800000);
  EXPECT_EQ(sqrt(c, f_one, {.saturation = saturation_mode::zero_to_one})
                .error(),
            arithmetic_error::unsupported_saturation);
  EXPECT_EQ(div(c, f_one, f_one,
                {.saturation = saturation_mode::zero_to_one})
                .error(),
            arithmetic_error::unsupported_saturation);

  const auto d_one = float64_t::from_bits(0x3ff0000000000000ULL);
  EXPECT_EQ(add(c, d_one, d_one,
                {.saturation = saturation_mode::zero_to_one})
                .error(),
            arithmetic_error::unsupported_saturation);
  EXPECT_EQ(add(c, d_one, d_one,
                {.subnormal = subnormal_mode::flush_input_and_output})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);

  EXPECT_EQ(fma<float32_t>(c, h_one, h_one, f_one,
                            {.subnormal = subnormal_mode::flush_input_and_output})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(fma<float32_t>(c, h_one, h_one, f_one,
                            {.saturation = saturation_mode::zero_to_one})
                ->value.bits(),
            f_one.bits());
}

TEST(ScalarArithmetic, MadIsFusedAndControlsAreTyped) {
  context c;
  const auto a = float32_t::from_bits(0x3f800001u);
  const auto b = float32_t::from_bits(0x3f7fffffu);
  const auto minus_one = float32_t::from_bits(0xbf800000u);
  const auto fused = fma(c, a, b, minus_one);
  const auto multiply_add = add(c, mul(c, a, b)->value, minus_one);
  const auto madd = mad(c, a, b, minus_one);
  ASSERT_TRUE(fused && multiply_add && madd);
  EXPECT_EQ(madd->value.bits(), fused->value.bits());
  EXPECT_EQ(madd->status.inexact, fused->status.inexact);
  EXPECT_NE(madd->value.bits(), multiply_add->value.bits());

  const auto positive = float32_t::from_bits(0x40000000u);
  const auto negative = float32_t::from_bits(0xbf800000u);
  const auto nan = float32_t::from_bits(0x7fc00001u);
  EXPECT_EQ(add(c, positive, positive,
                {.saturation = saturation_mode::zero_to_one})->value.bits(),
            0x3f800000u);
  EXPECT_EQ(add(c, negative, float32_t{},
                {.saturation = saturation_mode::zero_to_one})->value.bits(),
            0u);
  EXPECT_EQ(add(c, nan, float32_t{},
                {.saturation = saturation_mode::zero_to_one})->value.bits(),
            0u);
  EXPECT_EQ(add(c, negative, float32_t{},
                {.activation = activation_mode::relu})
                .error(), arithmetic_error::unsupported_activation);
  EXPECT_EQ(add(c, positive, positive,
                {.subnormal = subnormal_mode::flush_input})
                .error(), arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(add(c, positive, positive,
                {.saturation = saturation_mode::finite})
                .error(), arithmetic_error::unsupported_saturation);
  EXPECT_EQ(add(c, float64_t::from_bits(0x3ff0000000000000ULL),
                float64_t::from_bits(0x3ff0000000000000ULL),
                {.activation = activation_mode::relu})
                .error(), arithmetic_error::unsupported_activation);
}

TEST(ScalarArithmetic, PackedLaneOperationsReuseScalarCapabilities) {
  context c;
  const auto a = pack<float16x2_t>(
      {float16_t::from_bits(0x3c00), float16_t::from_bits(0x4000)});
  const auto b = pack<float16x2_t>(
      {float16_t::from_bits(0x4000), float16_t::from_bits(0x4200)});
  auto sum = add(c, a, b);
  auto product = mul(c, a, b);
  ASSERT_TRUE(sum);
  ASSERT_TRUE(product);
  EXPECT_EQ(sum->value[0].bits(), 0x4200);
  EXPECT_EQ(sum->value[1].bits(), 0x4500);
  EXPECT_EQ(product->value[0].bits(), 0x4000);
  EXPECT_EQ(product->value[1].bits(), 0x4600);
  const auto bf = pack<bfloat16x2_t>(
      {bfloat16_t::from_bits(0x3f80), bfloat16_t::from_bits(0x4000)});
  auto fused = fma(c, bf, bf, bf);
  ASSERT_TRUE(fused);
  EXPECT_EQ(fused->value[0].bits(), 0x4000);
  EXPECT_EQ(fused->value[1].bits(), 0x40c0);
  static_assert(!packed_operation_capability<packed_operation::add,
                                             float8_e4m3x2_t>::value);
}

namespace {

void expect_status(const floating_status& s, bool invalid = false,
                   bool overflow = false, bool underflow = false,
                   bool inexact = false) {
  EXPECT_EQ(s.invalid, invalid);
  EXPECT_FALSE(s.divide_by_zero);
  EXPECT_EQ(s.overflow, overflow);
  EXPECT_EQ(s.underflow, underflow);
  EXPECT_EQ(s.inexact, inexact);
  EXPECT_FALSE(s.model_dependent);
}

template <typename T>
void check_float_edges(T zero, T negative_zero, T one, T two, T half,
                       T min_normal, T min_subnormal, T max_finite,
                       T infinity) {
  context c;
  const auto signed_zero = add(c, negative_zero, negative_zero);
  const auto invalid = mul(c, zero, infinity);
  const auto overflow = mul(c, max_finite, two);
  const auto underflow = mul(c, min_subnormal, half);
  const auto input_ftz =
      mul(c, min_subnormal, one, {.subnormal = subnormal_mode::flush_input});
  const auto output_ftz =
      mul(c, min_normal, half, {.subnormal = subnormal_mode::flush_output});
  ASSERT_TRUE(signed_zero);
  ASSERT_TRUE(invalid);
  ASSERT_TRUE(overflow);
  ASSERT_TRUE(underflow);
  EXPECT_EQ(input_ftz.error(), arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(output_ftz.error(), arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(signed_zero->value.bits(), negative_zero.bits());
  EXPECT_TRUE(is_nan(invalid->value));
  EXPECT_EQ(overflow->value.bits(), infinity.bits());
  EXPECT_EQ(underflow->value.bits(), zero.bits());
  expect_status(signed_zero->status);
  expect_status(invalid->status, true);
  expect_status(overflow->status, false, true, false, true);
  expect_status(underflow->status, false, false, true, true);
}

template <typename T>
constexpr T reference_from_bits(std::make_unsigned_t<T> value) {
  if constexpr (std::is_signed_v<T>)
    return std::bit_cast<T>(value);
  else
    return value;
}

template <typename T>
void check_integer_samples() {
  using U = std::make_unsigned_t<T>;
  using W = integer_wide_t<T>;
  using UW = std::make_unsigned_t<W>;
  constexpr unsigned width = std::numeric_limits<U>::digits;
  const std::array values = [] {
    if constexpr (std::is_signed_v<T>)
      return std::array<T, 5>{std::numeric_limits<T>::min(), T{-1}, T{}, T{1},
                              std::numeric_limits<T>::max()};
    else
      return std::array<T, 5>{T{}, T{1}, T{0x55},
                              T(std::numeric_limits<T>::max() - 1),
                              std::numeric_limits<T>::max()};
  }();
  uint64_t seed = 0x8badf00d12345678ULL;
  const auto sample = [&](T a, T b) {
    const U ua = std::bit_cast<U>(a), ub = std::bit_cast<U>(b);
    context c;
    const auto sum = add(c, a, b);
    const auto difference = sub(c, a, b);
    const auto low = mul(c, a, b);
    const auto high = mul(c, a, b, {.part = product_part::high});
    const auto wide = mul<W>(c, a, b, {.part = product_part::wide});
    const auto carry = add_with_carry(a, b, true);
    const auto borrow = sub_with_borrow(a, b, true);
    ASSERT_TRUE(sum);
    ASSERT_TRUE(difference);
    ASSERT_TRUE(low);
    ASSERT_TRUE(high);
    ASSERT_TRUE(wide);
    EXPECT_EQ(sum->value, reference_from_bits<T>(U(ua + ub)));
    EXPECT_EQ(difference->value, reference_from_bits<T>(U(ua - ub)));
    const UW product = UW(ua) * UW(ub);
    const UW high_product =
        std::is_signed_v<T> ? static_cast<UW>(W(a) * W(b)) : product;
    EXPECT_EQ(low->value, reference_from_bits<T>(U(product)));
    EXPECT_EQ(high->value, reference_from_bits<T>(U(high_product >> width)));
    if constexpr (std::is_signed_v<T>)
      EXPECT_EQ(wide->value, W(a) * W(b));
    else
      EXPECT_EQ(wide->value, W(ua) * W(ub));
    const UW carried = UW(ua) + UW(ub) + 1;
    EXPECT_EQ(carry.value, reference_from_bits<T>(U(carried)));
    EXPECT_EQ(carry.status.carry, (carried >> width) != 0);
    EXPECT_EQ(borrow.value, reference_from_bits<T>(U(ua - ub - 1)));
    EXPECT_EQ(borrow.status.borrow, ua <= ub);
    bool signed_overflow = false;
    if constexpr (std::is_signed_v<T>)
      signed_overflow =
          a == std::numeric_limits<T>::min() && b == static_cast<T>(-1);
    if (b != 0 && !signed_overflow) {
      ASSERT_TRUE(div(c, a, b));
      ASSERT_TRUE(rem(c, a, b));
      EXPECT_EQ(div(c, a, b)->value, a / b);
      EXPECT_EQ(rem(c, a, b)->value, a % b);
    }
  };
  for (const auto a : values)
    for (const auto b : values)
      sample(a, b);
  for (unsigned i = 0; i != 24; ++i) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    const T a = reference_from_bits<T>(U(seed));
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    sample(a, reference_from_bits<T>(U(seed)));
  }
}

}  // namespace

TEST(ScalarArithmetic, CapabilityDrivenFloatBoundaryCoverage) {
  static_assert(scalar_addable<float16_t> && scalar_addable<float32_t> &&
                scalar_addable<float64_t> && scalar_addable<bfloat16_t>);
  static_assert(
      !scalar_addable<float8_e4m3_t> && !scalar_addable<float8_e5m2_t> &&
      !scalar_addable<float6_e2m3_t> && !scalar_addable<float6_e3m2_t> &&
      !scalar_addable<float4_e2m1_t>);
  check_float_edges(float16_t{}, float16_t::from_bits(0x8000),
                    float16_t::from_bits(0x3c00), float16_t::from_bits(0x4000),
                    float16_t::from_bits(0x3800), float16_t::from_bits(0x0400),
                    float16_t::from_bits(1), float16_t::from_bits(0x7bff),
                    float16_t::from_bits(0x7c00));
  check_float_edges(
      float32_t{}, float32_t::from_bits(0x80000000),
      float32_t::from_bits(0x3f800000), float32_t::from_bits(0x40000000),
      float32_t::from_bits(0x3f000000), float32_t::from_bits(0x00800000),
      float32_t::from_bits(1), float32_t::from_bits(0x7f7fffff),
      float32_t::from_bits(0x7f800000));
  check_float_edges(float64_t{}, float64_t::from_bits(0x8000000000000000ULL),
                    float64_t::from_bits(0x3ff0000000000000ULL),
                    float64_t::from_bits(0x4000000000000000ULL),
                    float64_t::from_bits(0x3fe0000000000000ULL),
                    float64_t::from_bits(0x0010000000000000ULL),
                    float64_t::from_bits(1),
                    float64_t::from_bits(0x7fefffffffffffffULL),
                    float64_t::from_bits(0x7ff0000000000000ULL));
  check_float_edges(
      bfloat16_t{}, bfloat16_t::from_bits(0x8000),
      bfloat16_t::from_bits(0x3f80), bfloat16_t::from_bits(0x4000),
      bfloat16_t::from_bits(0x3f00), bfloat16_t::from_bits(0x0080),
      bfloat16_t::from_bits(1), bfloat16_t::from_bits(0x7f7f),
      bfloat16_t::from_bits(0x7f80));
}

TEST(ScalarArithmetic, DirectedRoundingFmaAndIntegerTables) {
  context c;
  for (const auto mode :
       {rounding_mode::nearest_even, rounding_mode::toward_zero,
        rounding_mode::toward_negative, rounding_mode::toward_positive}) {
    const auto f32 = add(c, float32_t::from_bits(0x3f800000),
                         float32_t::from_bits(0x33800000), {.rounding = mode});
    const auto f64 =
        add(c, float64_t::from_bits(0x3ff0000000000000ULL),
            float64_t::from_bits(0x3ca0000000000000ULL), {.rounding = mode});
    ASSERT_TRUE(f32);
    ASSERT_TRUE(f64);
    EXPECT_EQ(f32->value.bits(),
              mode == rounding_mode::toward_positive ? 0x3f800001 : 0x3f800000);
    EXPECT_EQ(f64->value.bits(), mode == rounding_mode::toward_positive
                                     ? 0x3ff0000000000001ULL
                                     : 0x3ff0000000000000ULL);
    expect_status(f32->status, false, false, false, true);
    expect_status(f64->status, false, false, false, true);
  }
  const auto fused =
      fma(c, float32_t::from_bits(0x3f800001), float32_t::from_bits(0x3f7fffff),
          float32_t::from_bits(0xbf800000));
  ASSERT_TRUE(fused);
  EXPECT_EQ(fused->value.bits(), 0x337ffffe);
  check_integer_samples<int8_t>();
  check_integer_samples<uint8_t>();
  check_integer_samples<int16_t>();
  check_integer_samples<uint16_t>();
  check_integer_samples<int32_t>();
  check_integer_samples<uint32_t>();
  check_integer_samples<int64_t>();
  check_integer_samples<uint64_t>();
}

TEST(ScalarArithmetic, BfloatScalarUsesNearestEvenOnly) {
  context c;
  const auto positive =
      add(c, bfloat16_t::from_bits(0x3f80), bfloat16_t::from_bits(0x3b80),
          {.rounding = rounding_mode::nearest_even});
  const auto negative =
      add(c, bfloat16_t::from_bits(0xbf80), bfloat16_t::from_bits(0xbb80),
          {.rounding = rounding_mode::nearest_even});
  ASSERT_TRUE(positive);
  ASSERT_TRUE(negative);
  EXPECT_EQ(positive->value.bits(), 0x3f80u);
  EXPECT_EQ(negative->value.bits(), 0xbf80u);
  expect_status(positive->status, false, false, false, true);
  expect_status(negative->status, false, false, false, true);

  for (const auto mode :
       {rounding_mode::toward_zero, rounding_mode::toward_negative,
        rounding_mode::toward_positive}) {
    EXPECT_EQ(add(c, bfloat16_t::from_bits(0x3f80),
                  bfloat16_t::from_bits(0x3b80), {.rounding = mode})
                  .error(),
              arithmetic_error::unsupported_rounding);
    EXPECT_EQ(sub(c, bfloat16_t::from_bits(0x3f80),
                  bfloat16_t::from_bits(0x3b80), {.rounding = mode})
                  .error(),
              arithmetic_error::unsupported_rounding);
    EXPECT_EQ(mul(c, bfloat16_t::from_bits(0x3f80),
                  bfloat16_t::from_bits(0x3f80), {.rounding = mode})
                  .error(),
              arithmetic_error::unsupported_rounding);
    EXPECT_EQ(
        fma(c, bfloat16_t::from_bits(0x3f80), bfloat16_t::from_bits(0x3f80),
            bfloat16_t::from_bits(0x3f80), {.rounding = mode})
            .error(),
        arithmetic_error::unsupported_rounding);
    EXPECT_EQ(abs(c, bfloat16_t::from_bits(0xbf80), {.rounding = mode}).error(),
              arithmetic_error::unsupported_rounding);
    EXPECT_EQ(neg(c, bfloat16_t::from_bits(0x3f80), {.rounding = mode}).error(),
              arithmetic_error::unsupported_rounding);
    EXPECT_EQ(min(c, bfloat16_t::from_bits(0x3f80),
                  bfloat16_t::from_bits(0xbf80), {.rounding = mode})
                  .error(),
              arithmetic_error::unsupported_rounding);
    EXPECT_EQ(max(c, bfloat16_t::from_bits(0x3f80),
                  bfloat16_t::from_bits(0xbf80), {.rounding = mode})
                  .error(),
              arithmetic_error::unsupported_rounding);
  }
}

}  // namespace ptxsim::arith::test
