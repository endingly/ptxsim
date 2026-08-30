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

// PTX 9.3 table literals, intentionally independent of FormatTraits and the
// production low-precision encoder/decoder.
constexpr fp_class ue8m0_class(uint8_t bits) {
  return bits == 0xff ? fp_class::quiet_nan : fp_class::normal;
}

constexpr fp_class ue4m3_class(uint8_t bits) {
  bits &= 0x7f;
  if (bits == 0x7f)
    return fp_class::quiet_nan;
  return finite_class(bits, 0x7f, 0x78, 0x07);
}

constexpr uint32_t ue8m0_decoded_f32_bits(uint8_t bits) {
  if (bits == 0xff)
    return 0x7fc00000u;
  // UE8M0 has the F32 exponent bias but no zero encoding.  Its 0x00 is 2^-127,
  // represented exactly by the F32 subnormal 2^-127.
  return bits == 0 ? 0x00400000u : static_cast<uint32_t>(bits) << 23;
}

constexpr uint32_t ue4m3_decoded_f32_bits(uint8_t bits);

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

constexpr uint32_t ue4m3_decoded_f32_bits(uint8_t bits) {
  bits &= 0x7f;
  if (bits == 0x7f)
    return 0x7fc00000u;
  return decoded_f32_bits(bits, 0x7f, 0, 0x78, 0x07, 3, 7);
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
    EXPECT_EQ(classify(ue8), ue8m0_class(bits));
    EXPECT_EQ(classify(ue4), ue4m3_class(bits));

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
    expect_f32_decode(c, ue8, ue8m0_decoded_f32_bits(bits));
    expect_f32_decode(c, ue4, ue4m3_decoded_f32_bits(bits));
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

TEST(Conversion, Ptx93UnsignedScaleRawBitGoldens) {
  context c;
  const auto ue8_min = ufloat8_e8m0_t::from_bits(0x00);
  const auto ue8_nan = ufloat8_e8m0_t::from_bits(0xff);
  const auto ue4_max = ufloat7_e4m3_t::from_bits(0x7e);
  const auto ue4_nan = ufloat7_e4m3_t::from_bits(0x7f);

  EXPECT_TRUE(is_normal(ue8_min));
  EXPECT_FALSE(is_zero(ue8_min));
  EXPECT_TRUE(is_nan(ue8_nan));
  EXPECT_FALSE(is_infinity(ue8_nan));
  EXPECT_TRUE(is_normal(ue4_max));
  EXPECT_TRUE(is_nan(ue4_nan));
  EXPECT_FALSE(is_infinity(ue4_nan));

  EXPECT_EQ(cvt<float32_t>(c, ue8_min)->value.bits(), 0x00400000u);
  EXPECT_EQ(cvt<float32_t>(c, ue8_nan)->value.bits(), 0x7fc00000u);
  EXPECT_EQ(cvt<float32_t>(c, ue4_nan)->value.bits(), 0x7fc00000u);
  EXPECT_EQ(cvt<ufloat8_e8m0_t>(c, float32_t::from_bits(0x00400000))
                ->value.bits(),
            0x00);
  EXPECT_EQ(cvt<ufloat8_e8m0_t>(c, float32_t::from_bits(0x7fc00000))
                ->value.bits(),
            0xff);
  EXPECT_EQ(cvt<ufloat7_e4m3_t>(c, float32_t::from_bits(0x7fc00000))
                ->value.bits(),
            0x7f);
}

TEST(Conversion, UE8M0MinimumFiniteEndpoint) {
  context c;
  const auto expect_endpoint = [&](std::uint32_t bits, conversion_control control) {
    const auto encoded = cvt<ufloat8_e8m0_t>(c, float32_t::from_bits(bits), control);
    ASSERT_TRUE(encoded);
    EXPECT_EQ(encoded->value.bits(), 0x00);
    EXPECT_FALSE(encoded->status.invalid);
    EXPECT_FALSE(encoded->status.divide_by_zero);
    EXPECT_FALSE(encoded->status.overflow);
    EXPECT_TRUE(encoded->status.underflow);
    EXPECT_TRUE(encoded->status.inexact);
  };

  const auto exact_min = cvt<ufloat8_e8m0_t>(c, float32_t::from_bits(0x00400000));
  ASSERT_TRUE(exact_min);
  EXPECT_EQ(exact_min->value.bits(), 0x00);
  EXPECT_FALSE(exact_min->status.invalid);
  EXPECT_FALSE(exact_min->status.underflow);
  EXPECT_FALSE(exact_min->status.inexact);

  expect_endpoint(0x00300000, {});  // 0.75 * 2^-127
  expect_endpoint(0x00200000, {});  // 0.5 * 2^-127
  expect_endpoint(0x003fffff, {});  // just below 2^-127
  for (const auto mode : {rounding_mode::toward_zero,
                          rounding_mode::toward_positive,
                          rounding_mode::toward_negative,
                          rounding_mode::nearest_away})
    expect_endpoint(0x00300000, {.rounding = mode});

  expect_endpoint(0x00000000, {});
  const auto negative =
      cvt<ufloat8_e8m0_t>(c, float32_t::from_bits(0x80400000));
  ASSERT_TRUE(negative);
  EXPECT_EQ(negative->value.bits(), 0x00);
  EXPECT_TRUE(negative->status.invalid);
  EXPECT_FALSE(negative->status.underflow);
  EXPECT_TRUE(negative->status.inexact);
}

TEST(Conversion, S2F6FiniteSaturationSpecialValuesReportDiagnostics) {
  context c;
  const conversion_control finite{.saturation = saturation_mode::finite};
  const auto positive =
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x7f800000), finite);
  const auto negative =
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0xff800000), finite);
  const auto qnan =
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x7fc00000), finite);
  const auto snan =
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x7f800001), finite);
  ASSERT_TRUE(positive && negative && qnan && snan);
  EXPECT_EQ(positive->value.rep, 127);
  EXPECT_EQ(negative->value.rep, -128);
  EXPECT_EQ(qnan->value.rep, 127);
  EXPECT_EQ(snan->value.rep, 127);
  EXPECT_FALSE(positive->status.invalid);
  EXPECT_FALSE(negative->status.invalid);
  EXPECT_TRUE(qnan->status.invalid);
  EXPECT_TRUE(snan->status.invalid);
  EXPECT_TRUE(positive->status.overflow);
  EXPECT_TRUE(negative->status.overflow);
  EXPECT_FALSE(qnan->status.overflow);
  EXPECT_FALSE(snan->status.overflow);
  EXPECT_TRUE(positive->status.inexact);
  EXPECT_TRUE(negative->status.inexact);
  EXPECT_FALSE(qnan->status.inexact);
  EXPECT_FALSE(snan->status.inexact);
  EXPECT_FALSE(positive->status.divide_by_zero || positive->status.underflow ||
               positive->status.model_dependent);
  EXPECT_FALSE(negative->status.divide_by_zero || negative->status.underflow ||
               negative->status.model_dependent);
  EXPECT_FALSE(qnan->status.divide_by_zero || qnan->status.underflow ||
               qnan->status.model_dependent);
  EXPECT_FALSE(snan->status.divide_by_zero || snan->status.underflow ||
               snan->status.model_dependent);
}

TEST(Conversion, S2F6FiniteSaturationClampsRoundedFiniteValues) {
  context c;
  const conversion_control finite{.saturation = saturation_mode::finite};
  struct finite_case {
    std::uint32_t input;
    std::int8_t expected;
  };
  for (const auto [input, expected] :
       std::array<finite_case, 6>{finite_case{0x40400000u, 127},  // +3
                                  {0xc0400000u, -128},            // -3
                                  {0x3ffe0000u, 127},             // exact +127/64
                                  {0xc0000000u, -128},            // exact -128/64
                                  {0x40000000u, 127},             // just above +127/64
                                  {0xc0010000u, -128}}) {         // just below -128/64
    const auto result = cvt<fixed8_s2f6_t>(c, float32_t::from_bits(input), finite);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->value.rep, expected);
    const bool exact_endpoint = input == 0x3ffe0000u || input == 0xc0000000u;
    EXPECT_EQ(result->status.overflow, !exact_endpoint);
    EXPECT_EQ(result->status.inexact, !exact_endpoint);
    EXPECT_FALSE(result->status.invalid);
    EXPECT_FALSE(result->status.divide_by_zero);
    EXPECT_FALSE(result->status.underflow);
    EXPECT_FALSE(result->status.model_dependent);
  }

  const conversion_control relu_finite{
      .saturation = saturation_mode::finite, .activation = activation_mode::relu};
  for (const auto input : {0xc0400000u, 0xff800000u}) {
    const auto result =
        cvt<fixed8_s2f6_t>(c, float32_t::from_bits(input), relu_finite);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->value.rep, 0);
    EXPECT_FALSE(result->status.invalid);
    EXPECT_FALSE(result->status.divide_by_zero);
    EXPECT_FALSE(result->status.overflow);
    EXPECT_FALSE(result->status.underflow);
    EXPECT_FALSE(result->status.inexact);
    EXPECT_FALSE(result->status.model_dependent);
  }
  const auto relu_nan =
      cvt<fixed8_s2f6_t>(c, float32_t::from_bits(0x7fc00000), relu_finite);
  ASSERT_TRUE(relu_nan);
  EXPECT_EQ(relu_nan->value.rep, 127);
  EXPECT_TRUE(relu_nan->status.invalid);
  EXPECT_FALSE(relu_nan->status.overflow);
  EXPECT_FALSE(relu_nan->status.inexact);
}

TEST(Conversion, S2F6FiniteSaturationReportsExactSourceRangeBeforeRounding) {
  context c;
  const conversion_control finite{.saturation = saturation_mode::finite};
  const auto expect = [&](auto input, std::int8_t expected, bool overflow) {
    const auto result = cvt<fixed8_s2f6_t>(c, input, finite);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->value.rep, expected);
    EXPECT_EQ(result->status.overflow, overflow);
    EXPECT_EQ(result->status.inexact, overflow);
    EXPECT_FALSE(result->status.invalid || result->status.divide_by_zero ||
                 result->status.underflow || result->status.model_dependent);
  };

  expect(float32_t::from_bits(0x3ffe0000), 127, false);  // +127/64
  expect(float32_t::from_bits(0xc0000000), -128, false);  // -128/64
  expect(float32_t::from_bits(0x3ffe0001), 127, true);    // nextafter(+127/64)
  expect(float32_t::from_bits(0xc0000001), -128, true);   // nextafter(-128/64)
  expect(float64_t::from_bits(0x3fffc00000000000ULL), 127, false);
  expect(float64_t::from_bits(0xc000000000000000ULL), -128, false);
  expect(float64_t::from_bits(0x3fffc00000000001ULL), 127, true);
  expect(float64_t::from_bits(0xc000000000000001ULL), -128, true);

  for (const auto rounding : {rounding_mode::nearest_even,
                              rounding_mode::toward_zero,
                              rounding_mode::toward_positive,
                              rounding_mode::toward_negative}) {
    const conversion_control directed{.rounding = rounding,
                                      .saturation = saturation_mode::finite};
    for (const auto [input, expected] :
         {std::pair{0x3ffe0001u, std::int8_t{127}},
          std::pair{0xc0000001u, std::int8_t{-128}}}) {
      const auto result =
          cvt<fixed8_s2f6_t>(c, float32_t::from_bits(input), directed);
      ASSERT_TRUE(result);
      EXPECT_EQ(result->value.rep, expected);
      EXPECT_TRUE(result->status.overflow);
      EXPECT_TRUE(result->status.inexact);
      EXPECT_FALSE(result->status.invalid || result->status.divide_by_zero ||
                   result->status.underflow || result->status.model_dependent);
    }
  }
}

TEST(Conversion, CapabilityDrivenFamilyConversionRoutes) {
  context c;
  // The capability is the canonical decode/encode matrix, not a list of
  // opportunistic F32 hub routes.  Every positive pair below has a generic
  // path; TF32 remains deliberately limited to its PTX semantic bridge.
  static_assert(convertible_to<float64_t, float8_e4m3_t>);
  static_assert(convertible_to<float8_e4m3_t, bfloat16_t>);
  static_assert(convertible_to<fixed8_s2f6_t, int64_t>);
  static_assert(convertible_to<int64_t, fixed8_s2f6_t>);
  static_assert(convertible_to<float8_e5m2_t, float64_t>);
  static_assert(operation_capability<scalar_operation::cvt, float16_t,
                                     float32_t>::value);
  static_assert(operation_capability<scalar_operation::cvt, float64_t,
                                     bfloat16_t>::value);
  static_assert(!convertible_to<tfloat32_t, float64_t>);
  static_assert(!convertible_to<float16_t, tfloat32_t>);
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

TEST(Conversion, CanonicalMatrixDoesNotRoundThroughF32) {
  context c;
  // 2^53+1 is distinguishable in F64 but not F32.  The fixed target must see
  // the original integer bits and saturate, rather than first losing them in
  // a float hub.
  const auto precise = float64_t::from_bits(0x4340000000000001ULL);
  const auto fixed = cvt<fixed8_s2f6_t>(c, precise,
                                        {.saturation = saturation_mode::type_range});
  ASSERT_TRUE(fixed);
  EXPECT_EQ(fixed->value.rep, 127);
  EXPECT_TRUE(fixed->status.overflow);

  const auto low = cvt<float8_e4m3_t>(c, precise);
  ASSERT_TRUE(low);
  EXPECT_EQ(low->value.bits(), 0x7e);  // finite E4M3 maximum, not NaN 0x7f
  EXPECT_TRUE(low->status.overflow);

  // Just above an E4M3 midpoint.  F64->F32 first would collapse this to the
  // exact midpoint (and then choose even 0x38); canonical F64->E4M3 rounds
  // once to 0x39.
  const auto above_midpoint = float64_t::from_bits(0x3ff1000000400000ULL);
  const auto direct_low = cvt<float8_e4m3_t>(c, above_midpoint);
  ASSERT_TRUE(direct_low);
  EXPECT_EQ(direct_low->value.bits(), 0x39);
  const auto low_to_f64 = cvt<float64_t>(c, float8_e4m3_t::from_bits(0x39));
  ASSERT_TRUE(low_to_f64);
  EXPECT_EQ(low_to_f64->value.bits(), 0x3ff2000000000000ULL);

  const auto exact_fixed = cvt<fixed8_s2f6_t>(c, int64_t{1});
  ASSERT_TRUE(exact_fixed);
  EXPECT_EQ(exact_fixed->value.rep, 64);
  const auto exact_integer = cvt<int64_t>(c, fixed8_s2f6_t{96});
  ASSERT_TRUE(exact_integer);
  EXPECT_EQ(exact_integer->value, 2);
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
  const auto finite = cvt<float16_t>(
      c, float32_t::from_bits(0x7f800000),
      {.saturation = saturation_mode::finite});
  ASSERT_TRUE(finite);
  EXPECT_EQ(finite->value.bits(), 0x7bff);
  const auto relu = cvt<float16_t>(c, midpoint,
                                   {.activation = activation_mode::relu});
  ASSERT_TRUE(relu);
  EXPECT_EQ(relu->value.bits(), 0x3c00);
  EXPECT_EQ(cvt<float16_t>(c, midpoint, {.rounding = rounding_mode::stochastic})
                .error(),
            arithmetic_error::invalid_stochastic_input);
  const auto stochastic = cvt<float16_t>(
      c, midpoint, {.rounding = rounding_mode::stochastic},
      stochastic_rounding_input{bits32_t{0x12345678}});
  ASSERT_TRUE(stochastic);
  EXPECT_EQ(stochastic->value.bits(), 0x3c01);
  EXPECT_EQ(cvt<float16_t>(c, midpoint, {},
                           stochastic_rounding_input{bits32_t{0x12345678}})
                .error(),
            arithmetic_error::invalid_stochastic_input);

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

TEST(Conversion, StochasticRoundingUsesSupportedF32Forms) {
  context c;
  constexpr conversion_control stochastic{.rounding = rounding_mode::stochastic};
  constexpr stochastic_rounding_input zero{bits32_t{0}};
  constexpr stochastic_rounding_input maximum{bits32_t{0xffffffffu}};
  constexpr auto near_midpoint = float32_t::from_bits(0x3f800fff);
  constexpr auto far_midpoint = float32_t::from_bits(0x3f800001);

  static_assert(conversion_control_capability<
                float16_t, float32_t,
                conversion_control_feature::stochastic>::value);
  static_assert(!conversion_control_capability<
                float16_t, float64_t,
                conversion_control_feature::stochastic>::value);
  static_assert(!conversion_control_capability<
                ufloat8_e8m0_t, float32_t,
                conversion_control_feature::stochastic>::value);

  EXPECT_EQ(cvt<float16_t>(c, near_midpoint, stochastic, zero)->value.bits(),
            0x3c01);
  EXPECT_EQ(cvt<float16_t>(c, near_midpoint, stochastic, maximum)->value.bits(),
            0x3c00);
  EXPECT_EQ(cvt<float16_t>(c, far_midpoint, stochastic, zero)->value.bits(),
            0x3c01);
  EXPECT_EQ(cvt<float16_t>(c, far_midpoint, stochastic, maximum)->value.bits(),
            0x3c00);

  const auto replay_a = cvt<float16_t>(
      c, near_midpoint, stochastic, stochastic_rounding_input{bits32_t{0x12345678}});
  const auto replay_b = cvt<float16_t>(
      c, near_midpoint, stochastic, stochastic_rounding_input{bits32_t{0x12345678}});
  ASSERT_TRUE(replay_a && replay_b);
  EXPECT_EQ(replay_a->value.bits(), replay_b->value.bits());
  EXPECT_EQ(replay_a->status.inexact, replay_b->status.inexact);

  constexpr auto tiny_f64 = float64_t::from_bits(1);
  EXPECT_EQ(cvt<float16_t>(c, tiny_f64, stochastic, zero).error(),
            arithmetic_error::unsupported_rounding);
  EXPECT_EQ(cvt<float16_t>(c, tiny_f64, stochastic, maximum).error(),
            arithmetic_error::unsupported_rounding);
  EXPECT_EQ(cvt<ufloat8_e8m0_t>(c, near_midpoint, stochastic, zero).error(),
            arithmetic_error::unsupported_rounding);
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
  EXPECT_EQ(cvt<float32_t>(c, one, {.saturation = saturation_mode::finite})
                .error(),
            arithmetic_error::unsupported_saturation);
  const auto flushed = cvt<float32_t>(
      c, float32_t::from_bits(1),
      {.source_subnormal = subnormal_mode::flush_input});
  ASSERT_TRUE(flushed);
  EXPECT_EQ(flushed->value.bits(), 0u);
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

TEST(Conversion, SatfiniteAndReluFollowDestinationFormatRules) {
  context c;
  const auto positive_infinity = float32_t::from_bits(0x7f800000u);
  const auto negative_infinity = float32_t::from_bits(0xff800000u);
  const auto negative_one = float32_t::from_bits(0xbf800000u);

  const auto positive = cvt<float16_t>(
      c, positive_infinity, {.saturation = saturation_mode::finite});
  const auto negative = cvt<float16_t>(
      c, negative_infinity, {.saturation = saturation_mode::finite});
  ASSERT_TRUE(positive && negative);
  EXPECT_EQ(positive->value.bits(), 0x7bffu);
  EXPECT_EQ(negative->value.bits(), 0xfbffu);
  EXPECT_TRUE(positive->status.overflow);
  EXPECT_TRUE(positive->status.inexact);

  const auto low_relu = cvt<float4_e2m1_t>(
      c, negative_one,
      {.saturation = saturation_mode::finite,
       .activation = activation_mode::relu});
  const auto fixed_relu = cvt<fixed8_s2f6_t>(
      c, negative_one,
      {.saturation = saturation_mode::finite,
       .activation = activation_mode::relu});
  ASSERT_TRUE(low_relu && fixed_relu);
  EXPECT_EQ(low_relu->value.bits(), 0u);
  EXPECT_EQ(fixed_relu->value.rep, 0);

  EXPECT_EQ(cvt<float64_t>(c, float32_t::from_bits(0x3f800000u),
                           {.saturation = saturation_mode::finite})
                .error(),
            arithmetic_error::unsupported_saturation);
  EXPECT_EQ(cvt<bfloat16_t>(c, negative_one,
                            {.saturation = saturation_mode::zero_to_one})
                .error(),
            arithmetic_error::unsupported_saturation);
  EXPECT_EQ(cvt<ufloat7_e4m3_t>(
                c, negative_one,
                {.saturation = saturation_mode::finite})
                .error(),
            arithmetic_error::unsupported_saturation);
  EXPECT_EQ(cvt<ufloat8_e8m0_t>(
                c, negative_one, {.activation = activation_mode::relu})
                .error(),
            arithmetic_error::unsupported_activation);
  EXPECT_EQ(cvt<float32_t>(c, negative_one,
                           {.activation = activation_mode::relu})
                .error(),
            arithmetic_error::unsupported_activation);

  const auto tf32_tie = cvt<tfloat32_t>(
      c, float32_t::from_bits(0x3f801000u),
      {.rounding = rounding_mode::nearest_away});
  const auto tf32_finite = cvt<tfloat32_t>(
      c, positive_infinity, {.saturation = saturation_mode::finite});
  const auto tf32_relu = cvt<tfloat32_t>(
      c, negative_one, {.activation = activation_mode::relu});
  ASSERT_TRUE(tf32_tie && tf32_finite && tf32_relu);
  EXPECT_EQ(tf32_tie->value.canonical_value().bits(), 0x3f802000u);
  EXPECT_EQ(tf32_finite->value.canonical_value().bits(), 0x7f7fe000u);
  EXPECT_EQ(tf32_relu->value.canonical_value().bits(), 0u);
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
  auto profile_quantized =
      cvt<tfloat32_t>(c, float32_t::from_bits(0x3f800001));
  ASSERT_TRUE(profile_quantized);
  EXPECT_EQ(profile_quantized->value.canonical_value().bits(), 0x3f800000u);
  EXPECT_TRUE(profile_quantized->status.inexact);
  EXPECT_EQ(cvt<tfloat32_t>(context{unsupported}, float32_t::from_bits(0x3f800001))
                .error(),
            arithmetic_error::unsupported_operation);
  EXPECT_TRUE(
      cvt<float16_t>(c, f, {.rounding = rounding_mode::nearest_away}));
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

  EXPECT_TRUE(cvt<float16_t>(c, midpoint_plus,
                            {.saturation = saturation_mode::finite}));
  EXPECT_TRUE(cvt<float16_t>(c, midpoint_plus,
                            {.activation = activation_mode::relu}));
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
