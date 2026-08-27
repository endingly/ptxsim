#include <ptxsim/fp/types.hpp>

#include <bit>
#include <cstdint>

#include <gtest/gtest.h>

namespace ptxsim::fp::test {

TEST(SoftFloatDemo, AddF32NearestEven) {
  constexpr float lhs = 1.0F;
  constexpr float rhs = 0.5F;

  const auto result = add_f32(lhs, rhs, RoundingMode::NearestEven);

  EXPECT_EQ(std::bit_cast<std::uint32_t>(result),
            std::bit_cast<std::uint32_t>(1.5F));
}

TEST(SoftFloatDemo, AddF32BitsNearestEven) {
  constexpr std::uint32_t lhs_bits = 0x3F800000u;  // 1.0
  constexpr std::uint32_t rhs_bits = 0x3F000000u;  // 0.5

  const auto result_bits =
      add_f32_bits(lhs_bits, rhs_bits, RoundingMode::NearestEven);

  EXPECT_EQ(result_bits,
            0x3FC00000u);  // 1.5
}

TEST(SoftFloatDemo, RoundingModeChangesResult) {
  //
  // 1.0f + 2^-24
  //
  // 2^-24 is exactly half of one ULP at 1.0f.
  //
  // nearest-even:
  //   1.0f
  //
  // toward +infinity:
  //   nextafter(1.0f, +inf)
  //

  constexpr std::uint32_t one_bits = 0x3F800000u;

  constexpr std::uint32_t half_ulp_bits = 0x33800000u;  // 2^-24

  const auto nearest =
      add_f32_bits(one_bits, half_ulp_bits, RoundingMode::NearestEven);

  const auto upward =
      add_f32_bits(one_bits, half_ulp_bits, RoundingMode::TowardPositive);

  EXPECT_EQ(nearest, 0x3F800000u);

  EXPECT_EQ(upward, 0x3F800001u);
}

TEST(SoftFloatDemo, TowardZeroRoundsDownPositiveResult) {
  constexpr std::uint32_t one_bits = 0x3F800000u;

  constexpr std::uint32_t half_ulp_bits = 0x33800000u;

  const auto result =
      add_f32_bits(one_bits, half_ulp_bits, RoundingMode::TowardZero);

  EXPECT_EQ(result, 0x3F800000u);
}

TEST(SoftFloatDemo, TowardNegativeRoundsNegativeResultDown) {
  //
  // -1.0 + (-2^-24)
  //
  // nearest-even -> -1.0
  // toward -inf  -> next representable value below -1.0
  //

  constexpr std::uint32_t minus_one_bits = 0xBF800000u;

  constexpr std::uint32_t minus_half_ulp_bits = 0xB3800000u;

  const auto nearest = add_f32_bits(minus_one_bits, minus_half_ulp_bits,
                                    RoundingMode::NearestEven);

  const auto downward = add_f32_bits(minus_one_bits, minus_half_ulp_bits,
                                     RoundingMode::TowardNegative);

  EXPECT_EQ(nearest, 0xBF800000u);

  EXPECT_EQ(downward, 0xBF800001u);
}

}  // namespace ptxsim::fp::test