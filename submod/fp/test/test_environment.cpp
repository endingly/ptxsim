#include <ptxsim/fp/environment.hpp>
#include <ptxsim/fp/validation.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

extern "C" {
#include <softfloat/softfloat.h>
}

#include <gtest/gtest.h>

namespace ptxsim::fp::test {
namespace {

class ReferenceState {
 public:
  explicit ReferenceState(RoundingMode rounding)
      : rounding_(softfloat_roundingMode),
        tininess_(softfloat_detectTininess),
        flags_(softfloat_exceptionFlags) {
    softfloat_roundingMode = to_softfloat(rounding);
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
  }

  ~ReferenceState() {
    softfloat_roundingMode = rounding_;
    softfloat_detectTininess = tininess_;
    softfloat_exceptionFlags = flags_;
  }

  [[nodiscard]] std::uint8_t flags() const {
    std::uint8_t result = 0;
    if ((softfloat_exceptionFlags & softfloat_flag_inexact) != 0)
      result |= static_cast<std::uint8_t>(ExceptionFlag::Inexact);
    if ((softfloat_exceptionFlags & softfloat_flag_underflow) != 0)
      result |= static_cast<std::uint8_t>(ExceptionFlag::Underflow);
    if ((softfloat_exceptionFlags & softfloat_flag_overflow) != 0)
      result |= static_cast<std::uint8_t>(ExceptionFlag::Overflow);
    if ((softfloat_exceptionFlags & softfloat_flag_infinite) != 0)
      result |= static_cast<std::uint8_t>(ExceptionFlag::DivideByZero);
    if ((softfloat_exceptionFlags & softfloat_flag_invalid) != 0)
      result |= static_cast<std::uint8_t>(ExceptionFlag::Invalid);
    return result;
  }

 private:
  static std::uint_fast8_t to_softfloat(RoundingMode mode) {
    switch (mode) {
      case RoundingMode::NearestEven:
        return softfloat_round_near_even;
      case RoundingMode::TowardZero:
        return softfloat_round_minMag;
      case RoundingMode::TowardNegative:
        return softfloat_round_min;
      case RoundingMode::TowardPositive:
        return softfloat_round_max;
    }
    std::unreachable();
  }

  std::uint_fast8_t rounding_;
  std::uint_fast8_t tininess_;
  std::uint_fast8_t flags_;
};

struct ReferenceF32 {
  Fp32 value;
  std::uint8_t flags;
};

struct ReferenceF64 {
  Fp64 value;
  std::uint8_t flags;
};

[[nodiscard]] ReferenceF32 reference_f32_fma(Fp32 a, Fp32 b, Fp32 c,
                                             RoundingMode rounding) {
  ReferenceState state{rounding};
  const auto value = f32_mulAdd(float32_t{.v = a.bits}, float32_t{.v = b.bits},
                                float32_t{.v = c.bits});
  return ReferenceF32{Fp32{value.v}, state.flags()};
}

[[nodiscard]] ReferenceF64 reference_f64_fma(Fp64 a, Fp64 b, Fp64 c,
                                             RoundingMode rounding) {
  ReferenceState state{rounding};
  const auto value = f64_mulAdd(float64_t{.v = a.bits}, float64_t{.v = b.bits},
                                float64_t{.v = c.bits});
  return ReferenceF64{Fp64{value.v}, state.flags()};
}

}  // namespace

TEST(Environment, F32Arithmetic) {
  const Environment environment;
  EXPECT_EQ(environment.add(Fp32{0x3F800000}, Fp32{0x3F000000}).value.bits,
            0x3FC00000u);
  EXPECT_EQ(environment.sub(Fp32{0x3FC00000}, Fp32{0x3F000000}).value.bits,
            0x3F800000u);
  EXPECT_EQ(environment.mul(Fp32{0x3FC00000}, Fp32{0x40000000}).value.bits,
            0x40400000u);
  EXPECT_EQ(
      environment.fma(Fp32{0x40000000}, Fp32{0x40400000}, Fp32{0x3F800000})
          .value.bits,
      0x40E00000u);
  EXPECT_EQ(environment.div(Fp32{0x40C00000}, Fp32{0x40000000}).value.bits,
            0x40400000u);
  EXPECT_EQ(environment.sqrt(Fp32{0x40800000}).value.bits, 0x40000000u);
}

TEST(Environment, F64Arithmetic) {
  const Environment environment;
  EXPECT_EQ(
      environment.add(Fp64{0x3FF0000000000000ULL}, Fp64{0x3FE0000000000000ULL})
          .value.bits,
      0x3FF8000000000000ULL);
  EXPECT_EQ(
      environment.sub(Fp64{0x3FF8000000000000ULL}, Fp64{0x3FE0000000000000ULL})
          .value.bits,
      0x3FF0000000000000ULL);
  EXPECT_EQ(
      environment.mul(Fp64{0x3FF8000000000000ULL}, Fp64{0x4000000000000000ULL})
          .value.bits,
      0x4008000000000000ULL);
  EXPECT_EQ(environment
                .fma(Fp64{0x4000000000000000ULL}, Fp64{0x4008000000000000ULL},
                     Fp64{0x3FF0000000000000ULL})
                .value.bits,
            0x401C000000000000ULL);
  EXPECT_EQ(
      environment.div(Fp64{0x4008000000000000ULL}, Fp64{0x4000000000000000ULL})
          .value.bits,
      0x3FF8000000000000ULL);
  EXPECT_EQ(environment.sqrt(Fp64{0x4010000000000000ULL}).value.bits,
            0x4000000000000000ULL);
}

TEST(Environment, RoundingModesAndFma) {
  const Environment environment;
  constexpr Fp32 one{0x3F800000};
  constexpr Fp32 half_ulp{0x33800000};

  EXPECT_EQ(
      environment.add(one, half_ulp, {RoundingMode::NearestEven}).value.bits,
      one.bits);
  EXPECT_EQ(
      environment.add(one, half_ulp, {RoundingMode::TowardZero}).value.bits,
      one.bits);
  EXPECT_EQ(
      environment.add(one, half_ulp, {RoundingMode::TowardNegative}).value.bits,
      one.bits);
  EXPECT_EQ(
      environment.add(one, half_ulp, {RoundingMode::TowardPositive}).value.bits,
      0x3F800001u);

  for (const auto rounding :
       {RoundingMode::NearestEven, RoundingMode::TowardZero,
        RoundingMode::TowardNegative, RoundingMode::TowardPositive}) {
    const auto actual = environment.fma(one, one, half_ulp, {rounding});
    const auto expected = reference_f32_fma(one, one, half_ulp, rounding);
    EXPECT_EQ(actual.value, expected.value);
    EXPECT_EQ(actual.flags.bits(), expected.flags);
  }
}

TEST(Environment, ExceptionFlagsAreResults) {
  const Environment environment;
  const auto divide_by_zero = environment.div(Fp32{0x3F800000}, Fp32{});
  EXPECT_EQ(divide_by_zero.value.bits, 0x7F800000u);
  EXPECT_TRUE(divide_by_zero.flags.contains(ExceptionFlag::DivideByZero));

  const auto invalid = environment.sqrt(Fp32{0xBF800000});
  EXPECT_TRUE(is_nan(invalid.value));
  EXPECT_TRUE(invalid.flags.contains(ExceptionFlag::Invalid));
}

TEST(Environment, RestoresSoftFloatState) {
  const auto saved_rounding = softfloat_roundingMode;
  const auto saved_tininess = softfloat_detectTininess;
  const auto saved_flags = softfloat_exceptionFlags;
  softfloat_roundingMode = softfloat_round_max;
  softfloat_detectTininess = softfloat_tininess_beforeRounding;
  softfloat_exceptionFlags = softfloat_flag_invalid;

  const Environment environment;
  static_cast<void>(environment.add(Fp32{0x3F800000}, Fp32{0x33800000},
                                    {RoundingMode::NearestEven}));
  EXPECT_EQ(softfloat_roundingMode, softfloat_round_max);
  EXPECT_EQ(softfloat_detectTininess, softfloat_tininess_beforeRounding);
  EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_invalid);

  softfloat_roundingMode = saved_rounding;
  softfloat_detectTininess = saved_tininess;
  softfloat_exceptionFlags = saved_flags;
}

TEST(Environment, FtzPreservesSignedZero) {
  const Environment environment;
  constexpr ArithmeticControl ftz{.flush_subnormal = true};
  const auto input = environment.mul(Fp32{0x80000001}, Fp32{0x3F800000}, ftz);
  EXPECT_EQ(input.value.bits, 0x80000000u);

  const auto output = environment.mul(Fp32{0x80800000}, Fp32{0x3F000000}, ftz);
  EXPECT_EQ(output.value.bits, 0x80000000u);
  EXPECT_FALSE(output.flags.contains(ExceptionFlag::Underflow));
}

TEST(Environment, F64FtzIsRejected) {
  const Environment environment;
  EXPECT_THROW(static_cast<void>(environment.add(Fp64{0x3FF0000000000000ULL},
                                                 Fp64{0x3FF0000000000000ULL},
                                                 {.flush_subnormal = true})),
               std::invalid_argument);
}

TEST(Environment, SelectedConversions) {
  const Environment environment;
  constexpr auto nearest = RoundingMode::NearestEven;
  EXPECT_EQ(environment.i32_to_f32(-42, nearest).value.bits, 0xC2280000u);
  EXPECT_EQ(environment.u32_to_f32(42, nearest).value.bits, 0x42280000u);
  EXPECT_TRUE(environment.i32_to_f32(16'777'217, nearest)
                  .flags.contains(ExceptionFlag::Inexact));
  EXPECT_EQ(environment.i32_to_f64(-42, nearest).value.bits,
            0xC045000000000000ULL);
  EXPECT_EQ(environment.u32_to_f64(42, nearest).value.bits,
            0x4045000000000000ULL);
  const auto f32_to_i32 =
      environment.f32_to_i32(Fp32{0x3FC00000}, RoundingMode::TowardZero);
  EXPECT_EQ(f32_to_i32.value, 1);
  EXPECT_TRUE(f32_to_i32.flags.contains(ExceptionFlag::Inexact));
  EXPECT_EQ(
      environment.f32_to_u32(Fp32{0x3FC00000}, RoundingMode::TowardPositive)
          .value,
      2u);
  EXPECT_EQ(
      environment
          .f64_to_i32(Fp64{0x3FF8000000000000ULL}, RoundingMode::TowardZero)
          .value,
      1);
  EXPECT_EQ(
      environment
          .f64_to_u32(Fp64{0x3FF8000000000000ULL}, RoundingMode::TowardPositive)
          .value,
      2u);
  EXPECT_EQ(environment.f32_to_f64(Fp32{0x3FC00000}).value.bits,
            0x3FF8000000000000ULL);
  EXPECT_EQ(
      environment.f64_to_f32(Fp64{0x3FF8000000000000ULL}, nearest).value.bits,
      0x3FC00000u);
}

TEST(Types, ClassificationAndSignedZero) {
  EXPECT_EQ(classify(Fp16{}), FpClass::Zero);
  EXPECT_EQ(classify(Fp16{0x0001}), FpClass::Subnormal);
  EXPECT_EQ(classify(Fp16{0x3C00}), FpClass::Normal);
  EXPECT_EQ(classify(Fp16{0x7C00}), FpClass::Infinity);
  EXPECT_EQ(classify(Fp16{0x7E00}), FpClass::QuietNaN);
  EXPECT_EQ(classify(Fp16{0x7C01}), FpClass::SignalingNaN);
  EXPECT_TRUE(is_negative_zero(Fp16{0x8000}));

  EXPECT_EQ(classify(Fp32{}), FpClass::Zero);
  EXPECT_EQ(classify(Fp32{0x00000001}), FpClass::Subnormal);
  EXPECT_EQ(classify(Fp32{0x3F800000}), FpClass::Normal);
  EXPECT_EQ(classify(Fp32{0x7F800000}), FpClass::Infinity);
  EXPECT_EQ(classify(Fp32{0x7FC00000}), FpClass::QuietNaN);
  EXPECT_EQ(classify(Fp32{0x7F800001}), FpClass::SignalingNaN);
  EXPECT_TRUE(is_negative_zero(Fp32{0x80000000}));

  EXPECT_EQ(classify(Fp64{}), FpClass::Zero);
  EXPECT_EQ(classify(Fp64{0x0000000000000001ULL}), FpClass::Subnormal);
  EXPECT_EQ(classify(Fp64{0x3FF0000000000000ULL}), FpClass::Normal);
  EXPECT_EQ(classify(Fp64{0x7FF0000000000000ULL}), FpClass::Infinity);
  EXPECT_EQ(classify(Fp64{0x7FF8000000000000ULL}), FpClass::QuietNaN);
  EXPECT_EQ(classify(Fp64{0x7FF0000000000001ULL}), FpClass::SignalingNaN);
  EXPECT_TRUE(is_negative_zero(Fp64{0x8000000000000000ULL}));
}

TEST(Validation, AllPolicies) {
  using namespace validation;
  constexpr Fp32 one{0x3F800000};
  constexpr Fp32 next{0x3F800001};
  EXPECT_TRUE(bit_exact(one, one));
  EXPECT_FALSE(bit_exact(one, next));
  EXPECT_TRUE(same_float_class(Fp32{0x7FC00001}, Fp32{0xFFC00002}));
  EXPECT_EQ(ulp_distance(one, next), 1u);
  EXPECT_EQ(ulp_distance(Fp32{}, Fp32{0x80000000}), 0u);
  EXPECT_TRUE(within_ulp(one, next, 1));
  EXPECT_TRUE(within_relative(one, next, 0.000001F));
  EXPECT_TRUE(within_absolute(one, next, 0.000001F));
  EXPECT_FALSE(within_ulp(Fp32{0x7FC00000}, one, 10));
  EXPECT_FALSE(within_ulp(Fp32{0x7FC00000}, Fp32{0x7FC00000},
                          std::numeric_limits<std::uint32_t>::max()));
  EXPECT_TRUE(within_relative(Fp32{0x7F800000}, Fp32{0x7F800000}, 0.0F));
  EXPECT_TRUE(within_absolute(Fp32{0x7F800000}, Fp32{0x7F800000}, 0.0F));
  EXPECT_FALSE(within_relative(Fp32{0x7F800000}, one, 1.0F));
  EXPECT_FALSE(within_absolute(Fp32{0x7F800000}, Fp32{0xFF800000}, 1.0F));
  EXPECT_FALSE(within_relative(Fp32{0x7FC00000}, Fp32{0x7FC00000}, 1.0F));

  constexpr Fp64 double_one{0x3FF0000000000000ULL};
  constexpr Fp64 double_next{0x3FF0000000000001ULL};
  EXPECT_TRUE(bit_exact(double_one, double_one));
  EXPECT_TRUE(same_float_class(Fp64{0x7FF8000000000001ULL},
                               Fp64{0xFFF8000000000002ULL}));
  EXPECT_EQ(ulp_distance(double_one, double_next), 1u);
  EXPECT_TRUE(within_ulp(double_one, double_next, 1));
  EXPECT_TRUE(within_relative(double_one, double_next, 0.000001));
  EXPECT_TRUE(within_absolute(double_one, double_next, 0.000001));
  EXPECT_FALSE(within_ulp(Fp64{0x7FF8000000000000ULL},
                          Fp64{0x7FF8000000000000ULL},
                          std::numeric_limits<std::uint64_t>::max()));
  EXPECT_TRUE(within_relative(Fp64{0x7FF0000000000000ULL},
                              Fp64{0x7FF0000000000000ULL}, 0.0));
  EXPECT_FALSE(within_absolute(Fp64{0x7FF0000000000000ULL}, double_one, 1.0));
}

TEST(Environment, DeterministicRandomizedSoftFloatCorpus) {
  const Environment environment;
  std::mt19937_64 random{0xA4B1C2D3E4F56789ULL};
  const auto modes = {RoundingMode::NearestEven, RoundingMode::TowardZero,
                      RoundingMode::TowardNegative,
                      RoundingMode::TowardPositive};
  for (const auto rounding : modes) {
    for (int index = 0; index != 256; ++index) {
      const Fp32 a{static_cast<std::uint32_t>(random())};
      const Fp32 b{static_cast<std::uint32_t>(random())};
      const Fp32 c{static_cast<std::uint32_t>(random())};
      const auto actual = environment.fma(a, b, c, {rounding});
      const auto expected = reference_f32_fma(a, b, c, rounding);
      EXPECT_EQ(actual.value, expected.value) << index;
      EXPECT_EQ(actual.flags.bits(), expected.flags) << index;

      const Fp64 d{random()};
      const Fp64 e{random()};
      const Fp64 f{random()};
      const auto actual64 = environment.fma(d, e, f, {rounding});
      const auto expected64 = reference_f64_fma(d, e, f, rounding);
      EXPECT_EQ(actual64.value, expected64.value) << index;
      EXPECT_EQ(actual64.flags.bits(), expected64.flags) << index;
    }
  }
}

}  // namespace ptxsim::fp::test
