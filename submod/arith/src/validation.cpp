#include <ptxsim/arith/validation.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ptxsim::arith::validation {
namespace {

template <typename T>
[[nodiscard]] bool comparable(T lhs, T rhs) noexcept {
  return !is_nan(lhs) && !is_nan(rhs);
}

template <typename T>
[[nodiscard]] bool finite(T value) noexcept {
  const auto category = classify(value);
  return category == fp_class::zero || category == fp_class::subnormal ||
         category == fp_class::normal;
}

}  // namespace

bool within_relative(float32_t expected, float32_t actual,
                     float maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  const float lhs = std::bit_cast<float>(expected.bits());
  const float rhs = std::bit_cast<float>(actual.bits());
  return std::abs(lhs - rhs) <=
         maximum * std::max(std::abs(lhs), std::abs(rhs));
}

bool within_relative(float64_t expected, float64_t actual,
                     double maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  const double lhs = std::bit_cast<double>(expected.bits());
  const double rhs = std::bit_cast<double>(actual.bits());
  return std::abs(lhs - rhs) <=
         maximum * std::max(std::abs(lhs), std::abs(rhs));
}

bool within_absolute(float32_t expected, float32_t actual,
                     float maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  return std::abs(std::bit_cast<float>(expected.bits()) -
                  std::bit_cast<float>(actual.bits())) <= maximum;
}

bool within_absolute(float64_t expected, float64_t actual,
                     double maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  return std::abs(std::bit_cast<double>(expected.bits()) -
                  std::bit_cast<double>(actual.bits())) <= maximum;
}

}  // namespace ptxsim::arith::validation
