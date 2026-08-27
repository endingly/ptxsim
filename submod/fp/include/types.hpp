#pragma once

#include <cstdint>

namespace ptxsim::fp {

enum class RoundingMode {
  NearestEven,
  TowardZero,
  TowardNegative,
  TowardPositive,
};

std::uint32_t add_f32_bits(std::uint32_t lhs_bits, std::uint32_t rhs_bits,
                           RoundingMode rounding = RoundingMode::NearestEven);

float add_f32(float lhs, float rhs,
              RoundingMode rounding = RoundingMode::NearestEven);

}  // namespace fp