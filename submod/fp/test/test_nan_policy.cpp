#include <ptxsim/fp/environment.hpp>

#include <gtest/gtest.h>

namespace ptxsim::fp::test {

TEST(NaNPolicy, F32PayloadPriorityQuietingAndInvalid) {
  const Environment environment;
  constexpr Fp32 qnan_a{0xFFC00123u};
  constexpr Fp32 snan_b{0x7F800456u};
  constexpr Fp32 one{0x3F800000u};

  const auto first_wins = environment.add(qnan_a, snan_b);
  EXPECT_EQ(first_wins.value.bits, qnan_a.bits);
  EXPECT_TRUE(first_wins.flags.contains(ExceptionFlag::Invalid));

  const auto signaling_is_quieted = environment.mul(snan_b, one);
  EXPECT_EQ(signaling_is_quieted.value.bits, 0x7FC00456u);
  EXPECT_TRUE(signaling_is_quieted.flags.contains(ExceptionFlag::Invalid));

  EXPECT_EQ(environment.div(one, qnan_a).value.bits, qnan_a.bits);
  EXPECT_EQ(environment.sqrt(qnan_a).value.bits, qnan_a.bits);
  EXPECT_EQ(environment.fma(qnan_a, one, Fp32{0x7FC00789u}).value.bits,
            qnan_a.bits);

  const auto invalid_product_with_nan = environment.fma(
      Fp32{0x7F800000u}, Fp32{}, Fp32{0xFFC00789u});
  EXPECT_EQ(invalid_product_with_nan.value.bits, 0x7FC00000u);
  EXPECT_TRUE(invalid_product_with_nan.flags.contains(ExceptionFlag::Invalid));
}

TEST(NaNPolicy, F64PayloadPriorityQuietingAndInvalid) {
  const Environment environment;
  constexpr Fp64 qnan_a{0xFFF8000000000123ULL};
  constexpr Fp64 snan_b{0x7FF0000000000456ULL};
  constexpr Fp64 one{0x3FF0000000000000ULL};
  const auto result = environment.sub(qnan_a, snan_b);
  EXPECT_EQ(result.value.bits, qnan_a.bits);
  EXPECT_TRUE(result.flags.contains(ExceptionFlag::Invalid));
  const auto quieted = environment.sqrt(snan_b);
  EXPECT_EQ(quieted.value.bits, 0x7FF8000000000456ULL);
  EXPECT_TRUE(quieted.flags.contains(ExceptionFlag::Invalid));
  EXPECT_EQ(environment.fma(one, one, qnan_a).value.bits, qnan_a.bits);
}

TEST(NaNPolicy, Bf16PayloadPriorityQuietingAndInvalid) {
  const Environment environment;
  constexpr Bf16 qnan_a{0xFFC1u};
  constexpr Bf16 snan_b{0x7F82u};
  const auto add = environment.add(qnan_a, snan_b);
  EXPECT_EQ(add.value.bits, qnan_a.bits);
  EXPECT_TRUE(add.flags.contains(ExceptionFlag::Invalid));
  const auto sub = environment.sub(snan_b, Bf16{0x3F80u});
  EXPECT_EQ(sub.value.bits, 0x7FC2u);
  EXPECT_TRUE(sub.flags.contains(ExceptionFlag::Invalid));
  EXPECT_EQ(environment.mul(Bf16{0x3F80u}, qnan_a).value.bits, qnan_a.bits);
}

}  // namespace ptxsim::fp::test
