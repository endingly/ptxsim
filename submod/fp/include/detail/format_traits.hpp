#pragma once

#include <ptxsim/fp/types.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace ptxsim::fp {

template <typename T>
struct FormatTraits;

#define PTXSIM_IEEE_TRAITS(Type, BitsType, Total, Exp, Frac, Bias, Sign, \
                           ExpMask, FracMask, Quiet)                     \
  template <>                                                            \
  struct FormatTraits<Type> {                                            \
    using Bits = BitsType;                                               \
    static constexpr unsigned total_bits = Total;                        \
    static constexpr unsigned exponent_bits = Exp;                       \
    static constexpr unsigned fraction_bits = Frac;                      \
    static constexpr int exponent_bias = Bias;                           \
    static constexpr Bits sign_mask = Sign;                              \
    static constexpr Bits exponent_mask = ExpMask;                       \
    static constexpr Bits fraction_mask = FracMask;                      \
    static constexpr Bits quiet_nan_bit = Quiet;                         \
    static constexpr Bits storage_mask = static_cast<Bits>(~Bits{0});    \
    static constexpr bool has_subnormal = true;                          \
    static constexpr bool has_infinity = true;                           \
    static constexpr bool has_quiet_nan = true;                          \
    static constexpr bool has_signaling_nan = true;                      \
    static constexpr bool finite_only = false;                           \
    static constexpr unsigned all_exponent_field = (1u << Exp) - 1;      \
    static constexpr unsigned maximum_finite_exponent_field =            \
        all_exponent_field - 1;                                          \
    static constexpr std::uint64_t maximum_finite_fraction_field =       \
        (std::uint64_t{1} << Frac) - 1;                                  \
    static constexpr unsigned canonical_nan_exponent_field =             \
        all_exponent_field;                                               \
    static constexpr std::uint64_t canonical_nan_fraction_field =        \
        std::uint64_t{1} << (Frac - 1);                                  \
    static constexpr bool preserves_nan_payload = true;                  \
    static constexpr bool is_nan_fields(unsigned exponent,               \
                                        std::uint64_t fraction) {         \
      return exponent == all_exponent_field && fraction != 0;            \
    }                                                                     \
    static constexpr bool is_infinity_fields(unsigned exponent,          \
                                             std::uint64_t fraction) {    \
      return exponent == all_exponent_field && fraction == 0;            \
    }                                                                     \
    static constexpr bool is_quiet_nan_fraction(std::uint64_t fraction) {\
      return (fraction & (std::uint64_t{1} << (Frac - 1))) != 0;          \
    }                                                                     \
  }

PTXSIM_IEEE_TRAITS(Fp16, std::uint16_t, 16, 5, 10, 15, 0x8000u, 0x7C00u,
                   0x03FFu, 0x0200u);
PTXSIM_IEEE_TRAITS(Bf16, std::uint16_t, 16, 8, 7, 127, 0x8000u, 0x7F80u,
                   0x007Fu, 0x0040u);
PTXSIM_IEEE_TRAITS(Fp32, std::uint32_t, 32, 8, 23, 127, 0x80000000u,
                   0x7F800000u, 0x007FFFFFu, 0x00400000u);
PTXSIM_IEEE_TRAITS(Fp64, std::uint64_t, 64, 11, 52, 1023, 0x8000000000000000ULL,
                   0x7FF0000000000000ULL, 0x000FFFFFFFFFFFFFULL,
                   0x0008000000000000ULL);
PTXSIM_IEEE_TRAITS(Fp8E5M2, std::uint8_t, 8, 5, 2, 15, 0x80u, 0x7Cu, 0x03u,
                   0x02u);
#undef PTXSIM_IEEE_TRAITS

template <>
struct FormatTraits<Tf32> {
  using Bits = std::uint32_t;
  static constexpr unsigned total_bits = 32, exponent_bits = 8,
                            fraction_bits = 10;
  static constexpr int exponent_bias = 127;
  static constexpr Bits sign_mask = 0x80000000u, exponent_mask = 0x7F800000u,
                        fraction_mask = 0x007FE000u,
                        quiet_nan_bit = 0x00400000u, storage_mask = 0xFFFFE000u;
  static constexpr bool has_subnormal = true, has_infinity = true,
                        has_quiet_nan = true, has_signaling_nan = true,
                        finite_only = false;
  static constexpr unsigned all_exponent_field = 0xFFu;
  static constexpr unsigned maximum_finite_exponent_field = 0xFEu;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x3FFu;
  static constexpr unsigned canonical_nan_exponent_field = 0xFFu;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0x200u;
  static constexpr bool preserves_nan_payload = true;
  static constexpr bool is_nan_fields(unsigned exponent,
                                      std::uint64_t fraction) {
    return exponent == all_exponent_field && fraction != 0;
  }
  static constexpr bool is_infinity_fields(unsigned exponent,
                                           std::uint64_t fraction) {
    return exponent == all_exponent_field && fraction == 0;
  }
  static constexpr bool is_quiet_nan_fraction(std::uint64_t fraction) {
    return (fraction & 0x200u) != 0;
  }
};

template <>
struct FormatTraits<Fp8E4M3> {
  using Bits = std::uint8_t;
  static constexpr unsigned total_bits = 8, exponent_bits = 4,
                            fraction_bits = 3;
  static constexpr int exponent_bias = 7;
  static constexpr Bits sign_mask = 0x80u, exponent_mask = 0x78u,
                        fraction_mask = 0x07u, quiet_nan_bit = 0x04u,
                        storage_mask = 0xFFu;
  static constexpr bool has_subnormal = true, has_infinity = false,
                        has_quiet_nan = true, has_signaling_nan = false,
                        finite_only = false;
  static constexpr unsigned all_exponent_field = 0xFu;
  static constexpr unsigned maximum_finite_exponent_field = 0xFu;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x6u;
  static constexpr unsigned canonical_nan_exponent_field = 0xFu;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0x7u;
  static constexpr bool preserves_nan_payload = false;
  static constexpr bool is_nan_fields(unsigned exponent,
                                      std::uint64_t fraction) {
    return exponent == canonical_nan_exponent_field &&
           fraction == canonical_nan_fraction_field;
  }
  static constexpr bool is_infinity_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_quiet_nan_fraction(std::uint64_t) { return true; }
};

template <>
struct FormatTraits<Fp4E2M1> {
  using Bits = std::uint8_t;
  static constexpr unsigned total_bits = 4, exponent_bits = 2,
                            fraction_bits = 1;
  static constexpr int exponent_bias = 1;
  static constexpr Bits sign_mask = 0x08u, exponent_mask = 0x06u,
                        fraction_mask = 0x01u, quiet_nan_bit = 0,
                        storage_mask = 0x0Fu;
  static constexpr bool has_subnormal = true, has_infinity = false,
                        has_quiet_nan = false, has_signaling_nan = false,
                        finite_only = true;
  static constexpr unsigned all_exponent_field = 0x3u;
  static constexpr unsigned maximum_finite_exponent_field = 0x3u;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x1u;
  static constexpr unsigned canonical_nan_exponent_field = 0;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0;
  static constexpr bool preserves_nan_payload = false;
  static constexpr bool is_nan_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_infinity_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_quiet_nan_fraction(std::uint64_t) { return false; }
};

template <typename T>
concept FloatingFormat = requires(T value) {
  typename FormatTraits<T>::Bits;
  value.bits;
};

template <FloatingFormat T>
[[nodiscard]] constexpr bool is_valid_encoding(T value) noexcept {
  using Traits = FormatTraits<T>;
  return (value.bits &
          static_cast<typename Traits::Bits>(~Traits::storage_mask)) == 0;
}

template <FloatingFormat T>
[[nodiscard]] constexpr T normalize_encoding(T value) noexcept {
  return T{static_cast<typename FormatTraits<T>::Bits>(
      value.bits & FormatTraits<T>::storage_mask)};
}

template <FloatingFormat T>
[[nodiscard]] constexpr FpClass classify(T raw) noexcept {
  using Traits = FormatTraits<T>;
  const auto value = normalize_encoding(raw);
  constexpr unsigned fraction_lsb = std::countr_zero(Traits::fraction_mask);
  const auto exponent = static_cast<unsigned>(
      (value.bits & Traits::exponent_mask) >>
      (Traits::fraction_bits + fraction_lsb));
  const auto fraction = static_cast<std::uint64_t>(
      (value.bits & Traits::fraction_mask) >> fraction_lsb);
  if (exponent == 0)
    return fraction == 0 ? FpClass::Zero : FpClass::Subnormal;
  if (Traits::is_nan_fields(exponent, fraction))
    return Traits::is_quiet_nan_fraction(fraction) ? FpClass::QuietNaN
                                                    : FpClass::SignalingNaN;
  if (Traits::is_infinity_fields(exponent, fraction))
    return FpClass::Infinity;
  return FpClass::Normal;
}

template <FloatingFormat T>
[[nodiscard]] constexpr bool is_negative(T value) noexcept {
  return (normalize_encoding(value).bits & FormatTraits<T>::sign_mask) != 0;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_zero(T value) noexcept {
  return classify(value) == FpClass::Zero;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_negative_zero(T value) noexcept {
  return is_zero(value) && is_negative(value);
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_subnormal(T value) noexcept {
  return classify(value) == FpClass::Subnormal;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_nan(T value) noexcept {
  const auto c = classify(value);
  return c == FpClass::QuietNaN || c == FpClass::SignalingNaN;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_quiet_nan(T value) noexcept {
  return classify(value) == FpClass::QuietNaN;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_signaling_nan(T value) noexcept {
  return classify(value) == FpClass::SignalingNaN;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_infinity(T value) noexcept {
  return classify(value) == FpClass::Infinity;
}

template <FloatingFormat T>
[[nodiscard]] constexpr T flush_subnormal(T value) noexcept {
  return is_subnormal(value)
             ? T{static_cast<typename FormatTraits<T>::Bits>(
                   normalize_encoding(value).bits & FormatTraits<T>::sign_mask)}
             : normalize_encoding(value);
}

static_assert(!std::same_as<Fp8E4M3, Fp8E5M2>);
static_assert(std::is_trivially_copyable_v<Bf16> &&
              std::is_standard_layout_v<Bf16>);
static_assert(std::is_trivially_copyable_v<Tf32> &&
              std::is_standard_layout_v<Tf32>);

}  // namespace ptxsim::fp
