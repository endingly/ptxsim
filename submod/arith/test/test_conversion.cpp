#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <ptxsim/arith/arith.hpp>

namespace ptxsim::arith::test {
namespace {

constexpr fp_class ieee_class(uint16_t bits, uint16_t exponent_mask,
                              uint16_t fraction_mask, uint16_t quiet_bit) {
  const auto exponent = bits & exponent_mask;
  const auto fraction = bits & fraction_mask;
  if (exponent == 0)
    return fraction == 0 ? fp_class::zero : fp_class::subnormal;
  if (exponent != exponent_mask)
    return fp_class::normal;
  if (fraction == 0)
    return fp_class::infinity;
  return (fraction & quiet_bit) != 0 ? fp_class::quiet_nan
                                     : fp_class::signaling_nan;
}

constexpr fp_class finite_class(uint8_t bits, uint8_t storage_mask,
                                uint8_t exponent_mask, uint8_t fraction_mask) {
  bits &= storage_mask;
  if ((bits & exponent_mask) != 0)
    return fp_class::normal;
  return (bits & fraction_mask) == 0 ? fp_class::zero : fp_class::subnormal;
}

constexpr fp_class e4m3_class(uint8_t bits) {
  bits &= 0xff;
  if ((bits & 0x78) == 0)
    return (bits & 0x07) == 0 ? fp_class::zero : fp_class::subnormal;
  return (bits & 0x7f) == 0x7f ? fp_class::quiet_nan : fp_class::normal;
}

// This is a test-only field decoder.  It intentionally does not use the
// production low-precision unpacker or encoder.
constexpr uint32_t decoded_f32_bits(uint32_t bits, uint32_t storage_mask,
                                    uint32_t sign_mask, uint32_t exponent_mask,
                                    uint32_t fraction_mask,
                                    unsigned fraction_bits, int exponent_bias,
                                    bool ieee_specials = false,
                                    bool e4m3_nan = false) {
  bits &= storage_mask;
  const auto sign = sign_mask != 0 && (bits & sign_mask) != 0 ? 0x80000000u : 0;
  const auto fraction = bits & fraction_mask;
  const auto exponent = (bits & exponent_mask) >> fraction_bits;
  const auto all_exponents = exponent_mask >> fraction_bits;
  if (e4m3_nan && (bits & 0x7f) == 0x7f)
    return sign | 0x7fc00000u;
  if (ieee_specials && exponent == all_exponents) {
    if (fraction == 0)
      return sign | 0x7f800000u;
    return sign | 0x7f800000u | (fraction << (23 - fraction_bits)) |
           0x00400000u;
  }
  if (exponent != 0)
    return sign | ((exponent - exponent_bias + 127) << 23) |
           (fraction << (23 - fraction_bits));
  if (fraction == 0)
    return sign;

  const auto subnormal_quantum =
      1 - exponent_bias - static_cast<int>(fraction_bits);
  if (subnormal_quantum < -126)
    return sign | (fraction << (subnormal_quantum + 149));

  unsigned top = 0;
  for (auto shifted = fraction; shifted > 1; shifted >>= 1)
    ++top;
  const auto f32_exponent = static_cast<uint32_t>(
      1 - exponent_bias - static_cast<int>(fraction_bits) +
      static_cast<int>(top) + 127);
  return sign | (f32_exponent << 23) | ((fraction << (23 - top)) & 0x007fffffu);
}

template <typename T>
void expect_f32_roundtrip(context& c, T input, uint8_t expected_bits) {
  const auto widened = cvt<float32_t>(c, input);
  ASSERT_TRUE(widened);
  const auto roundtrip = cvt<T>(c, widened->value);
  ASSERT_TRUE(roundtrip);
  EXPECT_EQ(roundtrip->value.bits(), expected_bits);
}

template <typename T>
void expect_f32_decode(context& c, T input, uint32_t expected_bits) {
  const auto widened = cvt<float32_t>(c, input);
  ASSERT_TRUE(widened);
  EXPECT_EQ(widened->value.bits(), expected_bits);
}

template <typename T>
void expect_encoded(context& c, uint32_t source_bits, uint8_t expected_bits,
                    conversion_control control = {}) {
  SCOPED_TRACE(::testing::Message()
               << "source=0x" << std::hex << source_bits
               << " rounding=" << static_cast<unsigned>(control.rounding));
  const auto encoded = cvt<T>(c, float32_t::from_bits(source_bits), control);
  ASSERT_TRUE(encoded);
  EXPECT_EQ(encoded->value.bits(), expected_bits);
}

template <typename... T>
void expect_f32_conversion_routes(context& c, std::type_identity<T>...) {
  (
      [&] {
        static_assert(convertible_to<T, float32_t>);
        static_assert(convertible_to<float32_t, T>);
        const auto narrow = cvt<T>(c, float32_t::from_bits(0x3f800000));
        ASSERT_TRUE(narrow);
        EXPECT_TRUE(cvt<float32_t>(c, narrow->value));
      }(),
      ...);
}

}  // namespace

TEST(Conversion, ExhaustiveF16AndBf16ClassificationAndCanonicalRoundTrip) {
  context c;
  for (unsigned raw = 0; raw != 65536; ++raw) {
    SCOPED_TRACE(::testing::Message()
                 << "raw=0x" << std::hex << raw << " control=default");
    const auto bits = static_cast<uint16_t>(raw);
    const auto f16 = float16_t::from_bits(bits);
    const auto bf16 = bfloat16_t::from_bits(bits);
    const auto f16_class = ieee_class(bits, 0x7c00, 0x03ff, 0x0200);
    const auto bf16_class = ieee_class(bits, 0x7f80, 0x007f, 0x0040);

    EXPECT_TRUE(is_valid_encoding(f16));
    EXPECT_TRUE(is_valid_encoding(bf16));
    EXPECT_EQ(classify(f16), f16_class);
    EXPECT_EQ(classify(bf16), bf16_class);

    const auto f16_wide = cvt<float32_t>(c, f16);
    ASSERT_TRUE(f16_wide);
    EXPECT_EQ(
        f16_wide->value.bits(),
        decoded_f32_bits(bits, 0xffff, 0x8000, 0x7c00, 0x03ff, 10, 15, true));
    const auto f16_back = cvt<float16_t>(c, f16_wide->value);
    ASSERT_TRUE(f16_back);
    EXPECT_EQ(f16_back->value.bits(),
              static_cast<uint16_t>(
                  bits | (f16_class == fp_class::signaling_nan ? 0x0200 : 0)));
    EXPECT_EQ(f16_wide->status.invalid, f16_class == fp_class::signaling_nan);

    const auto bf16_wide = cvt<float32_t>(c, bf16);
    ASSERT_TRUE(bf16_wide);
    EXPECT_EQ(
        bf16_wide->value.bits(),
        decoded_f32_bits(bits, 0xffff, 0x8000, 0x7f80, 0x007f, 7, 127, true));
    const auto bf16_back = cvt<bfloat16_t>(c, bf16_wide->value);
    ASSERT_TRUE(bf16_back);
    EXPECT_EQ(bf16_back->value.bits(),
              static_cast<uint16_t>(
                  bits | (bf16_class == fp_class::signaling_nan ? 0x0040 : 0)));
    EXPECT_EQ(bf16_wide->status.invalid, bf16_class == fp_class::signaling_nan);
  }
}

TEST(Conversion, ExhaustiveLowPrecisionEncodingDecodeEncodeAndFixedRoundTrip) {
  context c;
  for (unsigned raw = 0; raw != 256; ++raw) {
    SCOPED_TRACE(::testing::Message()
                 << "raw=0x" << std::hex << raw << " control=default");
    const auto bits = static_cast<uint8_t>(raw);
    const auto e4 = float8_e4m3_t::from_bits(bits);
    const auto e5 = float8_e5m2_t::from_bits(bits);
    const auto e2 = float6_e2m3_t::from_bits(bits);
    const auto e3 = float6_e3m2_t::from_bits(bits);
    const auto f4 = float4_e2m1_t::from_bits(bits);
    const auto ue8 = ufloat8_e8m0_t::from_bits(bits);
    const auto ue4 = ufloat7_e4m3_t::from_bits(bits);

    EXPECT_TRUE(is_valid_encoding(e4));
    EXPECT_TRUE(is_valid_encoding(e5));
    EXPECT_EQ(is_valid_encoding(e2), raw < 64);
    EXPECT_EQ(is_valid_encoding(e3), raw < 64);
    EXPECT_EQ(is_valid_encoding(f4), raw < 16);
    EXPECT_TRUE(is_valid_encoding(ue8));
    EXPECT_EQ(is_valid_encoding(ue4), raw < 128);
    EXPECT_EQ(classify(e4), e4m3_class(bits));
    EXPECT_EQ(classify(e5), ieee_class(bits, 0x7c, 0x03, 0x02));
    EXPECT_EQ(classify(e2), finite_class(bits, 0x3f, 0x18, 0x07));
    EXPECT_EQ(classify(e3), finite_class(bits, 0x3f, 0x1c, 0x03));
    EXPECT_EQ(classify(f4), finite_class(bits, 0x0f, 0x06, 0x01));
    EXPECT_EQ(classify(ue8), bits == 0 ? fp_class::zero : fp_class::normal);
    EXPECT_EQ(classify(ue4), finite_class(bits, 0x7f, 0x78, 0x07));

    expect_f32_decode(
        c, e4,
        decoded_f32_bits(bits, 0xff, 0x80, 0x78, 0x07, 3, 7, false, true));
    expect_f32_decode(
        c, e5, decoded_f32_bits(bits, 0xff, 0x80, 0x7c, 0x03, 2, 15, true));
    expect_f32_decode(c, e2,
                      decoded_f32_bits(bits, 0x3f, 0x20, 0x18, 0x07, 3, 1));
    expect_f32_decode(c, e3,
                      decoded_f32_bits(bits, 0x3f, 0x20, 0x1c, 0x03, 2, 3));
    expect_f32_decode(c, f4,
                      decoded_f32_bits(bits, 0x0f, 0x08, 0x06, 0x01, 1, 1));
    expect_f32_decode(c, ue8, decoded_f32_bits(bits, 0xff, 0, 0xff, 0, 0, 127));
    expect_f32_decode(c, ue4,
                      decoded_f32_bits(bits, 0x7f, 0, 0x78, 0x07, 3, 7));
    expect_f32_roundtrip(c, e4, bits);
    expect_f32_roundtrip(
        c, e5,
        static_cast<uint8_t>(bits | (ieee_class(bits, 0x7c, 0x03, 0x02) ==
                                             fp_class::signaling_nan
                                         ? 0x02
                                         : 0)));
    expect_f32_roundtrip(c, e2, static_cast<uint8_t>(bits & 0x3f));
    expect_f32_roundtrip(c, e3, static_cast<uint8_t>(bits & 0x3f));
    expect_f32_roundtrip(c, f4, static_cast<uint8_t>(bits & 0x0f));
    expect_f32_roundtrip(c, ue8, bits);
    expect_f32_roundtrip(c, ue4, static_cast<uint8_t>(bits & 0x7f));
  }
  for (int raw = std::numeric_limits<int8_t>::min();
       raw <= std::numeric_limits<int8_t>::max(); ++raw) {
    SCOPED_TRACE(::testing::Message()
                 << "fixed raw=" << raw << " control=default");
    const auto fixed = fixed8_s2f6_t{static_cast<int8_t>(raw)};
    const auto widened = cvt<float32_t>(c, fixed);
    ASSERT_TRUE(widened);
    const auto roundtrip = cvt<fixed8_s2f6_t>(c, widened->value);
    ASSERT_TRUE(roundtrip);
    EXPECT_EQ(roundtrip->value.rep, fixed.rep);
  }
}

TEST(Conversion, CapabilityDrivenFamilyConversionRoutes) {
  context c;
  expect_f32_conversion_routes(
      c, std::type_identity<float16_t>{}, std::type_identity<bfloat16_t>{},
      std::type_identity<float8_e4m3_t>{}, std::type_identity<float8_e5m2_t>{},
      std::type_identity<float6_e2m3_t>{}, std::type_identity<float6_e3m2_t>{},
      std::type_identity<float4_e2m1_t>{}, std::type_identity<ufloat8_e8m0_t>{},
      std::type_identity<ufloat7_e4m3_t>{}, std::type_identity<tfloat32_t>{});

  static_assert(convertible_to<fixed8_s2f6_t, float32_t>);
  static_assert(convertible_to<float32_t, fixed8_s2f6_t>);
  static_assert(convertible_to<float32_t, int32_t>);
  static_assert(convertible_to<float32_t, uint32_t>);
  static_assert(convertible_to<float32_t, int64_t>);
  static_assert(convertible_to<float32_t, uint64_t>);
  static_assert(convertible_to<float64_t, int32_t>);
  static_assert(convertible_to<float64_t, uint32_t>);
  static_assert(convertible_to<float64_t, int64_t>);
  static_assert(convertible_to<float64_t, uint64_t>);
  static_assert(convertible_to<float16_t, float64_t>);
  static_assert(convertible_to<float64_t, float16_t>);

  EXPECT_TRUE(cvt<float32_t>(c, fixed8_s2f6_t{64}));
  EXPECT_TRUE(cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x3f800000)));
  EXPECT_TRUE(cvt<float64_t>(c, float16_t::from_bits(0x3c00)));
  EXPECT_TRUE(cvt<float16_t>(c, float64_t::from_bits(0x3ff0000000000000ULL)));
  EXPECT_TRUE(cvt<float32_t>(c, int32_t{1}));
  EXPECT_TRUE(cvt<float32_t>(c, uint32_t{1}));
  EXPECT_TRUE(cvt<float32_t>(c, int64_t{1}));
  EXPECT_TRUE(cvt<float32_t>(c, uint64_t{1}));
  EXPECT_TRUE(cvt<float64_t>(c, int32_t{1}));
  EXPECT_TRUE(cvt<float64_t>(c, uint32_t{1}));
  EXPECT_TRUE(cvt<float64_t>(c, int64_t{1}));
  EXPECT_TRUE(cvt<float64_t>(c, uint64_t{1}));
}

TEST(Conversion, DirectedControlsSpecialValuesAndDeterministicSamples) {
  context c;
  constexpr auto midpoint = float32_t::from_bits(0x3f801000);
  constexpr auto negative_midpoint = float32_t::from_bits(0xbf801000);
  EXPECT_EQ(cvt<float16_t>(c, midpoint)->value.bits(), 0x3c00);
  EXPECT_EQ(
      cvt<float16_t>(c, midpoint, {.rounding = rounding_mode::toward_zero})
          ->value.bits(),
      0x3c00);
  EXPECT_EQ(
      cvt<float16_t>(c, midpoint, {.rounding = rounding_mode::toward_negative})
          ->value.bits(),
      0x3c00);
  EXPECT_EQ(
      cvt<float16_t>(c, midpoint, {.rounding = rounding_mode::toward_positive})
          ->value.bits(),
      0x3c01);
  EXPECT_EQ(cvt<float16_t>(c, negative_midpoint,
                           {.rounding = rounding_mode::toward_negative})
                ->value.bits(),
            0xbc01);
  EXPECT_TRUE(cvt<float16_t>(c, float32_t::from_bits(1))->status.underflow);
  EXPECT_TRUE(cvt<float16_t>(c, float32_t::from_bits(1))->status.inexact);
  const auto overflow = cvt<float16_t>(c, float32_t::from_bits(0x7f7fffff));
  ASSERT_TRUE(overflow);
  EXPECT_EQ(overflow->value.bits(), 0x7c00);
  EXPECT_TRUE(overflow->status.overflow);
  EXPECT_TRUE(overflow->status.inexact);
  EXPECT_EQ(cvt<float16_t>(c, float32_t::from_bits(0x80000000))->value.bits(),
            0x8000);
  EXPECT_TRUE(
      cvt<float16_t>(c, float32_t::from_bits(0x7f800001))->status.invalid);
  EXPECT_TRUE(
      is_nan(cvt<float16_t>(c, float32_t::from_bits(0x7f800001))->value));

  const auto f16_subnormal = float16_t::from_bits(0x8001);
  EXPECT_EQ(cvt<float32_t>(c, f16_subnormal,
                           {.source_subnormal = subnormal_mode::flush_input})
                ->value.bits(),
            0x80000000u);
  EXPECT_EQ(
      cvt<float16_t>(c, float32_t::from_bits(0xb3800000),
                     {.destination_subnormal = subnormal_mode::flush_output})
          ->value.bits(),
      0x8000);
  EXPECT_EQ(cvt<float16_t>(c, float32_t::from_bits(0x7f800000),
                           {.saturation = saturation_mode::finite})
                .error(),
            arithmetic_error::unsupported_saturation);
  EXPECT_EQ(cvt<float16_t>(c, midpoint, {.activation = activation_mode::relu})
                .error(),
            arithmetic_error::unsupported_operation);
  EXPECT_EQ(cvt<float16_t>(c, midpoint, {.rounding = rounding_mode::stochastic})
                .error(),
            arithmetic_error::unsupported_rounding);

  uint32_t state = 0x00c0ffeeu;
  for (unsigned sample = 0; sample != 16; ++sample) {
    state = state * 1664525u + 1013904223u;
    const auto low = state & 0x1fffu;
    const auto raw = 0x3f800000u | low;
    const auto expected = static_cast<uint16_t>(low > 0x1000 ? 0x3c01 : 0x3c00);
    SCOPED_TRACE(::testing::Message()
                 << "seed=0x00c0ffee sample=" << sample << " raw=0x" << std::hex
                 << raw << " rounding=nearest_even");
    const auto converted = cvt<float16_t>(c, float32_t::from_bits(raw));
    ASSERT_TRUE(converted);
    EXPECT_EQ(converted->value.bits(), expected);
  }
}

TEST(Conversion, Bf16ParityTiesDirectedRoundingAndSpecials) {
  context c;
  constexpr auto halfway_even = float32_t::from_bits(0x3f808000);
  constexpr auto halfway_odd = float32_t::from_bits(0x3f818000);
  const auto rn_even = cvt<bfloat16_t>(c, halfway_even);
  const auto rn_odd = cvt<bfloat16_t>(c, halfway_odd);
  ASSERT_TRUE(rn_even);
  ASSERT_TRUE(rn_odd);
  EXPECT_EQ(rn_even->value.bits(), 0x3f80);
  EXPECT_EQ(rn_odd->value.bits(), 0x3f82);
  EXPECT_TRUE(rn_even->status.inexact);
  EXPECT_EQ(
      cvt<bfloat16_t>(c, halfway_even, {.rounding = rounding_mode::toward_zero})
          ->value.bits(),
      0x3f80);
  EXPECT_EQ(cvt<bfloat16_t>(c, halfway_even,
                            {.rounding = rounding_mode::toward_negative})
                ->value.bits(),
            0x3f80);
  EXPECT_EQ(cvt<bfloat16_t>(c, halfway_even,
                            {.rounding = rounding_mode::toward_positive})
                ->value.bits(),
            0x3f81);
  EXPECT_EQ(cvt<bfloat16_t>(c, float32_t::from_bits(0xbf808000),
                            {.rounding = rounding_mode::toward_negative})
                ->value.bits(),
            0xbf81);
  EXPECT_EQ(cvt<bfloat16_t>(c, float32_t::from_bits(0xbf808000),
                            {.rounding = rounding_mode::toward_positive})
                ->value.bits(),
            0xbf80);

  const auto nan = cvt<bfloat16_t>(c, float32_t::from_bits(0x7f800001));
  const auto tiny = cvt<bfloat16_t>(c, float32_t::from_bits(0x00000001));
  ASSERT_TRUE(nan);
  ASSERT_TRUE(tiny);
  EXPECT_EQ(nan->value.bits(), 0x7fc0);
  EXPECT_TRUE(nan->status.invalid);
  EXPECT_EQ(tiny->value.bits(), 0);
  EXPECT_TRUE(tiny->status.underflow);
  EXPECT_TRUE(tiny->status.inexact);
}

TEST(Conversion, LowPrecisionEncodeBoundaryAndTieVectors) {
  context c;
  constexpr auto rp =
      conversion_control{.rounding = rounding_mode::toward_positive};
  expect_encoded<float8_e4m3_t>(c, 0x3f800000, 0x38);
  expect_encoded<float8_e4m3_t>(c, 0x3f880000, 0x38);
  expect_encoded<float8_e4m3_t>(c, 0x3f880000, 0x39, rp);
  expect_encoded<float8_e5m2_t>(c, 0x3f800000, 0x3c);
  expect_encoded<float8_e5m2_t>(c, 0x3f900000, 0x3c);
  expect_encoded<float8_e5m2_t>(c, 0x3f900000, 0x3d, rp);
  expect_encoded<float6_e2m3_t>(c, 0x3f800000, 0x08);
  expect_encoded<float6_e2m3_t>(c, 0x3f880000, 0x08);
  expect_encoded<float6_e2m3_t>(c, 0x3f880000, 0x09, rp);
  expect_encoded<float6_e3m2_t>(c, 0x3f800000, 0x0c);
  expect_encoded<float6_e3m2_t>(c, 0x3f900000, 0x0c);
  expect_encoded<float6_e3m2_t>(c, 0x3f900000, 0x0d, rp);
  expect_encoded<float4_e2m1_t>(c, 0x3f800000, 0x02);
  expect_encoded<float4_e2m1_t>(c, 0x3fa00000, 0x02);
  expect_encoded<float4_e2m1_t>(c, 0x3fa00000, 0x03, rp);
  expect_encoded<ufloat8_e8m0_t>(c, 0x3f800000, 0x7f);
  expect_encoded<ufloat8_e8m0_t>(c, 0x3fc00000, 0x80);
  expect_encoded<ufloat8_e8m0_t>(c, 0x3fc00000, 0x80, rp);
  expect_encoded<ufloat7_e4m3_t>(c, 0x3f800000, 0x38);
  expect_encoded<ufloat7_e4m3_t>(c, 0x3f880000, 0x38);
  expect_encoded<ufloat7_e4m3_t>(c, 0x3f880000, 0x39, rp);
}

TEST(Conversion, FixedEncodingTraitsClassifyEveryLowBitPattern) {
  for (unsigned bits = 0; bits != 256; ++bits) {
    const auto e4 = float8_e4m3_t::from_bits(static_cast<uint8_t>(bits));
    const auto e5 = float8_e5m2_t::from_bits(static_cast<uint8_t>(bits));
    EXPECT_TRUE(is_valid_encoding(e4));
    EXPECT_TRUE(is_valid_encoding(e5));
    EXPECT_NE(classify(e4), fp_class::signaling_nan);
  }
  for (unsigned bits = 0; bits != 64; ++bits) {
    EXPECT_TRUE(is_valid_encoding(float6_e2m3_t::from_bits(bits)));
    EXPECT_TRUE(is_valid_encoding(float6_e3m2_t::from_bits(bits)));
  }
  for (unsigned bits = 0; bits != 16; ++bits)
    EXPECT_TRUE(is_valid_encoding(float4_e2m1_t::from_bits(bits)));
}

TEST(Conversion, FixedS2f6ConversionsRoundAndSaturate) {
  context c;
  auto fixed = cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x3fa00000));  // 1.25
  ASSERT_TRUE(fixed);
  EXPECT_EQ(fixed->value.rep, 80);
  auto widened = cvt<float32_t>(c, fixed->value);
  ASSERT_TRUE(widened);
  EXPECT_EQ(widened->value.bits(), 0x3fa00000);
  auto rounded = cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x3f810000),
                                    {.rounding = rounding_mode::toward_zero});
  ASSERT_TRUE(rounded);
  EXPECT_EQ(rounded->value.rep, 64);
  auto saturated =
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x40400000),
                         {.saturation = saturation_mode::type_range});
  ASSERT_TRUE(saturated);
  EXPECT_EQ(saturated->value.rep, 127);
  EXPECT_TRUE(saturated->status.overflow);
  EXPECT_EQ(
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x3f800000),
                         {.source_subnormal = subnormal_mode::flush_output})
          .error(),
      arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(cvt<fixed8_s2f6_t>(
                c, float32_t::from_bits(0x3f800000),
                {.destination_subnormal = subnormal_mode::flush_output})
                .error(),
            arithmetic_error::unsupported_subnormal_mode);
  EXPECT_TRUE(
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(1),
                         {.source_subnormal = subnormal_mode::flush_input}));
}

TEST(Conversion, IdentityControlsAreNotIgnored) {
  context c;
  const auto one = float32_t::from_bits(0x3f800000);
  EXPECT_EQ(
      cvt<float32_t>(c, one, {.saturation = saturation_mode::finite}).error(),
      arithmetic_error::unsupported_saturation);
  EXPECT_EQ(
      cvt<float32_t>(c, one, {.source_subnormal = subnormal_mode::flush_input})
          .error(),
      arithmetic_error::unsupported_subnormal_mode);
  EXPECT_EQ(
      cvt<int32_t>(c, int32_t{1}, {.rounding = rounding_mode::toward_zero})
          .error(),
      arithmetic_error::unsupported_rounding);
  EXPECT_EQ(
      cvt<int32_t>(c, int32_t{1},
                   {.destination_subnormal = subnormal_mode::flush_output})
          .error(),
      arithmetic_error::unsupported_subnormal_mode);
}

TEST(Conversion, PipelineIntegerLowPrecisionAndTf32Profile) {
  static_assert(!std::constructible_from<tfloat32_t, float32_t>);
  context c;
  const auto f = float32_t::from_bits(0x3f99999a);  // 1.2
  auto e2 = cvt<float6_e2m3_t>(c, f);
  auto e3 = cvt<float6_e3m2_t>(c, f);
  auto u8 = cvt<ufloat8_e8m0_t>(c, f);
  auto u4 = cvt<ufloat7_e4m3_t>(c, f);
  ASSERT_TRUE(e2);
  ASSERT_TRUE(e3);
  ASSERT_TRUE(u8);
  ASSERT_TRUE(u4);
  EXPECT_TRUE(cvt<float32_t>(c, e2->value));
  EXPECT_TRUE(cvt<float32_t>(c, e3->value));
  EXPECT_TRUE(cvt<float32_t>(c, u8->value));
  EXPECT_TRUE(cvt<float32_t>(c, u4->value));
  auto i64 = cvt<float32_t>(c, int64_t{1} << 40);
  ASSERT_TRUE(i64);
  auto back = cvt<int64_t>(c, i64->value);
  ASSERT_TRUE(back);
  auto tf = cvt<tfloat32_t>(c, float32_t::from_bits(0x3f801001));
  ASSERT_TRUE(tf);
  EXPECT_EQ(tf->value.canonical_value().bits() & 0x1fffu, 0u);
  auto encoded = encode(tf->value, c.profile().tf32);
  ASSERT_TRUE(encoded);
  auto decoded = decode_tf32(*encoded, c.profile().tf32);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(*decoded, tf->value);
  model_profile unsupported{};
  unsupported.tf32.model = tf32_encoding_model::unsupported;
  EXPECT_EQ(cvt<tfloat32_t>(context{unsupported}, f).error(),
            arithmetic_error::unsupported_operation);
  EXPECT_EQ(encode(tf->value, unsupported.tf32).error(),
            arithmetic_error::unsupported_operation);
  auto direct = detail::dispatch::quantize_tf32(
      float32_t::from_bits(0x3f800001), {}, c.profile().tf32);
  ASSERT_TRUE(direct);
  EXPECT_EQ(direct->value.canonical_value().bits(), 0x3f800000u);
  EXPECT_TRUE(direct->status.inexact);
  EXPECT_EQ(detail::dispatch::quantize_tf32(float32_t::from_bits(0x3f800001),
                                            {}, unsupported.tf32)
                .error(),
            arithmetic_error::unsupported_operation);
  EXPECT_EQ(
      cvt<float16_t>(c, f, {.rounding = rounding_mode::nearest_away}).error(),
      arithmetic_error::unsupported_rounding);
  EXPECT_TRUE(
      cvt<float16_t>(c, f, {.source_subnormal = subnormal_mode::flush_input}));
}

TEST(Conversion, DirectF64ConversionAndControlRegressions) {
  context c;
  // Just above the F16 midpoint: F64->F32->F16 would tie to 1.0, whereas
  // the direct SoftFloat conversion must round upward.
  const auto midpoint_plus = float64_t::from_bits(0x3ff0020000000001ULL);
  auto h = cvt<float16_t>(c, midpoint_plus);
  ASSERT_TRUE(h);
  EXPECT_EQ(h->value.bits(), 0x3c01);

  auto i64 = cvt<float64_t>(c, std::numeric_limits<int64_t>::max());
  ASSERT_TRUE(i64);
  EXPECT_EQ(i64->value.bits(), 0x43e0000000000000ULL);
  auto u64 = cvt<uint64_t>(c, float64_t::from_bits(0x43e0000000000000ULL));
  ASSERT_TRUE(u64);
  EXPECT_EQ(u64->value, uint64_t{1} << 63);

  const auto i32_min = cvt<float32_t>(c, std::numeric_limits<int32_t>::min());
  const auto i32_max = cvt<float32_t>(c, std::numeric_limits<int32_t>::max());
  const auto u32_max = cvt<float32_t>(c, std::numeric_limits<uint32_t>::max());
  ASSERT_TRUE(i32_min);
  ASSERT_TRUE(i32_max);
  ASSERT_TRUE(u32_max);
  EXPECT_EQ(i32_min->value.bits(), 0xcf000000u);
  EXPECT_EQ(i32_max->value.bits(), 0x4f000000u);
  EXPECT_EQ(u32_max->value.bits(), 0x4f800000u);
  EXPECT_TRUE(i32_max->status.inexact);
  EXPECT_TRUE(u32_max->status.inexact);

  const auto i64_min = cvt<float64_t>(c, std::numeric_limits<int64_t>::min());
  const auto u64_max = cvt<float64_t>(c, std::numeric_limits<uint64_t>::max());
  ASSERT_TRUE(i64_min);
  ASSERT_TRUE(u64_max);
  EXPECT_EQ(i64_min->value.bits(), 0xc3e0000000000000ULL);
  EXPECT_EQ(u64_max->value.bits(), 0x43f0000000000000ULL);
  EXPECT_TRUE(u64_max->status.inexact);

  const auto f32_i32 = cvt<int32_t>(c, float32_t::from_bits(0x4effffff));
  const auto f64_i64 =
      cvt<int64_t>(c, float64_t::from_bits(0x43dfffffffffffffULL));
  ASSERT_TRUE(f32_i32);
  ASSERT_TRUE(f64_i64);
  EXPECT_EQ(f32_i32->value, 2147483520);
  EXPECT_EQ(f64_i64->value, std::numeric_limits<int64_t>::max() - 1023);

  EXPECT_EQ(
      cvt<float16_t>(c, midpoint_plus, {.saturation = saturation_mode::finite})
          .error(),
      arithmetic_error::unsupported_saturation);
  EXPECT_EQ(
      cvt<float16_t>(c, midpoint_plus, {.activation = activation_mode::relu})
          .error(),
      arithmetic_error::unsupported_operation);
}

TEST(Conversion, PackedLayoutsCanonicalizePaddingAndPreserveLaneOrder) {
  constexpr auto fp6_mask = uint16_t{0x3f3f};
  for (unsigned bits = 0; bits != 65536; ++bits) {
    auto packed = float6_e2m3x2_t::from_bits(static_cast<uint16_t>(bits));
    EXPECT_EQ(packed.bits(), static_cast<uint16_t>(bits) & fp6_mask);
    const auto lanes = unpack(packed);
    EXPECT_EQ(lanes[0].bits(), bits & 0x3f);
    EXPECT_EQ(lanes[1].bits(), (bits >> 8) & 0x3f);
    EXPECT_EQ(pack<float6_e2m3x2_t>(lanes).bits(), packed.bits());
    const auto e3 = float6_e3m2x2_t::from_bits(static_cast<uint16_t>(bits));
    EXPECT_EQ(pack<float6_e3m2x2_t>(unpack(e3)).bits(), e3.bits());
  }
  for (unsigned bits = 0; bits != 65536; ++bits) {
    const auto packed = float8_e4m3x2_t::from_bits(static_cast<uint16_t>(bits));
    const auto lanes = unpack(packed);
    EXPECT_EQ(lanes[0].bits(), bits & 0xff);
    EXPECT_EQ(lanes[1].bits(), bits >> 8);
    EXPECT_EQ(pack<float8_e4m3x2_t>(lanes).bits(), bits);
  }
  for (unsigned bits = 0; bits != 256; ++bits) {
    const auto packed = float4_e2m1x2_t::from_bits(static_cast<uint8_t>(bits));
    const auto lanes = unpack(packed);
    EXPECT_EQ(lanes[0].bits(), bits & 0xf);
    EXPECT_EQ(lanes[1].bits(), bits >> 4);
    EXPECT_EQ(pack<float4_e2m1x2_t>(lanes).bits(), bits);
  }
  const auto f16 = pack<float16x2_t>(
      {float16_t::from_bits(0x3c00), float16_t::from_bits(0x4000)});
  EXPECT_EQ(f16.bits(), 0x40003c00u);
  const auto f8x4 = float8_e5m2x4_t::from_bits(0xff00807fu);
  EXPECT_EQ(unpack(f8x4)[0].bits(), 0x7f);
  EXPECT_EQ(unpack(f8x4)[1].bits(), 0x80);
  EXPECT_EQ(unpack(f8x4)[2].bits(), 0x00);
  EXPECT_EQ(unpack(f8x4)[3].bits(), 0xff);
}

}  // namespace ptxsim::arith::test
