#pragma once

// This is the only implementation bridge used by the public scalar facade.
// It deliberately speaks only in arith public types; SoftFloat and the former
// backend control/result types stay in src/detail.
#include <expected>

#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/result.hpp>
#include <ptxsim/arith/types.hpp>

namespace ptxsim::arith {
struct tf32_encoding_profile;
struct approximation_profile;
}
namespace ptxsim::arith::detail::dispatch {

#define PTXSIM_ARITH_DISPATCH_BINARY(T)                                   \
  std::expected<result<T, floating_status>, arithmetic_error> add(T, T,   \
                                                                    floating_control); \
  std::expected<result<T, floating_status>, arithmetic_error> sub(T, T,   \
                                                                    floating_control); \
  std::expected<result<T, floating_status>, arithmetic_error> mul(T, T,   \
                                                                    floating_control)
PTXSIM_ARITH_DISPATCH_BINARY(float16_t);
PTXSIM_ARITH_DISPATCH_BINARY(bfloat16_t);
PTXSIM_ARITH_DISPATCH_BINARY(float32_t);
PTXSIM_ARITH_DISPATCH_BINARY(float64_t);
#undef PTXSIM_ARITH_DISPATCH_BINARY
std::expected<result<float32_t, floating_status>, arithmetic_error> add(
    float16_t, float32_t, floating_control);
std::expected<result<float32_t, floating_status>, arithmetic_error> sub(
    float16_t, float32_t, floating_control);
std::expected<result<float32_t, floating_status>, arithmetic_error> add(
    bfloat16_t, float32_t, floating_control);
std::expected<result<float32_t, floating_status>, arithmetic_error> sub(
    bfloat16_t, float32_t, floating_control);

std::expected<result<float32_t, floating_status>, arithmetic_error> div(
    float32_t, float32_t, floating_control);
std::expected<result<float64_t, floating_status>, arithmetic_error> div(
    float64_t, float64_t, floating_control);

#define PTXSIM_ARITH_DISPATCH_FMA(T)                                      \
  std::expected<result<T, floating_status>, arithmetic_error> fma(        \
      T, T, T, floating_control)
PTXSIM_ARITH_DISPATCH_FMA(float16_t);
PTXSIM_ARITH_DISPATCH_FMA(bfloat16_t);
PTXSIM_ARITH_DISPATCH_FMA(float32_t);
PTXSIM_ARITH_DISPATCH_FMA(float64_t);
#undef PTXSIM_ARITH_DISPATCH_FMA
std::expected<result<float32_t, floating_status>, arithmetic_error> fma(
    float16_t, float16_t, float32_t, floating_control);
std::expected<result<float32_t, floating_status>, arithmetic_error> fma(
    bfloat16_t, bfloat16_t, float32_t, floating_control);

#define PTXSIM_ARITH_DISPATCH_UNARY(T)                                    \
  std::expected<result<T, floating_status>, arithmetic_error> abs(        \
      T, floating_control);                                                \
  std::expected<result<T, floating_status>, arithmetic_error> neg(        \
      T, floating_control);                                                \
  std::expected<result<T, floating_status>, arithmetic_error> min(        \
      T, T, floating_control);                                             \
  std::expected<result<T, floating_status>, arithmetic_error> max(        \
      T, T, floating_control)
PTXSIM_ARITH_DISPATCH_UNARY(float16_t);
PTXSIM_ARITH_DISPATCH_UNARY(bfloat16_t);
PTXSIM_ARITH_DISPATCH_UNARY(float32_t);
PTXSIM_ARITH_DISPATCH_UNARY(float64_t);
#undef PTXSIM_ARITH_DISPATCH_UNARY

std::expected<result<float32_t, floating_status>, arithmetic_error> sqrt(
    float32_t, floating_control);
std::expected<result<float64_t, floating_status>, arithmetic_error> sqrt(
    float64_t, floating_control);
std::expected<result<float32_t, floating_status>, arithmetic_error> rcp(
    float32_t, floating_control);
std::expected<result<float64_t, floating_status>, arithmetic_error> rcp(
    float64_t, floating_control);
std::expected<result<tfloat32_t, floating_status>, arithmetic_error>
quantize_tf32(float32_t, conversion_control,
              const tf32_encoding_profile&);

std::expected<result<float32_t, floating_status>, arithmetic_error> div_approx(
    float32_t, float32_t, special_function_control, const approximation_profile&);
std::expected<result<float32_t, floating_status>, arithmetic_error> div_full(
    float32_t, float32_t, special_function_control, const approximation_profile&);
#define PTXSIM_ARITH_DISPATCH_APPROX(name)                                 \
  std::expected<result<float32_t, floating_status>, arithmetic_error> name(\
      float32_t, special_function_control, const approximation_profile&)
PTXSIM_ARITH_DISPATCH_APPROX(rcp_approx);
PTXSIM_ARITH_DISPATCH_APPROX(sqrt_approx);
PTXSIM_ARITH_DISPATCH_APPROX(rsqrt_approx);
PTXSIM_ARITH_DISPATCH_APPROX(sin_approx);
PTXSIM_ARITH_DISPATCH_APPROX(cos_approx);
PTXSIM_ARITH_DISPATCH_APPROX(lg2_approx);
PTXSIM_ARITH_DISPATCH_APPROX(ex2_approx);
PTXSIM_ARITH_DISPATCH_APPROX(tanh_approx);
#undef PTXSIM_ARITH_DISPATCH_APPROX
std::expected<result<float64_t, floating_status>, arithmetic_error>
rcp_approx_ftz(float64_t, special_function_control,
               const approximation_profile&);
std::expected<result<float64_t, floating_status>, arithmetic_error>
rsqrt_approx(float64_t, special_function_control, const approximation_profile&);
std::expected<result<float64_t, floating_status>, arithmetic_error>
rsqrt_approx_ftz(float64_t, special_function_control,
                 const approximation_profile&);
std::expected<result<float16_t, floating_status>, arithmetic_error> tanh_approx(
    float16_t, special_function_control, const approximation_profile&);
std::expected<result<bfloat16_t, floating_status>, arithmetic_error> tanh_approx(
    bfloat16_t, special_function_control, const approximation_profile&);
std::expected<result<float16_t, floating_status>, arithmetic_error> ex2_approx(
    float16_t, special_function_control, const approximation_profile&);
std::expected<result<bfloat16_t, floating_status>, arithmetic_error> ex2_approx(
    bfloat16_t, special_function_control, const approximation_profile&);

}  // namespace ptxsim::arith::detail::dispatch
