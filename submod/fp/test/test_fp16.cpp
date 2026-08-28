#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

namespace ptxsim::fp::test {

TEST(Fp16Arithmetic, NativeSoftFloatOperationsAndRnOnlyPolicy) {
  const Environment environment;
  EXPECT_EQ(environment.add(Fp16{0x3C00u}, Fp16{0x3800u}).value.bits, 0x3E00u);
  EXPECT_EQ(environment.sub(Fp16{0x3C00u}, Fp16{0x3800u}).value.bits, 0x3800u);
  EXPECT_EQ(environment.mul(Fp16{0x3E00u}, Fp16{0x4000u}).value.bits, 0x4200u);
  EXPECT_EQ(
      environment.fma(Fp16{0x4000u}, Fp16{0x4200u}, Fp16{0x3C00u}).value.bits,
      0x4700u);

  EXPECT_THROW(
      static_cast<void>(environment.add(Fp16{0x3C00u}, Fp16{0x3800u},
                                        {RoundingMode::TowardPositive})),
      std::invalid_argument);
  EXPECT_THROW(static_cast<void>(environment.fma(Fp16{0x3C00u}, Fp16{0x3C00u},
                                                 Fp16{0x3C00u},
                                                 {RoundingMode::TowardZero})),
               std::invalid_argument);
}

TEST(Fp16Arithmetic, FtzNaNAndFlags) {
  const Environment environment;
  constexpr ArithmeticControl ftz{.flush_subnormal = true};
  EXPECT_EQ(environment.mul(Fp16{0x8001u}, Fp16{0x3C00u}, ftz).value.bits,
            0x8000u);
  EXPECT_EQ(environment.mul(Fp16{0x0400u}, Fp16{0x3800u}, ftz).value.bits,
            0x0000u);

  const auto signaling = environment.add(Fp16{0x7C01u}, Fp16{0x3C00u});
  EXPECT_EQ(signaling.value.bits, 0x7E01u);
  EXPECT_TRUE(signaling.flags.contains(ExceptionFlag::Invalid));
  const auto inexact = environment.add(Fp16{0x3C00u}, Fp16{0x1000u});
  EXPECT_TRUE(inexact.flags.contains(ExceptionFlag::Inexact));
}

TEST(Fp16Conversions, NativeF32AndF64Conversions) {
  const Environment environment;
  EXPECT_EQ(environment.f32_to_f16(Fp32{0x3F800000u}, RoundingMode::NearestEven)
                .value.bits,
            0x3C00u);
  EXPECT_EQ(environment.f16_to_f32(Fp16{0x3E00u}).value.bits, 0x3FC00000u);
  EXPECT_EQ(
      environment
          .f64_to_f16(Fp64{0x3FF0000000000000ULL}, RoundingMode::NearestEven)
          .value.bits,
      0x3C00u);
  EXPECT_EQ(environment.f16_to_f64(Fp16{0x3E00u}).value.bits,
            0x3FF8000000000000ULL);

  constexpr Fp32 halfway{0x3F801000u};
  EXPECT_EQ(environment.convert<Fp16>(halfway).value.bits, 0x3C00u);
  EXPECT_EQ(environment.convert<Fp16>(halfway, {RoundingMode::TowardPositive})
                .value.bits,
            0x3C01u);
  EXPECT_TRUE(environment.convert<Fp16>(halfway).flags.contains(
      ExceptionFlag::Inexact));
  EXPECT_THROW(static_cast<void>(environment.convert<Fp16>(
                   Fp32{0x3F800000u}, {.satfinite = true})),
               std::invalid_argument);
}

TEST(MixedPrecision, LowPrecisionOperandsWidenExactlyThenRoundF32) {
  const Environment environment;
  EXPECT_EQ(environment.add(Fp16{0x3C00u}, Fp32{0x3F000000u}).value.bits,
            0x3FC00000u);
  EXPECT_EQ(environment.sub(Fp16{0x3C00u}, Fp32{0x3F000000u}).value.bits,
            0x3F000000u);
  EXPECT_EQ(environment.fma(Fp16{0x4000u}, Fp16{0x4200u}, Fp32{0x3F000000u})
                .value.bits,
            0x40D00000u);
  EXPECT_EQ(environment
                .fma(Bf16{0x3F80u}, Bf16{0x3F80u}, Fp32{0x33800000u},
                     {RoundingMode::TowardPositive})
                .value.bits,
            0x3F800001u);
  EXPECT_TRUE(environment.fma(Fp16{0x7C00u}, Fp16{}, Fp32{})
                  .flags.contains(ExceptionFlag::Invalid));
  EXPECT_THROW(
      static_cast<void>(environment.add(Fp16{0x3C00u}, Fp32{0x3F800000u},
                                        {.flush_subnormal = true})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(environment.fma(Bf16{0x3F80u}, Bf16{0x3F80u}, Fp32{},
                                        {.flush_subnormal = true})),
      std::invalid_argument);
}

}  // namespace ptxsim::fp::test
