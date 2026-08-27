#pragma once

#include <cstdint>

namespace ptxsim::fp {

enum class RoundingMode {
  NearestEven,
  TowardZero,
  TowardNegative,
  TowardPositive,
};

struct Fp16 {
  std::uint16_t bits{};
  friend constexpr bool operator==(Fp16, Fp16) = default;
};

struct Fp32 {
  std::uint32_t bits{};
  friend constexpr bool operator==(Fp32, Fp32) = default;
};

struct Fp64 {
  std::uint64_t bits{};
  friend constexpr bool operator==(Fp64, Fp64) = default;
};

struct ArithmeticControl {
  RoundingMode rounding = RoundingMode::NearestEven;
  bool flush_subnormal = false;
};

enum class FpClass {
  Zero,
  Subnormal,
  Normal,
  Infinity,
  QuietNaN,
  SignalingNaN,
};

[[nodiscard]] constexpr FpClass classify(Fp16 value) noexcept {
  constexpr std::uint16_t exponent_mask = 0x7C00u;
  constexpr std::uint16_t fraction_mask = 0x03FFu;
  constexpr std::uint16_t quiet_nan_bit = 0x0200u;
  const auto exponent = value.bits & exponent_mask;
  const auto fraction = value.bits & fraction_mask;
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  if (exponent != exponent_mask)
    return FpClass::Normal;
  if (fraction == 0)
    return FpClass::Infinity;
  return (fraction & quiet_nan_bit) != 0 ? FpClass::QuietNaN
                                         : FpClass::SignalingNaN;
}

[[nodiscard]] constexpr FpClass classify(Fp32 value) noexcept {
  constexpr std::uint32_t exponent_mask = 0x7F800000u;
  constexpr std::uint32_t fraction_mask = 0x007FFFFFu;
  constexpr std::uint32_t quiet_nan_bit = 0x00400000u;
  const auto exponent = value.bits & exponent_mask;
  const auto fraction = value.bits & fraction_mask;
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  if (exponent != exponent_mask)
    return FpClass::Normal;
  if (fraction == 0)
    return FpClass::Infinity;
  return (fraction & quiet_nan_bit) != 0 ? FpClass::QuietNaN
                                         : FpClass::SignalingNaN;
}

[[nodiscard]] constexpr FpClass classify(Fp64 value) noexcept {
  constexpr std::uint64_t exponent_mask = 0x7FF0000000000000ULL;
  constexpr std::uint64_t fraction_mask = 0x000FFFFFFFFFFFFFULL;
  constexpr std::uint64_t quiet_nan_bit = 0x0008000000000000ULL;
  const auto exponent = value.bits & exponent_mask;
  const auto fraction = value.bits & fraction_mask;
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  if (exponent != exponent_mask)
    return FpClass::Normal;
  if (fraction == 0)
    return FpClass::Infinity;
  return (fraction & quiet_nan_bit) != 0 ? FpClass::QuietNaN
                                         : FpClass::SignalingNaN;
}

[[nodiscard]] constexpr bool is_negative(Fp16 value) noexcept {
  return (value.bits & 0x8000u) != 0;
}

[[nodiscard]] constexpr bool is_negative(Fp32 value) noexcept {
  return (value.bits & 0x80000000u) != 0;
}

[[nodiscard]] constexpr bool is_negative(Fp64 value) noexcept {
  return (value.bits & 0x8000000000000000ULL) != 0;
}

[[nodiscard]] constexpr bool is_zero(Fp16 value) noexcept {
  return classify(value) == FpClass::Zero;
}

[[nodiscard]] constexpr bool is_zero(Fp32 value) noexcept {
  return classify(value) == FpClass::Zero;
}

[[nodiscard]] constexpr bool is_zero(Fp64 value) noexcept {
  return classify(value) == FpClass::Zero;
}

[[nodiscard]] constexpr bool is_negative_zero(Fp16 value) noexcept {
  return is_zero(value) && is_negative(value);
}

[[nodiscard]] constexpr bool is_negative_zero(Fp32 value) noexcept {
  return is_zero(value) && is_negative(value);
}

[[nodiscard]] constexpr bool is_negative_zero(Fp64 value) noexcept {
  return is_zero(value) && is_negative(value);
}

[[nodiscard]] constexpr bool is_subnormal(Fp16 value) noexcept {
  return classify(value) == FpClass::Subnormal;
}

[[nodiscard]] constexpr bool is_subnormal(Fp32 value) noexcept {
  return classify(value) == FpClass::Subnormal;
}

[[nodiscard]] constexpr bool is_subnormal(Fp64 value) noexcept {
  return classify(value) == FpClass::Subnormal;
}

[[nodiscard]] constexpr bool is_nan(Fp16 value) noexcept {
  const auto category = classify(value);
  return category == FpClass::QuietNaN || category == FpClass::SignalingNaN;
}

[[nodiscard]] constexpr bool is_nan(Fp32 value) noexcept {
  const auto category = classify(value);
  return category == FpClass::QuietNaN || category == FpClass::SignalingNaN;
}

[[nodiscard]] constexpr bool is_nan(Fp64 value) noexcept {
  const auto category = classify(value);
  return category == FpClass::QuietNaN || category == FpClass::SignalingNaN;
}

[[nodiscard]] constexpr Fp32 flush_subnormal(Fp32 value) noexcept {
  return is_subnormal(value) ? Fp32{value.bits & 0x80000000u} : value;
}

}  // namespace ptxsim::fp
