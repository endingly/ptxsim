#pragma once

#include <ptxsim/arith/detail/format_traits.hpp>

#include <type_traits>

namespace ptxsim::arith {

template <typename To, typename From>
struct ConversionTraits {
  static constexpr bool supported = false;
};

#define PTXSIM_NARROW_CONVERSION_TRAITS(To)         \
  template <>                                       \
  struct ConversionTraits<To, float32_t> {          \
    using Output = To;                              \
    static constexpr bool supported = true;         \
    static constexpr bool accepts_rounding = true;  \
    static constexpr bool accepts_satfinite = true; \
    static constexpr bool inherent_saturation =     \
        !FormatTraits<To>::has_infinity;            \
  }

#define PTXSIM_WIDEN_CONVERSION_TRAITS(From)           \
  template <>                                          \
  struct ConversionTraits<float32_t, From> {           \
    using Output = float32_t;                          \
    static constexpr bool supported = true;            \
    static constexpr bool accepts_rounding = false;    \
    static constexpr bool accepts_satfinite = false;   \
    static constexpr bool inherent_saturation = false; \
  }

PTXSIM_NARROW_CONVERSION_TRAITS(bfloat16_t);
PTXSIM_NARROW_CONVERSION_TRAITS(float16_t);
PTXSIM_NARROW_CONVERSION_TRAITS(float8_e4m3_t);
PTXSIM_NARROW_CONVERSION_TRAITS(float8_e5m2_t);
PTXSIM_NARROW_CONVERSION_TRAITS(float6_e2m3_t);
PTXSIM_NARROW_CONVERSION_TRAITS(float6_e3m2_t);
PTXSIM_NARROW_CONVERSION_TRAITS(float4_e2m1_t);
PTXSIM_NARROW_CONVERSION_TRAITS(ufloat8_e8m0_t);
PTXSIM_NARROW_CONVERSION_TRAITS(ufloat7_e4m3_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(bfloat16_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(float16_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(float8_e4m3_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(float8_e5m2_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(float6_e2m3_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(float6_e3m2_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(float4_e2m1_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(ufloat8_e8m0_t);
PTXSIM_WIDEN_CONVERSION_TRAITS(ufloat7_e4m3_t);

template <>
struct ConversionTraits<float16_t, float64_t> {
  using Output = float16_t;
  static constexpr bool supported = true;
  static constexpr bool accepts_rounding = true;
  static constexpr bool accepts_satfinite = false;
  static constexpr bool inherent_saturation = false;
};

template <>
struct ConversionTraits<float64_t, float16_t> {
  using Output = float64_t;
  static constexpr bool supported = true;
  static constexpr bool accepts_rounding = false;
  static constexpr bool accepts_satfinite = false;
  static constexpr bool inherent_saturation = false;
};

#undef PTXSIM_NARROW_CONVERSION_TRAITS
#undef PTXSIM_WIDEN_CONVERSION_TRAITS

template <typename To, typename From>
concept SupportedConversion = ConversionTraits<To, From>::supported;

}  // namespace ptxsim::arith
