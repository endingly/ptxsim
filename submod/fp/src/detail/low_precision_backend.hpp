#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/detail/conversion_traits.hpp>
#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

namespace ptxsim::fp::detail {

template <typename To>
Result<To> narrow_from_f32(Fp32, ConversionControl);

template <typename From>
Result<Fp32> widen_to_f32(From, ConversionControl);

struct Bf16Backend {
  static Result<Bf16> add(Bf16, Bf16, ArithmeticControl);
  static Result<Bf16> sub(Bf16, Bf16, ArithmeticControl);
  static Result<Bf16> mul(Bf16, Bf16, ArithmeticControl);
  static Result<Bf16> fma(Bf16, Bf16, Bf16, ArithmeticControl);
};

extern template Result<Bf16> narrow_from_f32(Fp32, ConversionControl);
extern template Result<Tf32> narrow_from_f32(Fp32, ConversionControl);
extern template Result<Fp8E4M3> narrow_from_f32(Fp32, ConversionControl);
extern template Result<Fp8E5M2> narrow_from_f32(Fp32, ConversionControl);
extern template Result<Fp4E2M1> narrow_from_f32(Fp32, ConversionControl);
extern template Result<Fp32> widen_to_f32(Bf16, ConversionControl);
extern template Result<Fp32> widen_to_f32(Tf32, ConversionControl);
extern template Result<Fp32> widen_to_f32(Fp8E4M3, ConversionControl);
extern template Result<Fp32> widen_to_f32(Fp8E5M2, ConversionControl);
extern template Result<Fp32> widen_to_f32(Fp4E2M1, ConversionControl);

}  // namespace ptxsim::fp::detail
