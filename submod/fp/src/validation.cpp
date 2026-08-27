#include <ptxsim/fp/validation.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ptxsim::fp::validation {
namespace {

[[nodiscard]] constexpr std::uint32_t ordered(Fp32 value) noexcept {
  return is_negative(value) ? ~value.bits : value.bits | 0x80000000u;
}

[[nodiscard]] constexpr std::uint64_t ordered(Fp64 value) noexcept {
  return is_negative(value) ? ~value.bits : value.bits | 0x8000000000000000ULL;
}

template <typename T>
[[nodiscard]] bool comparable(T lhs, T rhs) noexcept {
  return !is_nan(lhs) && !is_nan(rhs);
}

template <typename T>
[[nodiscard]] bool finite(T value) noexcept {
  const auto category = classify(value);
  return category == FpClass::Zero || category == FpClass::Subnormal ||
         category == FpClass::Normal;
}

}  // namespace

bool bit_exact(Fp32 expected, Fp32 actual) noexcept {
  return expected == actual;
}

bool bit_exact(Fp64 expected, Fp64 actual) noexcept {
  return expected == actual;
}

bool same_float_class(Fp32 expected, Fp32 actual) noexcept {
  return classify(expected) == classify(actual);
}

bool same_float_class(Fp64 expected, Fp64 actual) noexcept {
  return classify(expected) == classify(actual);
}

std::uint32_t ulp_distance(Fp32 lhs, Fp32 rhs) noexcept {
  if (!comparable(lhs, rhs))
    return std::numeric_limits<std::uint32_t>::max();
  if (is_zero(lhs) && is_zero(rhs))
    return 0;
  const auto lhs_ordered = ordered(lhs);
  const auto rhs_ordered = ordered(rhs);
  return lhs_ordered > rhs_ordered ? lhs_ordered - rhs_ordered
                                   : rhs_ordered - lhs_ordered;
}

std::uint64_t ulp_distance(Fp64 lhs, Fp64 rhs) noexcept {
  if (!comparable(lhs, rhs))
    return std::numeric_limits<std::uint64_t>::max();
  if (is_zero(lhs) && is_zero(rhs))
    return 0;
  const auto lhs_ordered = ordered(lhs);
  const auto rhs_ordered = ordered(rhs);
  return lhs_ordered > rhs_ordered ? lhs_ordered - rhs_ordered
                                   : rhs_ordered - lhs_ordered;
}

bool within_ulp(Fp32 expected, Fp32 actual, std::uint32_t maximum) noexcept {
  return comparable(expected, actual) &&
         ulp_distance(expected, actual) <= maximum;
}

bool within_ulp(Fp64 expected, Fp64 actual, std::uint64_t maximum) noexcept {
  return comparable(expected, actual) &&
         ulp_distance(expected, actual) <= maximum;
}

bool within_relative(Fp32 expected, Fp32 actual, float maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  const float lhs = std::bit_cast<float>(expected.bits);
  const float rhs = std::bit_cast<float>(actual.bits);
  return std::abs(lhs - rhs) <=
         maximum * std::max(std::abs(lhs), std::abs(rhs));
}

bool within_relative(Fp64 expected, Fp64 actual, double maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  const double lhs = std::bit_cast<double>(expected.bits);
  const double rhs = std::bit_cast<double>(actual.bits);
  return std::abs(lhs - rhs) <=
         maximum * std::max(std::abs(lhs), std::abs(rhs));
}

bool within_absolute(Fp32 expected, Fp32 actual, float maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  return std::abs(std::bit_cast<float>(expected.bits) -
                  std::bit_cast<float>(actual.bits)) <= maximum;
}

bool within_absolute(Fp64 expected, Fp64 actual, double maximum) noexcept {
  if (!comparable(expected, actual))
    return false;
  if (expected == actual)
    return true;
  if (maximum < 0 || !finite(expected) || !finite(actual))
    return false;
  return std::abs(std::bit_cast<double>(expected.bits) -
                  std::bit_cast<double>(actual.bits)) <= maximum;
}

}  // namespace ptxsim::fp::validation
