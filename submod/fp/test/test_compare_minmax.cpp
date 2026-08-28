#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

namespace ptxsim::fp::test {

TEST(Compare, OrderedUnorderedSignedZeroAndFtz) {
  const Environment environment;
  EXPECT_TRUE(
      environment.compare(Fp16{}, Fp16{0x8000u}, CompareOp::Equal).value);
  EXPECT_FALSE(
      environment.compare(Fp32{}, Fp32{0x80000000u}, CompareOp::Less).value);
  EXPECT_FALSE(environment
                   .compare(Fp64{0x7FF8000000000000ULL},
                            Fp64{0x3FF0000000000000ULL}, CompareOp::NotEqual)
                   .value);
  EXPECT_TRUE(environment
                  .compare(Fp64{0x7FF8000000000000ULL},
                           Fp64{0x3FF0000000000000ULL},
                           CompareOp::NotEqualUnordered)
                  .value);
  const auto signaling =
      environment.compare(Fp32{0x7F800001u}, Fp32{}, CompareOp::NaN);
  EXPECT_TRUE(signaling.value);
  EXPECT_TRUE(signaling.flags.contains(ExceptionFlag::Invalid));
  EXPECT_TRUE(environment
                  .compare(Fp32{0x00000001u}, Fp32{}, CompareOp::Equal,
                           {.flush_subnormal = true})
                  .value);
}

TEST(MinMax, NanSignedZeroModifiersAndFtz) {
  const Environment environment;
  EXPECT_EQ(environment.min(Fp16{}, Fp16{0x8000u}).value.bits, 0x8000u);
  EXPECT_EQ(environment.max(Bf16{}, Bf16{0x8000u}).value.bits, 0x0000u);
  const auto one_nan = environment.min(Fp32{0x7FC00123u}, Fp32{0x3F800000u});
  EXPECT_EQ(one_nan.value.bits, 0x3F800000u);
  const auto both_nan = environment.max(Fp32{0x7FC00123u}, Fp32{0xFFC00456u});
  EXPECT_EQ(both_nan.value.bits, 0x7FC00123u);
  const auto canonical = environment.max(Fp32{0x7FC00123u}, Fp32{0x3F800000u},
                                         {.propagate_nan = true});
  EXPECT_EQ(canonical.value.bits, 0x7FC00000u);
  const auto signaling = environment.min(Fp16{0x7C01u}, Fp16{0x3C00u});
  EXPECT_EQ(signaling.value.bits, 0x3C00u);
  EXPECT_TRUE(signaling.flags.contains(ExceptionFlag::Invalid));

  EXPECT_EQ(environment
                .min(Fp32{0xC0400000u}, Fp32{0x40000000u},
                     {.absolute = true, .xor_sign = true})
                .value.bits,
            0xC0000000u);
  EXPECT_THROW(
      static_cast<void>(environment.min(Fp32{}, Fp32{}, {.xor_sign = true})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(environment.min(Fp16{}, Fp16{}, {.absolute = true})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(environment.max(Bf16{}, Bf16{}, {.absolute = true})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(environment.min(Fp32{}, Fp32{}, {.absolute = true})),
      std::invalid_argument);
  EXPECT_EQ(environment
                .min(Fp16{0xFE00u}, Fp16{0x4000u},
                     {.absolute = true, .xor_sign = true})
                .value.bits,
            0xC000u);
  EXPECT_EQ(environment
                .max(Bf16{0x7FC1u}, Bf16{0xBF80u},
                     {.absolute = true, .xor_sign = true})
                .value.bits,
            0xBF80u);
  EXPECT_EQ(environment
                .min(Fp32{0x7FC00123u}, Fp32{0xC0400000u},
                     {.absolute = true, .xor_sign = true})
                .value.bits,
            0xC0400000u);
  EXPECT_EQ(environment
                .max(Fp32{0xFFC00123u}, Fp32{0x7FC00456u},
                     {.absolute = true, .xor_sign = true})
                .value.bits,
            0xFFC00123u);
  EXPECT_EQ(
      environment.min(Fp32{0x00000001u}, Fp32{}, {}, {.flush_subnormal = true})
          .value.bits,
      0x00000000u);

  EXPECT_THROW(static_cast<void>(
                   environment.min(Fp64{}, Fp64{}, {.propagate_nan = true})),
               std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(environment.max(Fp64{}, Fp64{}, {.absolute = true})),
      std::invalid_argument);
}

TEST(MinMax, F32ThreeInputFoldsInOperandOrder) {
  const Environment environment;
  EXPECT_EQ(
      environment.min(Fp32{0x7FC00123u}, Fp32{0x40400000u}, Fp32{0x40000000u})
          .value.bits,
      0x40000000u);
  EXPECT_EQ(environment
                .max(Fp32{0x7FC00123u}, Fp32{0x40400000u}, Fp32{0x40000000u},
                     {.propagate_nan = true})
                .value.bits,
            0x7FC00000u);
  EXPECT_EQ(environment
                .min(Fp32{0xC0400000u}, Fp32{0xC0000000u}, Fp32{0x3F800000u},
                     {.absolute = true})
                .value.bits,
            0x3F800000u);
  EXPECT_THROW(static_cast<void>(
                   environment.min(Fp32{}, Fp32{}, Fp32{}, {.xor_sign = true})),
               std::invalid_argument);
}

TEST(UnaryFloatInstructions, AbsNegCopysignAndTestp) {
  const Environment environment;
  EXPECT_EQ(environment.abs(Fp16{0xBC00u}).value.bits, 0x3C00u);
  EXPECT_EQ(environment.neg(Bf16{0x3F80u}).value.bits, 0xBF80u);
  EXPECT_EQ(
      environment.copysign(Fp32{0xBF800000u}, Fp32{0x40000000u}).value.bits,
      0xC0000000u);
  EXPECT_EQ(
      environment
          .copysign(Fp64{0x3FF0000000000000ULL}, Fp64{0xC000000000000000ULL})
          .value.bits,
      0x4000000000000000ULL);
  EXPECT_TRUE(environment.testp(Fp32{}, TestpOp::Normal).value);
  EXPECT_TRUE(
      environment.testp(Fp64{0x0000000000000001ULL}, TestpOp::Subnormal).value);
  EXPECT_FALSE(environment.testp(Fp32{0x7F800000u}, TestpOp::Finite).value);
}

}  // namespace ptxsim::fp::test
