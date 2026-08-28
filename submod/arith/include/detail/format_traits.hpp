#pragma once

#include <ptxsim/arith/types.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace ptxsim::arith {

template <typename T>
struct FormatTraits;
template <typename Format>
using format_traits = FormatTraits<basic_float<Format>>;

struct format_info {
  unsigned storage_bits;
  unsigned value_bits;
  unsigned exponent_bits;
  unsigned fraction_bits;
  int exponent_bias;
  bool has_sign;
  bool has_zero;
  bool has_subnormal;
  bool has_infinity;
  bool has_nan;
  bool has_signaling_nan;
  bool fixed_encoding;
};

#define PTXSIM_IEEE_TRAITS(Type, BitsType, Total, Exp, Frac, Bias, Sign,  \
                           ExpMask, FracMask, Quiet)                      \
  template <>                                                             \
  struct FormatTraits<Type> {                                             \
    using Bits = BitsType;                                                \
    static constexpr unsigned total_bits = Total;                         \
    static constexpr unsigned exponent_bits = Exp;                        \
    static constexpr unsigned fraction_bits = Frac;                       \
    static constexpr int exponent_bias = Bias;                            \
    static constexpr Bits sign_mask = Sign;                               \
    static constexpr Bits exponent_mask = ExpMask;                        \
    static constexpr Bits fraction_mask = FracMask;                       \
    static constexpr Bits quiet_nan_bit = Quiet;                          \
    static constexpr Bits storage_mask = static_cast<Bits>(~Bits{0});     \
    static constexpr bool has_subnormal = true;                           \
    static constexpr bool has_zero = true;                                \
    static constexpr bool has_infinity = true;                            \
    static constexpr bool has_quiet_nan = true;                           \
    static constexpr bool has_signaling_nan = true;                       \
    static constexpr bool finite_only = false;                            \
    static constexpr bool fixed_encoding = true;                          \
    static constexpr unsigned all_exponent_field = (1u << Exp) - 1;       \
    static constexpr unsigned maximum_finite_exponent_field =             \
        all_exponent_field - 1;                                           \
    static constexpr std::uint64_t maximum_finite_fraction_field =        \
        (std::uint64_t{1} << Frac) - 1;                                   \
    static constexpr unsigned canonical_nan_exponent_field =              \
        all_exponent_field;                                               \
    static constexpr std::uint64_t canonical_nan_fraction_field =         \
        std::uint64_t{1} << (Frac - 1);                                   \
    static constexpr bool preserves_nan_payload = true;                   \
    static constexpr bool is_zero_fields(unsigned exponent,               \
                                         std::uint64_t fraction) {         \
      return exponent == 0 && fraction == 0;                              \
    }                                                                     \
    static constexpr bool is_nan_fields(unsigned exponent,                \
                                        std::uint64_t fraction) {         \
      return exponent == all_exponent_field && fraction != 0;             \
    }                                                                     \
    static constexpr bool is_infinity_fields(unsigned exponent,           \
                                             std::uint64_t fraction) {    \
      return exponent == all_exponent_field && fraction == 0;             \
    }                                                                     \
    static constexpr bool is_quiet_nan_fraction(std::uint64_t fraction) { \
      return (fraction & (std::uint64_t{1} << (Frac - 1))) != 0;          \
    }                                                                     \
  }

PTXSIM_IEEE_TRAITS(float16_t, std::uint16_t, 16, 5, 10, 15, 0x8000u, 0x7C00u,
                   0x03FFu, 0x0200u);
PTXSIM_IEEE_TRAITS(bfloat16_t, std::uint16_t, 16, 8, 7, 127, 0x8000u, 0x7F80u,
                   0x007Fu, 0x0040u);
PTXSIM_IEEE_TRAITS(float32_t, std::uint32_t, 32, 8, 23, 127, 0x80000000u,
                   0x7F800000u, 0x007FFFFFu, 0x00400000u);
PTXSIM_IEEE_TRAITS(float64_t, std::uint64_t, 64, 11, 52, 1023,
                   0x8000000000000000ULL, 0x7FF0000000000000ULL,
                   0x000FFFFFFFFFFFFFULL, 0x0008000000000000ULL);
PTXSIM_IEEE_TRAITS(float8_e5m2_t, std::uint8_t, 8, 5, 2, 15, 0x80u, 0x7Cu,
                   0x03u, 0x02u);
#undef PTXSIM_IEEE_TRAITS

// TF32's precision/range are format properties; its raw storage encoding is
// deliberately not exposed by tfloat32_t and is selected by a model profile.
template <>
struct FormatTraits<tfloat32_t> {
  using Bits = std::uint32_t;
  static constexpr unsigned total_bits = 32, exponent_bits = 8,
                            fraction_bits = 10;
  static constexpr int exponent_bias = 127;
  static constexpr Bits sign_mask = 0x80000000u, exponent_mask = 0x7F800000u,
                        fraction_mask = 0x007FE000u,
                        quiet_nan_bit = 0x00400000u, storage_mask = 0xFFFFE000u;
  static constexpr bool has_subnormal = true, has_infinity = true,
                        has_quiet_nan = true, has_signaling_nan = true,
                        finite_only = false, fixed_encoding = false,
                        has_zero = true;
  static constexpr unsigned all_exponent_field = 0xFFu;
  static constexpr unsigned maximum_finite_exponent_field = 0xFEu;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x3FFu;
  static constexpr unsigned canonical_nan_exponent_field = 0xFFu;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0x200u;
  static constexpr bool preserves_nan_payload = true;
  static constexpr bool is_zero_fields(unsigned exponent,
                                       std::uint64_t fraction) {
    return exponent == 0 && fraction == 0;
  }
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
struct FormatTraits<float8_e4m3_t> {
  using Bits = std::uint8_t;
  static constexpr unsigned total_bits = 8, exponent_bits = 4,
                            fraction_bits = 3;
  static constexpr int exponent_bias = 7;
  static constexpr Bits sign_mask = 0x80u, exponent_mask = 0x78u,
                        fraction_mask = 0x07u, quiet_nan_bit = 0x04u,
                        storage_mask = 0xFFu;
  static constexpr bool has_subnormal = true, has_infinity = false,
                        has_quiet_nan = true, has_signaling_nan = false,
                        finite_only = false, fixed_encoding = true,
                        has_zero = true;
  static constexpr unsigned all_exponent_field = 0xFu;
  static constexpr unsigned maximum_finite_exponent_field = 0xFu;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x6u;
  static constexpr unsigned canonical_nan_exponent_field = 0xFu;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0x7u;
  static constexpr bool preserves_nan_payload = false;
  static constexpr bool is_zero_fields(unsigned exponent,
                                       std::uint64_t fraction) {
    return exponent == 0 && fraction == 0;
  }
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
struct FormatTraits<float4_e2m1_t> {
  using Bits = std::uint8_t;
  static constexpr unsigned total_bits = 4, exponent_bits = 2,
                            fraction_bits = 1;
  static constexpr int exponent_bias = 1;
  static constexpr Bits sign_mask = 0x08u, exponent_mask = 0x06u,
                        fraction_mask = 0x01u, quiet_nan_bit = 0,
                        storage_mask = 0x0Fu;
  static constexpr bool has_subnormal = true, has_infinity = false,
                        has_quiet_nan = false, has_signaling_nan = false,
                        finite_only = true, fixed_encoding = true,
                        has_zero = true;
  static constexpr unsigned all_exponent_field = 0x3u;
  static constexpr unsigned maximum_finite_exponent_field = 0x3u;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x1u;
  static constexpr unsigned canonical_nan_exponent_field = 0;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0;
  static constexpr bool preserves_nan_payload = false;
  static constexpr bool is_zero_fields(unsigned exponent,
                                       std::uint64_t fraction) {
    return exponent == 0 && fraction == 0;
  }
  static constexpr bool is_nan_fields(unsigned, std::uint64_t) { return false; }
  static constexpr bool is_infinity_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_quiet_nan_fraction(std::uint64_t) { return false; }
};

// PTX low-precision tensor formats are finite, fixed encodings.  Their
// all-ones exponent is numeric (not IEEE infinity/NaN).
#define PTXSIM_FINITE_TRAITS(Type, Width, Exp, Frac, Bias, Sign, ExpMask, \
                             FracMask)                                    \
  template <>                                                             \
  struct FormatTraits<Type> {                                             \
    using Bits = std::uint8_t;                                            \
    static constexpr unsigned total_bits = Width;                         \
    static constexpr unsigned exponent_bits = Exp;                        \
    static constexpr unsigned fraction_bits = Frac;                       \
    static constexpr int exponent_bias = Bias;                            \
    static constexpr Bits sign_mask = Sign;                               \
    static constexpr Bits exponent_mask = ExpMask;                        \
    static constexpr Bits fraction_mask = FracMask;                       \
    static constexpr Bits quiet_nan_bit = 0;                              \
    static constexpr Bits storage_mask = (Bits{1} << Width) - 1;          \
    static constexpr bool has_subnormal = true;                           \
    static constexpr bool has_zero = true;                                \
    static constexpr bool has_infinity = false;                           \
    static constexpr bool has_quiet_nan = false;                          \
    static constexpr bool has_signaling_nan = false;                      \
    static constexpr bool finite_only = true;                             \
    static constexpr bool fixed_encoding = true;                          \
    static constexpr unsigned all_exponent_field = (1u << Exp) - 1;       \
    static constexpr unsigned maximum_finite_exponent_field =             \
        all_exponent_field;                                               \
    static constexpr std::uint64_t maximum_finite_fraction_field =        \
        (1u << Frac) - 1;                                                 \
    static constexpr unsigned canonical_nan_exponent_field = 0;           \
    static constexpr std::uint64_t canonical_nan_fraction_field = 0;      \
    static constexpr bool preserves_nan_payload = false;                  \
    static constexpr bool is_zero_fields(unsigned exponent,               \
                                         std::uint64_t fraction) {         \
      return exponent == 0 && fraction == 0;                              \
    }                                                                     \
    static constexpr bool is_nan_fields(unsigned, std::uint64_t) {        \
      return false;                                                       \
    }                                                                     \
    static constexpr bool is_infinity_fields(unsigned, std::uint64_t) {   \
      return false;                                                       \
    }                                                                     \
    static constexpr bool is_quiet_nan_fraction(std::uint64_t) {          \
      return false;                                                       \
    }                                                                     \
  }

PTXSIM_FINITE_TRAITS(float6_e2m3_t, 6, 2, 3, 1, 0x20u, 0x18u, 0x07u);
PTXSIM_FINITE_TRAITS(float6_e3m2_t, 6, 3, 2, 3, 0x20u, 0x1Cu, 0x03u);
#undef PTXSIM_FINITE_TRAITS

template <>
struct FormatTraits<ufloat8_e8m0_t> {
  using Bits = std::uint8_t;
  static constexpr unsigned total_bits = 8, exponent_bits = 8,
                            fraction_bits = 0;
  static constexpr int exponent_bias = 127;
  static constexpr Bits sign_mask = 0, exponent_mask = 0xFFu, fraction_mask = 0,
                        quiet_nan_bit = 0, storage_mask = 0xFFu;
  // PTX ISA 9.3 §5.2.3: ue8m0 has no zero or infinity; 0xff is its
  // single (quiet) NaN encoding.
  static constexpr bool has_subnormal = false, has_zero = false,
                        has_infinity = false, has_quiet_nan = true,
                        has_signaling_nan = false, finite_only = false,
                        fixed_encoding = true;
  static constexpr unsigned all_exponent_field = 0xFFu;
  static constexpr unsigned maximum_finite_exponent_field = 0xFEu;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0;
  static constexpr unsigned canonical_nan_exponent_field = 0xFFu;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0;
  static constexpr bool preserves_nan_payload = false;
  static constexpr bool is_zero_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_nan_fields(unsigned exponent, std::uint64_t) {
    return exponent == canonical_nan_exponent_field;
  }
  static constexpr bool is_infinity_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_quiet_nan_fraction(std::uint64_t) { return true; }
};

template <>
struct FormatTraits<ufloat7_e4m3_t> {
  using Bits = std::uint8_t;
  static constexpr unsigned total_bits = 7, exponent_bits = 4,
                            fraction_bits = 3;
  static constexpr int exponent_bias = 7;
  static constexpr Bits sign_mask = 0, exponent_mask = 0x78u,
                        fraction_mask = 0x07u, quiet_nan_bit = 0,
                        storage_mask = 0x7Fu;
  // PTX ISA 9.3 §5.2.3: ue4m3 has no infinity; raw 0x7f is its only
  // NaN encoding.
  static constexpr bool has_subnormal = true, has_zero = true,
                        has_infinity = false, has_quiet_nan = true,
                        has_signaling_nan = false, finite_only = false,
                        fixed_encoding = true;
  static constexpr unsigned all_exponent_field = 0xFu;
  static constexpr unsigned maximum_finite_exponent_field = 0xFu;
  static constexpr std::uint64_t maximum_finite_fraction_field = 0x6u;
  static constexpr unsigned canonical_nan_exponent_field = 0xFu;
  static constexpr std::uint64_t canonical_nan_fraction_field = 0x7u;
  static constexpr bool preserves_nan_payload = false;
  static constexpr bool is_zero_fields(unsigned exponent,
                                       std::uint64_t fraction) {
    return exponent == 0 && fraction == 0;
  }
  static constexpr bool is_nan_fields(unsigned exponent, std::uint64_t fraction) {
    return exponent == canonical_nan_exponent_field &&
           fraction == canonical_nan_fraction_field;
  }
  static constexpr bool is_infinity_fields(unsigned, std::uint64_t) {
    return false;
  }
  static constexpr bool is_quiet_nan_fraction(std::uint64_t) { return true; }
};

template <typename T>
concept FloatingFormat = requires(T value) {
  typename FormatTraits<T>::Bits;
  { value.bits() } -> std::convertible_to<typename FormatTraits<T>::Bits>;
};

template <FloatingFormat T>
[[nodiscard]] constexpr bool is_valid_encoding(T value) noexcept {
  using Traits = FormatTraits<T>;
  return (value.bits() &
          static_cast<typename Traits::Bits>(~Traits::storage_mask)) == 0;
}

template <FloatingFormat T>
[[nodiscard]] constexpr T normalize_encoding(T value) noexcept {
  return T::from_bits(static_cast<typename FormatTraits<T>::Bits>(
      value.bits() & FormatTraits<T>::storage_mask));
}

template <FloatingFormat T>
[[nodiscard]] constexpr fp_class classify(T raw) noexcept {
  using Traits = FormatTraits<T>;
  const auto value = normalize_encoding(raw);
  constexpr unsigned fraction_lsb =
      Traits::fraction_mask == 0 ? 0 : std::countr_zero(Traits::fraction_mask);
  const auto exponent =
      static_cast<unsigned>((value.bits() & Traits::exponent_mask) >>
                            (Traits::fraction_bits + fraction_lsb));
  const auto fraction = static_cast<std::uint64_t>(
      (value.bits() & Traits::fraction_mask) >> fraction_lsb);
  if (Traits::is_zero_fields(exponent, fraction))
    return fp_class::zero;
  if (exponent == 0 && Traits::has_subnormal)
    return fp_class::subnormal;
  if (Traits::is_nan_fields(exponent, fraction))
    return Traits::is_quiet_nan_fraction(fraction) ? fp_class::quiet_nan
                                                   : fp_class::signaling_nan;
  if (Traits::is_infinity_fields(exponent, fraction))
    return fp_class::infinity;
  return fp_class::normal;
}

template <FloatingFormat T>
[[nodiscard]] constexpr bool is_negative(T value) noexcept {
  return (normalize_encoding(value).bits() & FormatTraits<T>::sign_mask) != 0;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_zero(T value) noexcept {
  return classify(value) == fp_class::zero;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_negative_zero(T value) noexcept {
  return is_zero(value) && is_negative(value);
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_subnormal(T value) noexcept {
  return classify(value) == fp_class::subnormal;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_normal(T value) noexcept {
  return classify(value) == fp_class::normal;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_nan(T value) noexcept {
  const auto c = classify(value);
  return c == fp_class::quiet_nan || c == fp_class::signaling_nan;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_quiet_nan(T value) noexcept {
  return classify(value) == fp_class::quiet_nan;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_signaling_nan(T value) noexcept {
  return classify(value) == fp_class::signaling_nan;
}
template <FloatingFormat T>
[[nodiscard]] constexpr bool is_infinity(T value) noexcept {
  return classify(value) == fp_class::infinity;
}

template <FloatingFormat T>
inline constexpr format_info format_info_v{
    .storage_bits = sizeof(typename FormatTraits<T>::Bits) * 8,
    .value_bits = FormatTraits<T>::total_bits,
    .exponent_bits = FormatTraits<T>::exponent_bits,
    .fraction_bits = FormatTraits<T>::fraction_bits,
    .exponent_bias = FormatTraits<T>::exponent_bias,
    .has_sign = FormatTraits<T>::sign_mask != 0,
    .has_zero = FormatTraits<T>::has_zero,
    .has_subnormal = FormatTraits<T>::has_subnormal,
    .has_infinity = FormatTraits<T>::has_infinity,
    .has_nan =
        FormatTraits<T>::has_quiet_nan || FormatTraits<T>::has_signaling_nan,
    .has_signaling_nan = FormatTraits<T>::has_signaling_nan,
    .fixed_encoding = FormatTraits<T>::fixed_encoding,
};

[[nodiscard]] constexpr fp_class classify(tfloat32_t value) noexcept {
  return classify(value.canonical_value());
}
[[nodiscard]] constexpr bool is_negative(tfloat32_t value) noexcept {
  return is_negative(value.canonical_value());
}
[[nodiscard]] constexpr bool is_zero(tfloat32_t value) noexcept {
  return is_zero(value.canonical_value());
}
[[nodiscard]] constexpr bool is_negative_zero(tfloat32_t value) noexcept {
  return is_negative_zero(value.canonical_value());
}
[[nodiscard]] constexpr bool is_subnormal(tfloat32_t value) noexcept {
  return is_subnormal(value.canonical_value());
}
[[nodiscard]] constexpr bool is_normal(tfloat32_t value) noexcept {
  return is_normal(value.canonical_value());
}
[[nodiscard]] constexpr bool is_nan(tfloat32_t value) noexcept {
  return is_nan(value.canonical_value());
}
[[nodiscard]] constexpr bool is_infinity(tfloat32_t value) noexcept {
  return is_infinity(value.canonical_value());
}

template <FloatingFormat T>
[[nodiscard]] constexpr T flush_subnormal(T value) noexcept {
  return is_subnormal(value)
             ? T::from_bits(static_cast<typename FormatTraits<T>::Bits>(
                   normalize_encoding(value).bits() &
                   FormatTraits<T>::sign_mask))
             : normalize_encoding(value);
}

static_assert(!std::same_as<float8_e4m3_t, float8_e5m2_t>);
static_assert(std::is_trivially_copyable_v<bfloat16_t> &&
              std::is_standard_layout_v<bfloat16_t>);
static_assert(std::is_trivially_copyable_v<tfloat32_t> &&
              std::is_standard_layout_v<tfloat32_t>);

}  // namespace ptxsim::arith
