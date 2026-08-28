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
    float32_t, float32_t, special_function_control);
std::expected<result<float32_t, floating_status>, arithmetic_error> div_full(
    float32_t, float32_t, special_function_control);
#define PTXSIM_ARITH_DISPATCH_APPROX(name)                                 \
  std::expected<result<float32_t, floating_status>, arithmetic_error> name(\
      float32_t, special_function_control)
PTXSIM_ARITH_DISPATCH_APPROX(rcp_approx);
PTXSIM_ARITH_DISPATCH_APPROX(sqrt_approx);
PTXSIM_ARITH_DISPATCH_APPROX(rsqrt_approx);
PTXSIM_ARITH_DISPATCH_APPROX(sin_approx);
PTXSIM_ARITH_DISPATCH_APPROX(cos_approx);
PTXSIM_ARITH_DISPATCH_APPROX(lg2_approx);
PTXSIM_ARITH_DISPATCH_APPROX(ex2_approx);
PTXSIM_ARITH_DISPATCH_APPROX(tanh_approx);
#undef PTXSIM_ARITH_DISPATCH_APPROX
std::expected<result<float16_t, floating_status>, arithmetic_error> tanh_approx(
    float16_t, special_function_control);
std::expected<result<bfloat16_t, floating_status>, arithmetic_error> tanh_approx(
    bfloat16_t, special_function_control);
std::expected<result<float16_t, floating_status>, arithmetic_error> ex2_approx(
    float16_t, special_function_control);
std::expected<result<bfloat16_t, floating_status>, arithmetic_error> ex2_approx(
    bfloat16_t, special_function_control);

// Exact native conversions.  Low-precision formats intentionally enter/leave
// via F32 only where that is their defined canonical conversion.
#define PTXSIM_ARITH_DISPATCH_TO(To, name, From)                           \
  std::expected<result<To, floating_status>, arithmetic_error> name(      \
      From, conversion_control)
PTXSIM_ARITH_DISPATCH_TO(float32_t, to_f32, float16_t);
PTXSIM_ARITH_DISPATCH_TO(float32_t, to_f32, float64_t);
PTXSIM_ARITH_DISPATCH_TO(float64_t, to_f64, float32_t);
PTXSIM_ARITH_DISPATCH_TO(float64_t, to_f64, float16_t);
PTXSIM_ARITH_DISPATCH_TO(float16_t, to_f16, float32_t);
PTXSIM_ARITH_DISPATCH_TO(float16_t, to_f16, float64_t);
#define PTXSIM_ARITH_DISPATCH_LOW(T, name) \
  PTXSIM_ARITH_DISPATCH_TO(float32_t, to_f32, T); \
  PTXSIM_ARITH_DISPATCH_TO(T, name, float32_t)
PTXSIM_ARITH_DISPATCH_LOW(bfloat16_t, to_bf16);
PTXSIM_ARITH_DISPATCH_LOW(float8_e4m3_t, to_f8e4m3);
PTXSIM_ARITH_DISPATCH_LOW(float8_e5m2_t, to_f8e5m2);
PTXSIM_ARITH_DISPATCH_LOW(float6_e2m3_t, to_f6e2m3);
PTXSIM_ARITH_DISPATCH_LOW(float6_e3m2_t, to_f6e3m2);
PTXSIM_ARITH_DISPATCH_LOW(float4_e2m1_t, to_f4e2m1);
PTXSIM_ARITH_DISPATCH_LOW(ufloat8_e8m0_t, to_ue8m0);
PTXSIM_ARITH_DISPATCH_LOW(ufloat7_e4m3_t, to_ue4m3);
#undef PTXSIM_ARITH_DISPATCH_LOW
#undef PTXSIM_ARITH_DISPATCH_TO

#define PTXSIM_ARITH_DISPATCH_INT(T, to_f32_name, to_f64_name, from_f32_name, from_f64_name) \
  std::expected<result<float32_t, floating_status>, arithmetic_error> to_f32_name(T, conversion_control); \
  std::expected<result<float64_t, floating_status>, arithmetic_error> to_f64_name(T, conversion_control); \
  std::expected<result<T, floating_status>, arithmetic_error> from_f32_name(float32_t, conversion_control); \
  std::expected<result<T, floating_status>, arithmetic_error> from_f64_name(float64_t, conversion_control)
PTXSIM_ARITH_DISPATCH_INT(std::int32_t, i32_to_f32, i32_to_f64, f32_to_i32, f64_to_i32);
PTXSIM_ARITH_DISPATCH_INT(std::uint32_t, u32_to_f32, u32_to_f64, f32_to_u32, f64_to_u32);
PTXSIM_ARITH_DISPATCH_INT(std::int64_t, i64_to_f32, i64_to_f64, f32_to_i64, f64_to_i64);
PTXSIM_ARITH_DISPATCH_INT(std::uint64_t, u64_to_f32, u64_to_f64, f32_to_u64, f64_to_u64);
#undef PTXSIM_ARITH_DISPATCH_INT

}  // namespace ptxsim::arith::detail::dispatch
