#pragma once

#include <ptxsim/arith/controls.hpp>
#include "internal_controls.hpp"
#include "internal_result.hpp"
#include <ptxsim/arith/types.hpp>

#include <cstdint>

namespace ptxsim::arith::detail {

template <typename T>
struct SoftFloatBackend;

template <>
struct SoftFloatBackend<float16_t> {
  static Result<float16_t> add(float16_t, float16_t, ArithmeticControl);
  static Result<float16_t> sub(float16_t, float16_t, ArithmeticControl);
  static Result<float16_t> mul(float16_t, float16_t, ArithmeticControl);
  static Result<float16_t> fma(float16_t, float16_t, float16_t, ArithmeticControl);
};

template <>
struct SoftFloatBackend<float32_t> {
  static Result<float32_t> add(float32_t, float32_t, ArithmeticControl);
  static Result<float32_t> sub(float32_t, float32_t, ArithmeticControl);
  static Result<float32_t> mul(float32_t, float32_t, ArithmeticControl);
  static Result<float32_t> fma(float32_t, float32_t, float32_t, ArithmeticControl);
  static Result<float32_t> mad(float32_t, float32_t, float32_t, ArithmeticControl);
  static Result<float32_t> div(float32_t, float32_t, ArithmeticControl);
  static Result<float32_t> rcp(float32_t, ArithmeticControl);
  static Result<float32_t> sqrt(float32_t, ArithmeticControl);
};

template <>
struct SoftFloatBackend<float64_t> {
  static Result<float64_t> add(float64_t, float64_t, ArithmeticControl);
  static Result<float64_t> sub(float64_t, float64_t, ArithmeticControl);
  static Result<float64_t> mul(float64_t, float64_t, ArithmeticControl);
  static Result<float64_t> fma(float64_t, float64_t, float64_t, ArithmeticControl);
  static Result<float64_t> mad(float64_t, float64_t, float64_t, ArithmeticControl);
  static Result<float64_t> div(float64_t, float64_t, ArithmeticControl);
  static Result<float64_t> rcp(float64_t, ArithmeticControl);
  static Result<float64_t> sqrt(float64_t, ArithmeticControl);
};

Result<float32_t> i32_to_f32(std::int32_t, RoundingMode);
Result<std::int32_t> f32_to_i32(float32_t, RoundingMode);
Result<float32_t> u32_to_f32(std::uint32_t, RoundingMode);
Result<std::uint32_t> f32_to_u32(float32_t, RoundingMode);
Result<float32_t> i64_to_f32(std::int64_t, RoundingMode);
Result<std::int64_t> f32_to_i64(float32_t, RoundingMode);
Result<float32_t> u64_to_f32(std::uint64_t, RoundingMode);
Result<std::uint64_t> f32_to_u64(float32_t, RoundingMode);
Result<float64_t> i32_to_f64(std::int32_t, RoundingMode);
Result<std::int32_t> f64_to_i32(float64_t, RoundingMode);
Result<float64_t> u32_to_f64(std::uint32_t, RoundingMode);
Result<std::uint32_t> f64_to_u32(float64_t, RoundingMode);
Result<float64_t> i64_to_f64(std::int64_t, RoundingMode);
Result<std::int64_t> f64_to_i64(float64_t, RoundingMode);
Result<float64_t> u64_to_f64(std::uint64_t, RoundingMode);
Result<std::uint64_t> f64_to_u64(float64_t, RoundingMode);
Result<float64_t> f32_to_f64(float32_t);
Result<float32_t> f64_to_f32(float64_t, RoundingMode);
Result<float16_t> f32_to_f16(float32_t, RoundingMode);
Result<float32_t> f16_to_f32(float16_t);
Result<float16_t> f64_to_f16(float64_t, RoundingMode);
Result<float64_t> f16_to_f64(float16_t);

Result<float32_t> add(float16_t, float32_t, ArithmeticControl);
Result<float32_t> sub(float16_t, float32_t, ArithmeticControl);
Result<float32_t> fma(float16_t, float16_t, float32_t, ArithmeticControl);
Result<float32_t> add(bfloat16_t, float32_t, ArithmeticControl);
Result<float32_t> sub(bfloat16_t, float32_t, ArithmeticControl);
Result<float32_t> fma(bfloat16_t, bfloat16_t, float32_t, ArithmeticControl);

}  // namespace ptxsim::arith::detail
