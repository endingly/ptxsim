#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

#include <cstdint>

namespace ptxsim::fp::detail {

template <typename T>
struct SoftFloatBackend;

template <>
struct SoftFloatBackend<Fp16> {
  static Result<Fp16> add(Fp16, Fp16, ArithmeticControl);
  static Result<Fp16> sub(Fp16, Fp16, ArithmeticControl);
  static Result<Fp16> mul(Fp16, Fp16, ArithmeticControl);
  static Result<Fp16> fma(Fp16, Fp16, Fp16, ArithmeticControl);
};

template <>
struct SoftFloatBackend<Fp32> {
  static Result<Fp32> add(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> sub(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> mul(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> fma(Fp32, Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> mad(Fp32, Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> div(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> rcp(Fp32, ArithmeticControl);
  static Result<Fp32> sqrt(Fp32, ArithmeticControl);
};

template <>
struct SoftFloatBackend<Fp64> {
  static Result<Fp64> add(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> sub(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> mul(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> fma(Fp64, Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> mad(Fp64, Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> div(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> rcp(Fp64, ArithmeticControl);
  static Result<Fp64> sqrt(Fp64, ArithmeticControl);
};

Result<Fp32> i32_to_f32(std::int32_t, RoundingMode);
Result<std::int32_t> f32_to_i32(Fp32, RoundingMode);
Result<Fp32> u32_to_f32(std::uint32_t, RoundingMode);
Result<std::uint32_t> f32_to_u32(Fp32, RoundingMode);
Result<Fp64> i32_to_f64(std::int32_t, RoundingMode);
Result<std::int32_t> f64_to_i32(Fp64, RoundingMode);
Result<Fp64> u32_to_f64(std::uint32_t, RoundingMode);
Result<std::uint32_t> f64_to_u32(Fp64, RoundingMode);
Result<Fp64> f32_to_f64(Fp32);
Result<Fp32> f64_to_f32(Fp64, RoundingMode);
Result<Fp16> f32_to_f16(Fp32, RoundingMode);
Result<Fp32> f16_to_f32(Fp16);
Result<Fp16> f64_to_f16(Fp64, RoundingMode);
Result<Fp64> f16_to_f64(Fp16);

Result<Fp32> add(Fp16, Fp32, ArithmeticControl);
Result<Fp32> sub(Fp16, Fp32, ArithmeticControl);
Result<Fp32> fma(Fp16, Fp16, Fp32, ArithmeticControl);
Result<Fp32> add(Bf16, Fp32, ArithmeticControl);
Result<Fp32> sub(Bf16, Fp32, ArithmeticControl);
Result<Fp32> fma(Bf16, Bf16, Fp32, ArithmeticControl);

}  // namespace ptxsim::fp::detail
