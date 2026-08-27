#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

#include <cstdint>

namespace ptxsim::fp::detail {

template <typename T>
struct SoftFloatBackend;

template <>
struct SoftFloatBackend<Fp32> {
  static Result<Fp32> add(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> sub(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> mul(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> fma(Fp32, Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> div(Fp32, Fp32, ArithmeticControl);
  static Result<Fp32> sqrt(Fp32, ArithmeticControl);
};

template <>
struct SoftFloatBackend<Fp64> {
  static Result<Fp64> add(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> sub(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> mul(Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> fma(Fp64, Fp64, Fp64, ArithmeticControl);
  static Result<Fp64> div(Fp64, Fp64, ArithmeticControl);
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

}  // namespace ptxsim::fp::detail
