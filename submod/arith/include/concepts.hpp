#pragma once

#include <ptxsim/arith/types.hpp>

#include <concepts>
#include <type_traits>

namespace ptxsim::arith {

// This is the sole operation vocabulary for public arithmetic capabilities.
// Compatibility traits for packed and tensor forms below are derived from it;
// no second capability table is allowed to grow independently.
enum class scalar_operation {
  add,
  sub,
  mul,
  fma,
  mad,
  div,
  sqrt,
  cvt,
  pack,
  unpack,
  mma,
  scaled_mma,
};
enum class arithmetic_family {
  signed_integer,
  unsigned_integer,
  ieee_binary,
  bfloat,
  finite_low_precision,
  tensor_quantized,
  fixed_point,
  raw_bits,
  predicate,
};

template <typename T>
struct arithmetic_family_of {
  static constexpr arithmetic_family value =
      std::is_integral_v<T>
          ? (std::is_signed_v<T> ? arithmetic_family::signed_integer
                                 : arithmetic_family::unsigned_integer)
          : arithmetic_family::raw_bits;
};

template <typename F>
struct arithmetic_family_of<basic_float<F>> {
  static constexpr arithmetic_family value =
      arithmetic_family::finite_low_precision;
};
template <>
struct arithmetic_family_of<float16_t> {
  static constexpr auto value = arithmetic_family::ieee_binary;
};
template <>
struct arithmetic_family_of<float32_t> {
  static constexpr auto value = arithmetic_family::ieee_binary;
};
template <>
struct arithmetic_family_of<float64_t> {
  static constexpr auto value = arithmetic_family::ieee_binary;
};
template <>
struct arithmetic_family_of<bfloat16_t> {
  static constexpr auto value = arithmetic_family::bfloat;
};
template <>
struct arithmetic_family_of<tfloat32_t> {
  static constexpr auto value = arithmetic_family::tensor_quantized;
};
template <>
struct arithmetic_family_of<fixed8_s2f6_t> {
  static constexpr auto value = arithmetic_family::fixed_point;
};

template <typename T>
inline constexpr arithmetic_family arithmetic_family_v =
    arithmetic_family_of<std::remove_cvref_t<T>>::value;

template <scalar_operation Op, typename Result, typename... Operands>
struct operation_capability : std::false_type {};

// Conversion capability is intentionally sourced from one generic canonical
// route: every supported type has a decode hook and an encode hook.  It is no
// longer a Cartesian list of pairwise F32 hub routes.
template <typename To, typename From>
struct conversion_capability {
 private:
  template <typename T>
  static constexpr bool integer = std::integral<T> && !std::same_as<T, bool>;
  template <typename T>
  static constexpr bool scalar =
      arithmetic_family_v<T> == arithmetic_family::ieee_binary ||
      arithmetic_family_v<T> == arithmetic_family::bfloat ||
      arithmetic_family_v<T> == arithmetic_family::finite_low_precision ||
      arithmetic_family_v<T> == arithmetic_family::tensor_quantized;
  static constexpr bool canonical_type_to = integer<To> || scalar<To> ||
                                            std::same_as<To, fixed8_s2f6_t>;
  static constexpr bool canonical_type_from = integer<From> || scalar<From> ||
                                              std::same_as<From, fixed8_s2f6_t>;
  static constexpr bool tf32_bridge =
      (std::same_as<To, tfloat32_t> && std::same_as<From, float32_t>) ||
      (std::same_as<To, float32_t> && std::same_as<From, tfloat32_t>);
  static constexpr bool tf32_participates = std::same_as<To, tfloat32_t> ||
                                            std::same_as<From, tfloat32_t>;

 public:
  static constexpr bool value =
      tf32_participates ? tf32_bridge : canonical_type_to && canonical_type_from;
};

template <typename To, typename From>
struct operation_capability<scalar_operation::cvt, To, From>
    : std::bool_constant<conversion_capability<To, From>::value> {};

// Rounding forms are deliberately a smaller capability matrix than ordinary
// canonical conversion.  They are numerical scalar forms, not an assertion
// about PTX packed operand ordering.
enum class conversion_control_feature { nearest_away, stochastic };
template <typename To, typename From, conversion_control_feature Feature>
struct conversion_control_capability : std::false_type {};
template <typename To, typename From>
struct conversion_narrowing_control_capability
    : std::bool_constant<
          (std::same_as<From, float32_t> || std::same_as<From, float64_t>) &&
          (std::same_as<To, float16_t> || std::same_as<To, bfloat16_t> ||
           std::same_as<To, float8_e4m3_t> ||
           std::same_as<To, float8_e5m2_t> ||
           std::same_as<To, float6_e2m3_t> ||
           std::same_as<To, float6_e3m2_t> ||
           std::same_as<To, float4_e2m1_t> ||
           std::same_as<To, ufloat8_e8m0_t> ||
           std::same_as<To, ufloat7_e4m3_t>)> {};
template <typename To, typename From>
struct conversion_control_capability<To, From,
                                     conversion_control_feature::nearest_away>
    : std::bool_constant<
          conversion_narrowing_control_capability<To, From>::value ||
          (std::same_as<To, tfloat32_t> &&
           std::same_as<From, float32_t>)> {};
template <typename To, typename From>
struct conversion_control_capability<To, From,
                                     conversion_control_feature::stochastic>
    : conversion_narrowing_control_capability<To, From> {};

template <typename T>
struct operation_capability<scalar_operation::add, T, T, T>
    : std::bool_constant<std::integral<T> ||
                         arithmetic_family_v<T> ==
                             arithmetic_family::ieee_binary ||
                         arithmetic_family_v<T> == arithmetic_family::bfloat> {
};
template <typename T>
struct operation_capability<scalar_operation::sub, T, T, T>
    : operation_capability<scalar_operation::add, T, T, T> {};
template <typename T>
struct operation_capability<scalar_operation::mul, T, T, T>
    : operation_capability<scalar_operation::add, T, T, T> {};
template <typename T>
struct operation_capability<scalar_operation::div, T, T, T>
    : std::bool_constant<std::same_as<T, float32_t> ||
                         std::same_as<T, float64_t>> {};
template <typename T>
struct operation_capability<scalar_operation::sqrt, T, T>
    : std::bool_constant<std::same_as<T, float32_t> ||
                         std::same_as<T, float64_t>> {};
template <typename T>
struct operation_capability<scalar_operation::fma, T, T, T, T>
    : std::bool_constant<arithmetic_family_v<T> ==
                             arithmetic_family::ieee_binary ||
                         arithmetic_family_v<T> == arithmetic_family::bfloat> {
};
template <>
struct operation_capability<scalar_operation::fma, float32_t, float16_t,
                            float16_t, float32_t> : std::true_type {};
template <>
struct operation_capability<scalar_operation::fma, float32_t, bfloat16_t,
                            bfloat16_t, float32_t> : std::true_type {};
template <typename T>
struct operation_capability<scalar_operation::mad, T, T, T, T>
    : std::bool_constant<std::same_as<T, float32_t> ||
                         std::same_as<T, float64_t>> {};
#define PTXSIM_MIXED_F32_CAPABILITY(op, low)                               \
  template <>                                                               \
  struct operation_capability<scalar_operation::op, float32_t, low,        \
                              float32_t> : std::true_type {}
PTXSIM_MIXED_F32_CAPABILITY(add, float16_t);
PTXSIM_MIXED_F32_CAPABILITY(sub, float16_t);
PTXSIM_MIXED_F32_CAPABILITY(add, bfloat16_t);
PTXSIM_MIXED_F32_CAPABILITY(sub, bfloat16_t);
#undef PTXSIM_MIXED_F32_CAPABILITY

template <typename T>
concept arithmetic_integer = std::integral<T> && !std::same_as<T, bool>;
template <typename T>
concept scalar_float =
    requires { arithmetic_family_v<T>; } &&
    (arithmetic_family_v<T> == arithmetic_family::ieee_binary ||
     arithmetic_family_v<T> == arithmetic_family::bfloat ||
     arithmetic_family_v<T> == arithmetic_family::finite_low_precision ||
     arithmetic_family_v<T> == arithmetic_family::tensor_quantized);
template <typename T>
concept scalar_addable =
    operation_capability<scalar_operation::add, T, T, T>::value;
template <typename T>
concept fixed_scalar = arithmetic_family_v<T> == arithmetic_family::fixed_point;
template <typename To, typename From>
concept convertible_to =
    operation_capability<scalar_operation::cvt, To, From>::value;
template <typename T>
concept tensor_multiplicand = scalar_float<T> || arithmetic_integer<T>;

}  // namespace ptxsim::arith
