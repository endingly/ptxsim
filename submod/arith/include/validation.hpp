#pragma once

#include <ptxsim/arith/detail/format_traits.hpp>

#include <bit>
#include <cstdint>
#include <limits>

namespace ptxsim::arith::validation {

template <FloatingFormat T>
[[nodiscard]] constexpr bool bit_exact(T expected, T actual) noexcept {
  return expected == actual;
}

template <FloatingFormat T>
[[nodiscard]] constexpr bool same_float_class(T expected, T actual) noexcept {
  return classify(expected) == classify(actual);
}

template <FloatingFormat T>
[[nodiscard]] constexpr typename FormatTraits<T>::Bits ordered_bits(
    T raw) noexcept {
  using Traits = FormatTraits<T>;
  using Bits = typename Traits::Bits;
  constexpr unsigned padding =
      Traits::fraction_mask == 0 ? 0 : std::countr_zero(Traits::fraction_mask);
  constexpr Bits sign = static_cast<Bits>(Traits::sign_mask >> padding);
  constexpr Bits mask = static_cast<Bits>(Traits::storage_mask >> padding);
  const Bits bits =
      static_cast<Bits>(normalize_encoding(raw).bits() >> padding);
  return (bits & sign) != 0 ? static_cast<Bits>((~bits) & mask)
                            : static_cast<Bits>(bits | sign);
}

template <FloatingFormat T>
[[nodiscard]] constexpr typename FormatTraits<T>::Bits ulp_distance(
    T lhs, T rhs) noexcept {
  using Bits = typename FormatTraits<T>::Bits;
  if (is_nan(lhs) || is_nan(rhs))
    return std::numeric_limits<Bits>::max();
  if (is_zero(lhs) && is_zero(rhs))
    return 0;
  const Bits a = ordered_bits(lhs);
  const Bits b = ordered_bits(rhs);
  return a > b ? static_cast<Bits>(a - b) : static_cast<Bits>(b - a);
}

template <FloatingFormat T>
[[nodiscard]] constexpr bool within_ulp(
    T expected, T actual, typename FormatTraits<T>::Bits maximum) noexcept {
  return !is_nan(expected) && !is_nan(actual) &&
         ulp_distance(expected, actual) <= maximum;
}

// Validation-only host floating-point comparisons; never use for execution.
[[nodiscard]] bool within_relative(float32_t expected, float32_t actual,
                                   float maximum) noexcept;
[[nodiscard]] bool within_relative(float64_t expected, float64_t actual,
                                   double maximum) noexcept;
[[nodiscard]] bool within_absolute(float32_t expected, float32_t actual,
                                   float maximum) noexcept;
[[nodiscard]] bool within_absolute(float64_t expected, float64_t actual,
                                   double maximum) noexcept;

}  // namespace ptxsim::arith::validation
