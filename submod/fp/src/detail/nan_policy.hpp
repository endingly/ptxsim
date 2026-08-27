#pragma once

#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

#include <bit>
#include <cstdint>

namespace ptxsim::fp::detail {

template <FloatingFormat T>
[[nodiscard]] constexpr T canonical_nan() noexcept {
  using Traits = FormatTraits<T>;
  using Bits = typename Traits::Bits;
  constexpr unsigned fraction_lsb = std::countr_zero(Traits::fraction_mask);
  return T{static_cast<Bits>(
      (static_cast<Bits>(Traits::canonical_nan_exponent_field)
       << (Traits::fraction_bits + fraction_lsb)) |
      (static_cast<Bits>(Traits::canonical_nan_fraction_field)
       << fraction_lsb))};
}

template <FloatingFormat T>
[[nodiscard]] constexpr T quiet_nan(T value) noexcept {
  return T{static_cast<typename FormatTraits<T>::Bits>(
      normalize_encoding(value).bits | FormatTraits<T>::quiet_nan_bit)};
}

template <FloatingFormat T>
[[nodiscard]] Result<T> propagate_nan(T value) noexcept {
  ExceptionFlags flags;
  if (is_signaling_nan(value))
    flags |= ExceptionFlag::Invalid;
  return {quiet_nan(value), flags};
}

template <FloatingFormat T>
[[nodiscard]] Result<T> propagate_nan(T first, T second) noexcept {
  ExceptionFlags flags;
  if (is_signaling_nan(first) || is_signaling_nan(second))
    flags |= ExceptionFlag::Invalid;
  return {quiet_nan(is_nan(first) ? first : second), flags};
}

template <FloatingFormat T>
[[nodiscard]] Result<T> propagate_nan(T first, T second, T third) noexcept {
  ExceptionFlags flags;
  if (is_signaling_nan(first) || is_signaling_nan(second) ||
      is_signaling_nan(third))
    flags |= ExceptionFlag::Invalid;
  const T selected = is_nan(first) ? first : (is_nan(second) ? second : third);
  return {quiet_nan(selected), flags};
}

template <FloatingFormat T>
[[nodiscard]] Result<T> canonical_invalid_nan() noexcept {
  return {canonical_nan<T>(),
          ExceptionFlags{static_cast<std::uint8_t>(ExceptionFlag::Invalid)}};
}

}  // namespace ptxsim::fp::detail
