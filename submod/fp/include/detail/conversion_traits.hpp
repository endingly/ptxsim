#pragma once

#include <ptxsim/fp/types.hpp>

#include <type_traits>

namespace ptxsim::fp {

template <typename To, typename From>
struct ConversionTraits {
  static constexpr bool supported = false;
};

#define PTXSIM_NARROW_CONVERSION_TRAITS(To)                                  \
  template <>                                                                 \
  struct ConversionTraits<To, Fp32> {                                        \
    using Output = To;                                                        \
    static constexpr bool supported = true;                                  \
    static constexpr bool accepts_rounding = true;                           \
    static constexpr bool accepts_satfinite = true;                          \
    static constexpr bool inherent_saturation =                              \
        !FormatTraits<To>::has_infinity;                                     \
  }

#define PTXSIM_WIDEN_CONVERSION_TRAITS(From)                                 \
  template <>                                                                 \
  struct ConversionTraits<Fp32, From> {                                      \
    using Output = Fp32;                                                      \
    static constexpr bool supported = true;                                  \
    static constexpr bool accepts_rounding = false;                          \
    static constexpr bool accepts_satfinite = false;                         \
    static constexpr bool inherent_saturation = false;                       \
  }

PTXSIM_NARROW_CONVERSION_TRAITS(Bf16);
PTXSIM_NARROW_CONVERSION_TRAITS(Fp16);
PTXSIM_NARROW_CONVERSION_TRAITS(Tf32);
PTXSIM_NARROW_CONVERSION_TRAITS(Fp8E4M3);
PTXSIM_NARROW_CONVERSION_TRAITS(Fp8E5M2);
PTXSIM_NARROW_CONVERSION_TRAITS(Fp4E2M1);
PTXSIM_WIDEN_CONVERSION_TRAITS(Bf16);
PTXSIM_WIDEN_CONVERSION_TRAITS(Fp16);
PTXSIM_WIDEN_CONVERSION_TRAITS(Tf32);
PTXSIM_WIDEN_CONVERSION_TRAITS(Fp8E4M3);
PTXSIM_WIDEN_CONVERSION_TRAITS(Fp8E5M2);
PTXSIM_WIDEN_CONVERSION_TRAITS(Fp4E2M1);

template <>
struct ConversionTraits<Fp16, Fp64> {
  using Output = Fp16;
  static constexpr bool supported = true;
  static constexpr bool accepts_rounding = true;
  static constexpr bool accepts_satfinite = false;
  static constexpr bool inherent_saturation = false;
};

template <>
struct ConversionTraits<Fp64, Fp16> {
  using Output = Fp64;
  static constexpr bool supported = true;
  static constexpr bool accepts_rounding = false;
  static constexpr bool accepts_satfinite = false;
  static constexpr bool inherent_saturation = false;
};

#undef PTXSIM_NARROW_CONVERSION_TRAITS
#undef PTXSIM_WIDEN_CONVERSION_TRAITS

template <typename To, typename From>
concept SupportedConversion = ConversionTraits<To, From>::supported;

}  // namespace ptxsim::fp
