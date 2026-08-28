#pragma once

#include "nan_policy.hpp"
#include "operation_policy.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ptxsim::fp::detail {
namespace comparison {

template <FloatingFormat T>
[[nodiscard]] T input(T value, ArithmeticControl control) noexcept {
  return resolve_subnormal(control) == SubnormalMode::FlushToSignedZero
             ? flush_subnormal(value)
             : normalize_encoding(value);
}

template <FloatingFormat T>
[[nodiscard]] T output(T value, ArithmeticControl control) noexcept {
  return resolve_subnormal(control) == SubnormalMode::FlushToSignedZero
             ? flush_subnormal(value)
             : value;
}

template <FloatingFormat T>
[[nodiscard]] constexpr T absolute(T value) noexcept {
  using Bits = typename FormatTraits<T>::Bits;
  return T{static_cast<Bits>(normalize_encoding(value).bits &
                             static_cast<Bits>(~FormatTraits<T>::sign_mask))};
}

template <FloatingFormat T>
[[nodiscard]] constexpr T negate(T value) noexcept {
  using Bits = typename FormatTraits<T>::Bits;
  return T{static_cast<Bits>(normalize_encoding(value).bits ^
                             FormatTraits<T>::sign_mask)};
}

template <FloatingFormat T>
[[nodiscard]] int numeric_compare(T lhs, T rhs) noexcept {
  lhs = normalize_encoding(lhs);
  rhs = normalize_encoding(rhs);
  if (is_zero(lhs) && is_zero(rhs))
    return 0;
  const bool lhs_negative = is_negative(lhs);
  const bool rhs_negative = is_negative(rhs);
  if (lhs_negative != rhs_negative)
    return lhs_negative ? -1 : 1;
  const auto left = static_cast<std::uint64_t>(lhs.bits);
  const auto right = static_cast<std::uint64_t>(rhs.bits);
  if (left == right)
    return 0;
  const bool less = left < right;
  return lhs_negative ? (less ? 1 : -1) : (less ? -1 : 1);
}

template <FloatingFormat T>
[[nodiscard]] Result<T> abs(T value, ArithmeticControl control) {
  validate_control<Operation::Abs, T>(control);
  return {output(absolute(input(value, control)), control), {}};
}

template <FloatingFormat T>
[[nodiscard]] Result<T> neg(T value, ArithmeticControl control) {
  validate_control<Operation::Neg, T>(control);
  return {output(negate(input(value, control)), control), {}};
}

template <FloatingFormat T>
void validate_minmax_modifiers(MinMaxControl minmax_control,
                               bool three_input = false) {
  if constexpr (std::is_same_v<T, Fp64>) {
    if (minmax_control.propagate_nan || minmax_control.absolute ||
        minmax_control.xor_sign) {
      throw std::invalid_argument("f64 min/max does not support modifiers");
    }
  }
  if (three_input) {
    if (minmax_control.xor_sign)
      throw std::invalid_argument(
          "three-input f32 min/max does not support xor_sign");
  } else if (minmax_control.absolute != minmax_control.xor_sign) {
    throw std::invalid_argument(
        "two-input min/max requires .abs and .xorsign together");
  }
}

template <FloatingFormat T>
[[nodiscard]] Result<T> minmax_impl(T lhs, T rhs, MinMaxControl minmax_control,
                                    ArithmeticControl control,
                                    bool choose_min) {
  lhs = input(lhs, control);
  rhs = input(rhs, control);
  const bool result_negative = is_negative(lhs) != is_negative(rhs);
  const T compared_lhs = minmax_control.absolute ? absolute(lhs) : lhs;
  const T compared_rhs = minmax_control.absolute ? absolute(rhs) : rhs;
  const auto apply_xor_sign = [&](T value) {
    if (!minmax_control.xor_sign)
      return value;
    value.bits = static_cast<typename FormatTraits<T>::Bits>(
        (value.bits & static_cast<typename FormatTraits<T>::Bits>(
                          ~FormatTraits<T>::sign_mask)) |
        (result_negative ? FormatTraits<T>::sign_mask : 0));
    return value;
  };

  if (is_nan(lhs) || is_nan(rhs)) {
    ExceptionFlags flags;
    if (is_signaling_nan(lhs) || is_signaling_nan(rhs))
      flags |= ExceptionFlag::Invalid;
    if (minmax_control.propagate_nan)
      return {canonical_nan<T>(), flags};
    if (!is_nan(lhs))
      return {output(apply_xor_sign(compared_lhs), control), flags};
    if (!is_nan(rhs))
      return {output(apply_xor_sign(compared_rhs), control), flags};
    // PTX explicitly ignores .abs/.xorsign when the selected result is NaN.
    auto selected = propagate_nan(lhs, rhs);
    selected.flags |= flags;
    return selected;
  }

  const int relation = numeric_compare(compared_lhs, compared_rhs);
  T result;
  if (relation == 0 && is_zero(compared_lhs) && is_zero(compared_rhs)) {
    result = T{static_cast<typename FormatTraits<T>::Bits>(
        (choose_min ? (is_negative(compared_lhs) || is_negative(compared_rhs))
                    : (is_negative(compared_lhs) && is_negative(compared_rhs)))
            ? FormatTraits<T>::sign_mask
            : 0)};
  } else {
    result = (choose_min ? relation <= 0 : relation >= 0) ? compared_lhs
                                                          : compared_rhs;
  }
  return {output(apply_xor_sign(result), control), {}};
}

template <FloatingFormat T>
[[nodiscard]] Result<T> minmax(T lhs, T rhs, MinMaxControl minmax_control,
                               ArithmeticControl control, bool choose_min) {
  validate_minmax_modifiers<T>(minmax_control);
  if (choose_min)
    validate_control<Operation::Min, T>(control);
  else
    validate_control<Operation::Max, T>(control);
  return minmax_impl(lhs, rhs, minmax_control, control, choose_min);
}

[[nodiscard]] inline Result<Fp32> minmax(Fp32 a, Fp32 b, Fp32 c,
                                         MinMaxControl minmax_control,
                                         ArithmeticControl control,
                                         bool choose_min) {
  validate_minmax_modifiers<Fp32>(minmax_control, true);
  if (choose_min)
    validate_control<Operation::Min, Fp32>(control);
  else
    validate_control<Operation::Max, Fp32>(control);
  auto first = minmax_impl(a, b, minmax_control, control, choose_min);
  auto result =
      minmax_impl(first.value, c, minmax_control, control, choose_min);
  result.flags |= first.flags;
  return result;
}

template <FloatingFormat T>
[[nodiscard]] Result<bool> compare(T lhs, T rhs, CompareOp operation,
                                   ArithmeticControl control) {
  validate_control<Operation::Compare, T>(control);
  lhs = input(lhs, control);
  rhs = input(rhs, control);
  const bool unordered = is_nan(lhs) || is_nan(rhs);
  ExceptionFlags flags;
  if (is_signaling_nan(lhs) || is_signaling_nan(rhs))
    flags |= ExceptionFlag::Invalid;
  if (operation == CompareOp::Number)
    return {!unordered, flags};
  if (operation == CompareOp::NaN)
    return {unordered, flags};
  if (unordered) {
    switch (operation) {
      case CompareOp::EqualUnordered:
      case CompareOp::NotEqualUnordered:
      case CompareOp::LessUnordered:
      case CompareOp::LessEqualUnordered:
      case CompareOp::GreaterUnordered:
      case CompareOp::GreaterEqualUnordered:
        return {true, flags};
      default:
        return {false, flags};
    }
  }
  const int relation = numeric_compare(lhs, rhs);
  switch (operation) {
    case CompareOp::Equal:
    case CompareOp::EqualUnordered:
      return {relation == 0, flags};
    case CompareOp::NotEqual:
    case CompareOp::NotEqualUnordered:
      return {relation != 0, flags};
    case CompareOp::Less:
    case CompareOp::LessUnordered:
      return {relation < 0, flags};
    case CompareOp::LessEqual:
    case CompareOp::LessEqualUnordered:
      return {relation <= 0, flags};
    case CompareOp::Greater:
    case CompareOp::GreaterUnordered:
      return {relation > 0, flags};
    case CompareOp::GreaterEqual:
    case CompareOp::GreaterEqualUnordered:
      return {relation >= 0, flags};
    case CompareOp::Number:
    case CompareOp::NaN:
      std::unreachable();
  }
  std::unreachable();
}

template <typename T>
[[nodiscard]] Result<T> copysign(T sign, T magnitude) {
  validate_control<Operation::Copysign, T>({});
  using Bits = typename FormatTraits<T>::Bits;
  const auto sign_bits =
      normalize_encoding(sign).bits & FormatTraits<T>::sign_mask;
  const auto magnitude_bits = normalize_encoding(magnitude).bits &
                              static_cast<Bits>(~FormatTraits<T>::sign_mask);
  return {T{static_cast<Bits>(sign_bits | magnitude_bits)}, {}};
}

template <typename T>
[[nodiscard]] Result<bool> testp(T value, TestpOp operation) {
  validate_control<Operation::Testp, T>({});
  const auto category = classify(value);
  switch (operation) {
    case TestpOp::Finite:
      return {category == FpClass::Zero || category == FpClass::Subnormal ||
                  category == FpClass::Normal,
              {}};
    case TestpOp::Infinite:
      return {category == FpClass::Infinity, {}};
    case TestpOp::Number:
      return {!is_nan(value), {}};
    case TestpOp::NotANumber:
      return {is_nan(value), {}};
    case TestpOp::Normal:
      return {category == FpClass::Zero || category == FpClass::Normal, {}};
    case TestpOp::Subnormal:
      return {category == FpClass::Subnormal, {}};
  }
  std::unreachable();
}

}  // namespace comparison
}  // namespace ptxsim::fp::detail
