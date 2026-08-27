#pragma once

#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

#include <cstdint>

namespace ptxsim::fp {

class Environment {
 public:
  //
  // Binary32
  //

  [[nodiscard]]
  Result<Fp32> add(Fp32 lhs, Fp32 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp32> sub(Fp32 lhs, Fp32 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp32> mul(Fp32 lhs, Fp32 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp32> fma(Fp32 a, Fp32 b, Fp32 c,
                   ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp32> div(Fp32 lhs, Fp32 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp32> sqrt(Fp32 value, ArithmeticControl control = {}) const;

  //
  // Binary64
  //

  [[nodiscard]]
  Result<Fp64> add(Fp64 lhs, Fp64 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp64> sub(Fp64 lhs, Fp64 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp64> mul(Fp64 lhs, Fp64 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp64> fma(Fp64 a, Fp64 b, Fp64 c,
                   ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp64> div(Fp64 lhs, Fp64 rhs, ArithmeticControl control = {}) const;

  [[nodiscard]]
  Result<Fp64> sqrt(Fp64 value, ArithmeticControl control = {}) const;

  [[nodiscard]] Result<Fp32> i32_to_f32(std::int32_t value,
                                        RoundingMode rounding) const;
  [[nodiscard]] Result<std::int32_t> f32_to_i32(Fp32 value,
                                                RoundingMode rounding) const;
  [[nodiscard]] Result<Fp32> u32_to_f32(std::uint32_t value,
                                        RoundingMode rounding) const;
  [[nodiscard]] Result<std::uint32_t> f32_to_u32(Fp32 value,
                                                 RoundingMode rounding) const;
  [[nodiscard]] Result<Fp64> i32_to_f64(std::int32_t value,
                                        RoundingMode rounding) const;
  [[nodiscard]] Result<std::int32_t> f64_to_i32(Fp64 value,
                                                RoundingMode rounding) const;
  [[nodiscard]] Result<Fp64> u32_to_f64(std::uint32_t value,
                                        RoundingMode rounding) const;
  [[nodiscard]] Result<std::uint32_t> f64_to_u32(Fp64 value,
                                                 RoundingMode rounding) const;
  [[nodiscard]] Result<Fp64> f32_to_f64(Fp32 value) const;
  [[nodiscard]] Result<Fp32> f64_to_f32(Fp64 value,
                                        RoundingMode rounding) const;
};

}  // namespace ptxsim::fp
