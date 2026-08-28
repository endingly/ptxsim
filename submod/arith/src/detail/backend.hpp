#pragma once

#include <ptxsim/arith/controls.hpp>
#include "conversion_traits.hpp"
#include "internal_controls.hpp"
#include "internal_result.hpp"
#include <ptxsim/arith/types.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace ptxsim::arith::detail {

class backend {
 public:
  // IEEE binary16.  PTX scalar f16 arithmetic permits .rn only.
  [[nodiscard]] static Result<float16_t> add(float16_t, float16_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> sub(float16_t, float16_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> mul(float16_t, float16_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> fma(float16_t, float16_t, float16_t,
                                             ArithmeticControl = {});
  //
  // Binary32
  //

  [[nodiscard]]
  static Result<float32_t> add(float32_t lhs, float32_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float32_t> sub(float32_t lhs, float32_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float32_t> mul(float32_t lhs, float32_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float32_t> fma(float32_t a, float32_t b, float32_t c,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float32_t> div(float32_t lhs, float32_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float32_t> sqrt(float32_t value,
                                ArithmeticControl control = {});
  [[nodiscard]] static Result<float32_t> mad(float32_t, float32_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> rcp(float32_t, ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> div_approx(float32_t, float32_t,
                                                    ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> div_full(float32_t, float32_t,
                                                  ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> rcp_approx(float32_t,
                                                    ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> sqrt_approx(float32_t,
                                                     ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> rsqrt_approx(
      float32_t, ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> sin_approx(float32_t,
                                                    ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> cos_approx(float32_t,
                                                    ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> lg2_approx(float32_t,
                                                    ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> ex2_approx(float32_t,
                                                    ApproximationControl = {});
  [[nodiscard]] static Result<float32_t> tanh_approx(float32_t,
                                                       ApproximationControl);
  //
  // Binary64
  //

  [[nodiscard]]
  static Result<float64_t> add(float64_t lhs, float64_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float64_t> sub(float64_t lhs, float64_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float64_t> mul(float64_t lhs, float64_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float64_t> fma(float64_t a, float64_t b, float64_t c,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float64_t> div(float64_t lhs, float64_t rhs,
                               ArithmeticControl control = {});
  [[nodiscard]]
  static Result<float64_t> sqrt(float64_t value,
                                ArithmeticControl control = {});
  [[nodiscard]] static Result<float64_t> mad(float64_t, float64_t, float64_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float64_t> rcp(float64_t, ArithmeticControl = {});
  [[nodiscard]] static Result<float64_t> rsqrt_approx(float64_t);
  [[nodiscard]] static Result<float64_t> rcp_approx_ftz(float64_t);
  [[nodiscard]] static Result<float64_t> rsqrt_approx_ftz(float64_t);
  // BF16 arithmetic uses a direct integer guard/round/sticky core.  FMA is
  // fused: the exact product and addend are rounded to BF16 only once.
  [[nodiscard]] static Result<bfloat16_t> add(bfloat16_t lhs, bfloat16_t rhs,
                                              ArithmeticControl control = {});
  [[nodiscard]] static Result<bfloat16_t> sub(bfloat16_t lhs, bfloat16_t rhs,
                                              ArithmeticControl control = {});
  [[nodiscard]] static Result<bfloat16_t> mul(bfloat16_t lhs, bfloat16_t rhs,
                                              ArithmeticControl control = {});
  [[nodiscard]] static Result<bfloat16_t> fma(bfloat16_t a, bfloat16_t b,
                                              bfloat16_t c,
                                              ArithmeticControl control = {});
  // PTX mixed-precision scalar operations.  The low-precision operands are
  // exactly widened and the operation rounds once to binary32.
  [[nodiscard]] static Result<float32_t> add(float16_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> sub(float16_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> fma(float16_t, float16_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> add(bfloat16_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> sub(bfloat16_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> fma(bfloat16_t, bfloat16_t, float32_t,
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> abs(float16_t, ArithmeticControl = {});
  [[nodiscard]] static Result<bfloat16_t> abs(bfloat16_t,
                                              ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> abs(float32_t, ArithmeticControl = {});
  [[nodiscard]] static Result<float64_t> abs(float64_t, ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> neg(float16_t, ArithmeticControl = {});
  [[nodiscard]] static Result<bfloat16_t> neg(bfloat16_t,
                                              ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> neg(float32_t, ArithmeticControl = {});
  [[nodiscard]] static Result<float64_t> neg(float64_t, ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> min(float16_t, float16_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<bfloat16_t> min(bfloat16_t, bfloat16_t,
                                              MinMaxControl = {},
                                              ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> min(float32_t, float32_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> min(float32_t, float32_t, float32_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float64_t> min(float64_t, float64_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float16_t> max(float16_t, float16_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<bfloat16_t> max(bfloat16_t, bfloat16_t,
                                              MinMaxControl = {},
                                              ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> max(float32_t, float32_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float32_t> max(float32_t, float32_t, float32_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<float64_t> max(float64_t, float64_t,
                                             MinMaxControl = {},
                                             ArithmeticControl = {});
  [[nodiscard]] static Result<bool> compare(float16_t, float16_t, CompareOp,
                                            ArithmeticControl = {});
  [[nodiscard]] static Result<bool> compare(bfloat16_t, bfloat16_t, CompareOp,
                                            ArithmeticControl = {});
  [[nodiscard]] static Result<bool> compare(float32_t, float32_t, CompareOp,
                                            ArithmeticControl = {});
  [[nodiscard]] static Result<bool> compare(
      float64_t, float64_t, CompareOp,
      ArithmeticControl =
          {});  // PTX operand order: copy sign of the first argument into the magnitude of
  // the second argument.
  [[nodiscard]] static Result<float32_t> copysign(float32_t sign,
                                                  float32_t magnitude);
  [[nodiscard]] static Result<float64_t> copysign(float64_t sign,
                                                  float64_t magnitude);
  [[nodiscard]] static Result<bool> testp(float32_t, TestpOp);
  [[nodiscard]] static Result<bool> testp(float64_t, TestpOp);
  [[nodiscard]] static Result<float16_t> tanh_approx(float16_t,
                                                       ApproximationControl);
  [[nodiscard]] static Result<float16_t> ex2_approx(float16_t,
                                                      ApproximationControl);
  [[nodiscard]] static Result<bfloat16_t> tanh_approx(bfloat16_t,
                                                        ApproximationControl);
  [[nodiscard]] static Result<bfloat16_t> ex2_approx(bfloat16_t,
                                                       ApproximationControl);
  [[nodiscard]] static Result<float32_t> i32_to_f32(std::int32_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<std::int32_t> f32_to_i32(float32_t value,
                                                       RoundingMode rounding);
  [[nodiscard]] static Result<float32_t> u32_to_f32(std::uint32_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<std::uint32_t> f32_to_u32(float32_t value,
                                                        RoundingMode rounding);
  [[nodiscard]] static Result<float32_t> i64_to_f32(std::int64_t, RoundingMode);
  [[nodiscard]] static Result<std::int64_t> f32_to_i64(float32_t, RoundingMode);
  [[nodiscard]] static Result<float32_t> u64_to_f32(std::uint64_t,
                                                    RoundingMode);
  [[nodiscard]] static Result<std::uint64_t> f32_to_u64(float32_t,
                                                        RoundingMode);
  [[nodiscard]] static Result<float64_t> i32_to_f64(std::int32_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<std::int32_t> f64_to_i32(float64_t value,
                                                       RoundingMode rounding);
  [[nodiscard]] static Result<float64_t> u32_to_f64(std::uint32_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<std::uint32_t> f64_to_u32(float64_t value,
                                                        RoundingMode rounding);
  [[nodiscard]] static Result<float64_t> f32_to_f64(float32_t value);
  [[nodiscard]] static Result<float32_t> f64_to_f32(float64_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<float16_t> f32_to_f16(float32_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<float32_t> f16_to_f32(float16_t value);
  [[nodiscard]] static Result<float16_t> f64_to_f16(float64_t value,
                                                    RoundingMode rounding);
  [[nodiscard]] static Result<float64_t> f16_to_f64(float16_t value);
};

}  // namespace ptxsim::arith::detail
