#pragma once

#include <ptxsim/arith/controls.hpp>
#include "internal_controls.hpp"
#include "conversion_traits.hpp"
#include "internal_result.hpp"
#include <ptxsim/arith/types.hpp>

namespace ptxsim::arith::detail {

template <typename To>
Result<To> narrow_from_f32(float32_t, ConversionControl);

template <typename From>
Result<float32_t> widen_to_f32(From, ConversionControl);

struct Bf16Backend {
  static Result<bfloat16_t> add(bfloat16_t, bfloat16_t, ArithmeticControl);
  static Result<bfloat16_t> sub(bfloat16_t, bfloat16_t, ArithmeticControl);
  static Result<bfloat16_t> mul(bfloat16_t, bfloat16_t, ArithmeticControl);
  static Result<bfloat16_t> fma(bfloat16_t, bfloat16_t, bfloat16_t, ArithmeticControl);
};

extern template Result<bfloat16_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<float8_e4m3_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<float8_e5m2_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<float6_e2m3_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<float6_e3m2_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<float4_e2m1_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<ufloat8_e8m0_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<ufloat7_e4m3_t> narrow_from_f32(float32_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(bfloat16_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(float8_e4m3_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(float8_e5m2_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(float6_e2m3_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(float6_e3m2_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(float4_e2m1_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(ufloat8_e8m0_t, ConversionControl);
extern template Result<float32_t> widen_to_f32(ufloat7_e4m3_t, ConversionControl);

}  // namespace ptxsim::arith::detail
