#pragma once

#include <ptxsim/arith/controls.hpp>

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
  rcp,
  abs,
  neg,
  min,
  max,
  compare,
  rsqrt,
  sin,
  cos,
  lg2,
  ex2,
  tanh,
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

// This is the public source of truth for controls on homogeneous scalar
// floating-point operations.  Dispatch and the numerical backends both use
// it, so a control cannot be accepted by one layer and rejected by another.
template <bool Available, bool DirectedRounding = false, bool Ftz = false,
          bool Saturation = false, bool Relu = false>
struct floating_control_capability {
  static constexpr bool supported = Available;

  static constexpr bool supports(rounding_mode mode) {
    return mode == rounding_mode::nearest_even ||
           (DirectedRounding &&
            (mode == rounding_mode::toward_zero ||
             mode == rounding_mode::toward_negative ||
             mode == rounding_mode::toward_positive));
  }
  static constexpr bool supports(subnormal_mode mode) {
    return mode == subnormal_mode::preserve ||
           (Ftz && mode == subnormal_mode::flush_input_and_output);
  }
  static constexpr bool supports(saturation_mode mode) {
    return mode == saturation_mode::none ||
           (Saturation && mode == saturation_mode::zero_to_one);
  }
  static constexpr bool supports(activation_mode mode) {
    return mode == activation_mode::none ||
           (Relu && mode == activation_mode::relu);
  }
  static constexpr bool supports(floating_control control) {
    return Available && supports(control.rounding) && supports(control.subnormal) &&
           supports(control.saturation) && supports(control.activation) &&
           !(control.saturation != saturation_mode::none &&
             control.activation != activation_mode::none);
  }
};

template <scalar_operation Op, typename T, typename... Operands>
struct floating_operation_control_capability
    : floating_control_capability<false> {};

template <bool Available = false, bool Exact = false, bool Approximate = false,
          bool Full = false, bool Ftz = false, bool Preserve = true>
struct special_function_control_capability {
  static constexpr bool supported = Available;

  static constexpr bool supports(approximation_mode mode) {
    return Available && ((mode == approximation_mode::exact && Exact) ||
           (mode == approximation_mode::ptx_approximate && Approximate) ||
           (mode == approximation_mode::ptx_full && Full));
  }
  static constexpr bool supports(subnormal_mode mode) {
    return Available && ((Preserve && mode == subnormal_mode::preserve) ||
                         (Ftz &&
                          mode == subnormal_mode::flush_input_and_output));
  }
  static constexpr bool supports(special_function_control control) {
    return supports(control.approximation) && supports(control.subnormal);
  }
};

template <scalar_operation Op, typename T>
struct special_function_operation_capability
    : special_function_control_capability<> {};

template <>
struct special_function_operation_capability<scalar_operation::div, float32_t>
    : special_function_control_capability<true, true, true, true, true> {};
template <>
struct special_function_operation_capability<scalar_operation::sqrt, float32_t>
    : special_function_control_capability<true, true, true, false, true> {};
template <>
struct special_function_operation_capability<scalar_operation::rcp, float32_t>
    : special_function_control_capability<true, true, true, false, true> {};
template <>
struct special_function_operation_capability<scalar_operation::sqrt, float64_t>
    : special_function_control_capability<true, true> {};
template <>
struct special_function_operation_capability<scalar_operation::rcp, float64_t>
    : special_function_control_capability<true, true, true, false, true> {
  using Base = special_function_control_capability<true, true, true, false,
                                                   true>;
  using Base::supports;
  static constexpr bool supports(special_function_control control) {
    return (control.approximation == approximation_mode::exact &&
            control.subnormal == subnormal_mode::preserve) ||
           (control.approximation == approximation_mode::ptx_approximate &&
            control.subnormal == subnormal_mode::flush_input_and_output);
  }
};
template <>
struct special_function_operation_capability<scalar_operation::rsqrt, float32_t>
    : special_function_control_capability<true, false, true, false, true> {};
template <>
struct special_function_operation_capability<scalar_operation::rsqrt,
                                             float64_t>
    : special_function_control_capability<true, false, true, false, true> {
  using Base = special_function_control_capability<true, false, true, false,
                                                   true>;
  using Base::supports;
  static constexpr bool supports(special_function_control control) {
    return control.approximation == approximation_mode::ptx_approximate &&
           (control.subnormal == subnormal_mode::preserve ||
            control.subnormal == subnormal_mode::flush_input_and_output);
  }
};
template <scalar_operation Op>
  requires(Op == scalar_operation::sin || Op == scalar_operation::cos ||
           Op == scalar_operation::lg2 || Op == scalar_operation::ex2)
struct special_function_operation_capability<Op, float32_t>
    : special_function_control_capability<true, false, true, false, true> {};
template <typename T>
  requires(std::same_as<T, float16_t> || std::same_as<T, bfloat16_t> ||
           std::same_as<T, float32_t>)
struct special_function_operation_capability<scalar_operation::tanh, T>
    : special_function_control_capability<true, false, true, false, false> {};
template <>
struct special_function_operation_capability<scalar_operation::ex2, float16_t>
    : special_function_control_capability<true, false, true> {};
template <>
struct special_function_operation_capability<scalar_operation::ex2,
                                             bfloat16_t>
    : special_function_control_capability<true, false, true, false, true,
                                          false> {};

template <scalar_operation Op>
  requires(Op == scalar_operation::add || Op == scalar_operation::sub ||
           Op == scalar_operation::mul || Op == scalar_operation::fma ||
           Op == scalar_operation::mad || Op == scalar_operation::div ||
           Op == scalar_operation::sqrt || Op == scalar_operation::rcp)
struct floating_operation_control_capability<Op, float32_t>
    : floating_control_capability<true, true, true,
                                  Op != scalar_operation::div &&
                                      Op != scalar_operation::sqrt &&
                                      Op != scalar_operation::rcp> {};

template <scalar_operation Op>
  requires(Op == scalar_operation::add || Op == scalar_operation::sub ||
           Op == scalar_operation::mul || Op == scalar_operation::fma ||
           Op == scalar_operation::mad || Op == scalar_operation::div ||
           Op == scalar_operation::sqrt || Op == scalar_operation::rcp)
struct floating_operation_control_capability<Op, float64_t>
    : floating_control_capability<true, true> {};

template <scalar_operation Op>
  requires(Op == scalar_operation::add || Op == scalar_operation::sub ||
           Op == scalar_operation::mul || Op == scalar_operation::fma)
struct floating_operation_control_capability<Op, float16_t>
    : floating_control_capability<true, false, true, true,
                                  Op == scalar_operation::fma> {};

template <scalar_operation Op>
  requires(Op == scalar_operation::add || Op == scalar_operation::sub ||
           Op == scalar_operation::mul || Op == scalar_operation::fma)
struct floating_operation_control_capability<Op, bfloat16_t>
    : floating_control_capability<true, false, false, false,
                                  Op == scalar_operation::fma> {};

template <scalar_operation Op>
  requires(Op == scalar_operation::abs || Op == scalar_operation::neg ||
           Op == scalar_operation::min || Op == scalar_operation::max ||
           Op == scalar_operation::compare)
struct floating_operation_control_capability<Op, float32_t>
    : floating_control_capability<true, false, true> {};
template <scalar_operation Op>
  requires(Op == scalar_operation::abs || Op == scalar_operation::neg ||
           Op == scalar_operation::min || Op == scalar_operation::max ||
           Op == scalar_operation::compare)
struct floating_operation_control_capability<Op, float64_t>
    : floating_control_capability<true> {};
template <scalar_operation Op>
  requires(Op == scalar_operation::abs || Op == scalar_operation::neg ||
           Op == scalar_operation::min || Op == scalar_operation::max ||
           Op == scalar_operation::compare)
struct floating_operation_control_capability<Op, float16_t>
    : floating_control_capability<true, false, true> {};
template <scalar_operation Op>
  requires(Op == scalar_operation::abs || Op == scalar_operation::neg ||
           Op == scalar_operation::min || Op == scalar_operation::max ||
           Op == scalar_operation::compare)
struct floating_operation_control_capability<Op, bfloat16_t>
    : floating_control_capability<true> {};

template <scalar_operation Op, typename Low>
  requires((Op == scalar_operation::add || Op == scalar_operation::sub) &&
           (std::same_as<Low, float16_t> || std::same_as<Low, bfloat16_t>))
struct floating_operation_control_capability<Op, float32_t, Low, float32_t>
    : floating_control_capability<true, true, false, true> {};
template <typename Low>
  requires(std::same_as<Low, float16_t> || std::same_as<Low, bfloat16_t>)
struct floating_operation_control_capability<scalar_operation::fma, float32_t,
                                             Low, Low, float32_t>
    : floating_control_capability<true, true, false, true> {};

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
    : std::bool_constant<
          std::same_as<From, float32_t> &&
          (std::same_as<To, float16_t> || std::same_as<To, bfloat16_t> ||
           std::same_as<To, float8_e4m3_t> ||
           std::same_as<To, float8_e5m2_t> ||
           std::same_as<To, float6_e2m3_t> ||
           std::same_as<To, float6_e3m2_t> ||
           std::same_as<To, float4_e2m1_t>)> {};

template <typename T>
struct operation_capability<scalar_operation::add, T, T, T>
    : std::bool_constant<std::integral<T> ||
                         floating_operation_control_capability<
                             scalar_operation::add, T>::supported> {};
template <typename T>
struct operation_capability<scalar_operation::sub, T, T, T>
    : std::bool_constant<std::integral<T> ||
                         floating_operation_control_capability<
                             scalar_operation::sub, T>::supported> {};
template <typename T>
struct operation_capability<scalar_operation::mul, T, T, T>
    : std::bool_constant<std::integral<T> ||
                         floating_operation_control_capability<
                             scalar_operation::mul, T>::supported> {};
template <typename T>
struct operation_capability<scalar_operation::div, T, T, T>
    : std::bool_constant<floating_operation_control_capability<
                             scalar_operation::div, T>::supported> {};
template <typename T>
struct operation_capability<scalar_operation::sqrt, T, T>
    : std::bool_constant<floating_operation_control_capability<
                             scalar_operation::sqrt, T>::supported> {};
template <typename T>
struct operation_capability<scalar_operation::fma, T, T, T, T>
    : std::bool_constant<floating_operation_control_capability<
                             scalar_operation::fma, T>::supported> {};
template <>
struct operation_capability<scalar_operation::fma, float32_t, float16_t,
                            float16_t, float32_t>
    : std::bool_constant<floating_operation_control_capability<
          scalar_operation::fma, float32_t, float16_t, float16_t,
          float32_t>::supported> {};
template <>
struct operation_capability<scalar_operation::fma, float32_t, bfloat16_t,
                            bfloat16_t, float32_t>
    : std::bool_constant<floating_operation_control_capability<
          scalar_operation::fma, float32_t, bfloat16_t, bfloat16_t,
          float32_t>::supported> {};
template <typename T>
struct operation_capability<scalar_operation::mad, T, T, T, T>
    : std::bool_constant<floating_operation_control_capability<
                             scalar_operation::mad, T>::supported> {};
#define PTXSIM_MIXED_F32_CAPABILITY(op, low)                               \
  template <>                                                               \
  struct operation_capability<scalar_operation::op, float32_t, low,        \
                              float32_t>                                   \
      : std::bool_constant<floating_operation_control_capability<          \
            scalar_operation::op, float32_t, low, float32_t>::supported> {}
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
