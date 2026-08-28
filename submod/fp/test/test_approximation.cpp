#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace ptxsim::fp::test {
namespace {

template <typename T>
concept HasApproximation = requires(const Environment& environment, T value) {
  environment.tanh_approx(value);
  environment.ex2_approx(value);
};

template <typename T>
concept HasMadAndRcp = requires(const Environment& environment, T value) {
  environment.mad(value, value, value);
  environment.rcp(value);
};

static_assert(HasMadAndRcp<Fp32>);
static_assert(HasMadAndRcp<Fp64>);
static_assert(HasApproximation<Fp16>);
static_assert(HasApproximation<Bf16>);
static_assert(!HasMadAndRcp<Tf32>);

}  // namespace

TEST(ModernExactOperations, MadAndReciprocalUseExactBackend) {
  const Environment environment;
  EXPECT_EQ(
      environment.mad(Fp32{0x40000000u}, Fp32{0x40400000u}, Fp32{0x3F800000u})
          .value.bits,
      0x40E00000u);
  EXPECT_EQ(environment
                .mad(Fp64{0x4000000000000000ULL}, Fp64{0x4008000000000000ULL},
                     Fp64{0x3FF0000000000000ULL})
                .value.bits,
            0x401C000000000000ULL);
  EXPECT_EQ(environment.rcp(Fp32{0x40000000u}).value.bits, 0x3F000000u);
  EXPECT_EQ(environment.rcp(Fp64{0x4000000000000000ULL}).value.bits,
            0x3FE0000000000000ULL);
  EXPECT_EQ(
      environment.rcp(Fp32{0x00000001u}, {.flush_subnormal = true}).value.bits,
      0x7F800000u);
}

TEST(ApproximationF32, SpecialCasesAndReferenceAccuracy) {
  const Environment environment;
  EXPECT_EQ(
      environment.div_approx(Fp32{0x40400000u}, Fp32{0x40000000u}).value.bits,
      0x3FC00000u);
  EXPECT_EQ(
      environment.div_full(Fp32{0x40400000u}, Fp32{0x40000000u}).value.bits,
      0x3FC00000u);
  EXPECT_EQ(environment.rcp_approx(Fp32{0x40000000u}).value.bits, 0x3F000000u);
  EXPECT_EQ(environment.sqrt_approx(Fp32{0x40800000u}).value.bits, 0x40000000u);
  EXPECT_EQ(environment.rsqrt_approx(Fp32{0x40800000u}).value.bits,
            0x3F000000u);
  EXPECT_EQ(environment.sin_approx(Fp32{0x80000000u}).value.bits, 0x80000000u);
  EXPECT_EQ(environment.cos_approx(Fp32{}).value.bits, 0x3F800000u);
  EXPECT_EQ(environment.lg2_approx(Fp32{}).value.bits, 0xFF800000u);
  EXPECT_TRUE(environment.lg2_approx(Fp32{0xBF800000u})
                  .flags.contains(ExceptionFlag::Invalid));
  EXPECT_EQ(environment.ex2_approx(Fp32{0xFF800000u}).value.bits, 0u);
  EXPECT_EQ(environment.ex2_approx(Fp32{0x7F800000u}).value.bits, 0x7F800000u);
  EXPECT_EQ(environment.tanh_approx(Fp32{0xFF800000u}).value.bits, 0xBF800000u);

  // High-precision reference constants, independent from the host-libm
  // implementation used by the approximation backend. PTX 9.3 bounds are
  // 2^-20.5 absolute for sin/cos in [-2pi, 2pi], 2^-22 absolute for lg2 in
  // (0.5, 2), two ULP for ex2, and 2^-11 relative for tanh.
  constexpr float sin_reference = 0.47942553860420300538F;
  constexpr float cos_reference = 0.87758256189037271612F;
  constexpr float lg2_reference = 0.58496250072115618145F;
  constexpr float ex2_reference = 1.41421356237309504880F;
  constexpr float tanh_reference = 0.46211715726000975850F;
  const auto as_float = [](Fp32 value) {
    return std::bit_cast<float>(value.bits);
  };
  EXPECT_LE(std::abs(as_float(environment.sin_approx(Fp32{0x3F000000u}).value) -
                     sin_reference),
            6.75e-7F);
  EXPECT_LE(std::abs(as_float(environment.cos_approx(Fp32{0x3F000000u}).value) -
                     cos_reference),
            6.75e-7F);
  EXPECT_LE(std::abs(as_float(environment.lg2_approx(Fp32{0x3FC00000u}).value) -
                     lg2_reference),
            2.3841858e-7F);
  EXPECT_LE(std::abs(as_float(environment.ex2_approx(Fp32{0x3F000000u}).value) -
                     ex2_reference),
            2.3841858e-7F);
  EXPECT_LE(
      std::abs((as_float(environment.tanh_approx(Fp32{0x3F000000u}).value) -
                tanh_reference) /
               tanh_reference),
      4.8828125e-4F);
  EXPECT_EQ(environment.sin_approx(Fp32{0x00000001u}, {.flush_subnormal = true})
                .value.bits,
            0u);
}

TEST(ApproximationF64, FtzAlgorithmsCanonicalizeAndTruncate) {
  const Environment environment;
  const auto reciprocal =
      environment.rcp_approx_ftz(Fp64{0x4008000000000000ULL});
  EXPECT_EQ(reciprocal.value.bits & 0x00000000FFFFFFFFULL, 0ULL);
  constexpr Fp64 low_zero{0x4008000000000000ULL};
  constexpr Fp64 low_ones{0x40080000FFFFFFFFULL};
  constexpr std::uint64_t high_word_mask = 0xFFFFFFFF00000000ULL;
  EXPECT_NE(environment.rcp(low_zero).value.bits & high_word_mask,
            environment.rcp(low_ones).value.bits & high_word_mask);
  EXPECT_EQ(environment.rcp_approx_ftz(low_zero).value.bits,
            environment.rcp_approx_ftz(low_ones).value.bits);
  EXPECT_NE(environment.rsqrt_approx(low_zero).value.bits & high_word_mask,
            environment.rsqrt_approx(low_ones).value.bits & high_word_mask);
  EXPECT_EQ(environment.rsqrt_approx_ftz(low_zero).value.bits,
            environment.rsqrt_approx_ftz(low_ones).value.bits);
  EXPECT_EQ(environment.rcp_approx_ftz(Fp64{0x0000000000000001ULL}).value.bits,
            0x7FF0000000000000ULL);
  EXPECT_EQ(environment.rsqrt_approx(Fp64{0x4010000000000000ULL}).value.bits,
            0x3FE0000000000000ULL);
  const auto negative =
      environment.rsqrt_approx_ftz(Fp64{0xBFF0000000000000ULL});
  EXPECT_EQ(negative.value.bits, 0x7FFFFFFF00000000ULL);
  EXPECT_TRUE(negative.flags.contains(ExceptionFlag::Invalid));
  const auto signaling =
      environment.rcp_approx_ftz(Fp64{0x7FF0000100000000ULL});
  EXPECT_EQ(signaling.value.bits, 0x7FFFFFFF00000000ULL);
  EXPECT_TRUE(signaling.flags.contains(ExceptionFlag::Invalid));
}

TEST(ApproximationLowPrecision, TanhAndEx2HaveSpecifiedFtzBoundary) {
  const Environment environment;
  EXPECT_EQ(environment.tanh_approx(Fp16{}).value.bits, 0u);
  EXPECT_EQ(environment.ex2_approx(Fp16{0x3C00u}).value.bits, 0x4000u);
  EXPECT_EQ(environment.tanh_approx(Fp16{0x3800u}).value.bits, 0x3765u);
  EXPECT_EQ(environment.ex2_approx(Fp16{0x3800u}).value.bits, 0x3DA8u);
  EXPECT_EQ(environment.tanh_approx(Bf16{}).value.bits, 0u);
  EXPECT_EQ(environment.ex2_approx(Bf16{0x3F80u}).value.bits, 0x4000u);
  EXPECT_EQ(environment.tanh_approx(Bf16{0x3F00u}).value.bits, 0x3EEDu);
  EXPECT_EQ(environment.ex2_approx(Bf16{0x3F00u}).value.bits, 0x3FB5u);
  EXPECT_EQ(environment.ex2_approx(Bf16{0x0001u}).value.bits, 0x3F80u);
}

TEST(ResultModifiers, SaturateAndReluComposeWithArithmeticResults) {
  const Environment environment;
  EXPECT_EQ(saturate(environment.add(Fp32{0x3F800000u}, Fp32{0x3F800000u}))
                .value.bits,
            0x3F800000u);
  EXPECT_EQ(saturate(Result<Fp16>{Fp16{0x7E00u}, {}}).value.bits, 0u);
  EXPECT_EQ(relu(Result<Bf16>{Bf16{0xBF80u}, {}}).value.bits, 0u);
  EXPECT_EQ(relu(Result<Fp16>{Fp16{0x7E01u}, {}}).value.bits, 0x7E00u);
  EXPECT_EQ(relu(Result<Bf16>{Bf16{0x7FC1u}, {}}).value.bits, 0x7FC0u);
}

}  // namespace ptxsim::fp::test
