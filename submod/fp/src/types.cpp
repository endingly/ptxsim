#include <bit>
#include <ptxsim/fp/types.hpp>
#include <stdexcept>

extern "C" {
#include <softfloat/softfloat.h>
}

namespace ptxsim::fp {

namespace {

std::uint_fast8_t to_softfloat_rounding_mode(RoundingMode mode) {
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

  throw std::runtime_error("invalid rounding mode");
}

}  // namespace

std::uint32_t add_f32_bits(std::uint32_t lhs_bits, std::uint32_t rhs_bits,
                           RoundingMode rounding) {
  softfloat_roundingMode = to_softfloat_rounding_mode(rounding);

  // 可选：
  // 清空上一条操作留下的 IEEE exception flags。
  softfloat_exceptionFlags = 0;

  float32_t lhs{
      .v = lhs_bits,
  };

  float32_t rhs{
      .v = rhs_bits,
  };

  const float32_t result = f32_add(lhs, rhs);

  return result.v;
}

float add_f32(float lhs, float rhs, RoundingMode rounding) {
  static_assert(sizeof(float) == sizeof(std::uint32_t));

  const auto lhs_bits = std::bit_cast<std::uint32_t>(lhs);

  const auto rhs_bits = std::bit_cast<std::uint32_t>(rhs);

  const auto result_bits = add_f32_bits(lhs_bits, rhs_bits, rounding);

  return std::bit_cast<float>(result_bits);
}

}  // namespace ptxsim::fp