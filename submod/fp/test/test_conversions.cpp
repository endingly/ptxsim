#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace ptxsim::fp::test {
namespace {

FpClass expected_bf16_class(std::uint16_t bits) {
  const auto exponent = bits & 0x7F80u;
  const auto fraction = bits & 0x007Fu;
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  if (exponent != 0x7F80u)
    return FpClass::Normal;
  if (fraction == 0)
    return FpClass::Infinity;
  return (fraction & 0x0040u) ? FpClass::QuietNaN : FpClass::SignalingNaN;
}

}  // namespace

TEST(Conversions, ExhaustiveBf16RoundTripAndClassification) {
  const Environment environment;
  for (std::uint32_t bits = 0; bits != 65536; ++bits) {
    const Bf16 input{static_cast<std::uint16_t>(bits)};
    EXPECT_EQ(classify(input), expected_bf16_class(input.bits)) << bits;
    const auto widened = environment.convert<Fp32>(input);
    const auto roundtrip = environment.convert<Bf16>(widened.value);
    const auto expected = is_signaling_nan(input)
                              ? static_cast<std::uint16_t>(input.bits | 0x0040u)
                              : input.bits;
    EXPECT_EQ(roundtrip.value.bits, expected) << bits;
    EXPECT_EQ(classify(roundtrip.value),
              is_signaling_nan(input) ? FpClass::QuietNaN : classify(input))
        << bits;
    EXPECT_EQ(widened.flags.contains(ExceptionFlag::Invalid),
              is_signaling_nan(input))
        << bits;
  }
}

TEST(Conversions, Bf16RoundingModesFlagsAndSpecials) {
  const Environment environment;
  constexpr Fp32 halfway{0x3F808000u};
  EXPECT_EQ(environment.convert<Bf16>(halfway).value.bits, 0x3F80u);
  EXPECT_EQ(environment.convert<Bf16>(halfway, {RoundingMode::TowardPositive})
                .value.bits,
            0x3F81u);
  EXPECT_EQ(
      environment
          .convert<Bf16>(Fp32{0xBF808000u}, {RoundingMode::TowardNegative})
          .value.bits,
      0xBF81u);
  EXPECT_TRUE(environment.convert<Bf16>(halfway).flags.contains(
      ExceptionFlag::Inexact));

  const auto tiny = environment.convert<Bf16>(Fp32{1});
  EXPECT_EQ(tiny.value.bits, 0);
  EXPECT_TRUE(tiny.flags.contains(ExceptionFlag::Underflow));
  EXPECT_TRUE(tiny.flags.contains(ExceptionFlag::Inexact));
  EXPECT_EQ(environment.convert<Bf16>(Fp32{0x7F800000u}).value.bits, 0x7F80u);
  EXPECT_TRUE(environment.convert<Bf16>(Fp32{0x7F800001u})
                  .flags.contains(ExceptionFlag::Invalid));
}

TEST(Conversions, Tf32Fp8AndFp4Boundaries) {
  const Environment environment;
  EXPECT_EQ(environment.convert<Tf32>(Fp32{0x3F801000u}).value.bits,
            0x3F800000u);
  EXPECT_EQ(environment.convert<Tf32>(Fp32{0x3F803000u}).value.bits,
            0x3F804000u);
  EXPECT_EQ(environment.convert<Fp32>(Tf32{0x3F801FFFu}).value.bits,
            0x3F800000u);

  EXPECT_EQ(environment.convert<Fp8E4M3>(Fp32{0x3F800000u}).value.bits, 0x38u);
  EXPECT_EQ(environment.convert<Fp8E5M2>(Fp32{0x3F800000u}).value.bits, 0x3Cu);
  EXPECT_EQ(environment.convert<Fp4E2M1>(Fp32{0x3F800000u}).value.bits, 0x02u);
  EXPECT_EQ(environment.convert<Fp32>(Fp8E4M3{0x7Eu}).value.bits, 0x43E00000u);
  EXPECT_EQ(environment.convert<Fp32>(Fp4E2M1{0x07u}).value.bits, 0x40C00000u);

  const auto e4_overflow = environment.convert<Fp8E4M3>(Fp32{0x7F800000u});
  EXPECT_EQ(e4_overflow.value.bits, 0x7Eu);
  EXPECT_TRUE(e4_overflow.flags.contains(ExceptionFlag::Overflow));
  EXPECT_FALSE(is_nan(e4_overflow.value));
  const auto e5_sat =
      environment.convert<Fp8E5M2>(Fp32{0x7F7FFFFFu}, {.satfinite = true});
  EXPECT_EQ(e5_sat.value.bits, 0x7Bu);
  EXPECT_TRUE(e5_sat.flags.contains(ExceptionFlag::Overflow));
  const auto fp4_nan = environment.convert<Fp4E2M1>(Fp32{0x7FC00000u});
  EXPECT_EQ(fp4_nan.value.bits, 0x07u);
  EXPECT_TRUE(fp4_nan.flags.contains(ExceptionFlag::Invalid));
}

TEST(Conversions, ExhaustiveFp8AndFp4RoundTrip) {
  const Environment environment;
  for (unsigned bits = 0; bits != 256; ++bits) {
    const Fp8E4M3 e4{static_cast<std::uint8_t>(bits)};
    const auto e4_result =
        environment.convert<Fp8E4M3>(environment.convert<Fp32>(e4).value);
    EXPECT_EQ(e4_result.value.bits, e4.bits) << bits;

    const Fp8E5M2 e5{static_cast<std::uint8_t>(bits)};
    const auto e5_result =
        environment.convert<Fp8E5M2>(environment.convert<Fp32>(e5).value);
    const auto expected_e5 = is_signaling_nan(e5)
                                 ? static_cast<std::uint8_t>(e5.bits | 0x02u)
                                 : e5.bits;
    EXPECT_EQ(e5_result.value.bits, expected_e5) << bits;
  }
  for (unsigned bits = 0; bits != 16; ++bits) {
    const Fp4E2M1 fp4{static_cast<std::uint8_t>(bits)};
    EXPECT_EQ(environment.convert<Fp4E2M1>(environment.convert<Fp32>(fp4).value)
                  .value.bits,
              fp4.bits)
        << bits;
  }
}

TEST(Conversions, UnsupportedWideningControlsAreRejected) {
  const Environment environment;
  EXPECT_THROW(static_cast<void>(environment.convert<Fp32>(
                   Bf16{0x3F80u}, {RoundingMode::TowardZero})),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(environment.convert<Fp32>(
                   Fp8E5M2{0x3Cu}, {.satfinite = true})),
               std::invalid_argument);
}

}  // namespace ptxsim::fp::test
