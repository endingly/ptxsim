#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/detail/conversion_traits.hpp>
#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

#include <cstdint>
#include <type_traits>

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

  // BF16 arithmetic uses a direct integer guard/round/sticky core.  FMA is
  // fused: the exact product and addend are rounded to BF16 only once.
  [[nodiscard]] Result<Bf16> add(Bf16 lhs, Bf16 rhs,
                                 ArithmeticControl control = {}) const;
  [[nodiscard]] Result<Bf16> sub(Bf16 lhs, Bf16 rhs,
                                 ArithmeticControl control = {}) const;
  [[nodiscard]] Result<Bf16> mul(Bf16 lhs, Bf16 rhs,
                                 ArithmeticControl control = {}) const;
  [[nodiscard]] Result<Bf16> fma(Bf16 a, Bf16 b, Bf16 c,
                                 ArithmeticControl control = {}) const;

  template <typename To, typename From>
    requires SupportedConversion<To, From>
  [[nodiscard]] Result<To> convert(From value,
                                   ConversionControl control = {}) const {
    return convert_impl(std::type_identity<To>{}, value, control);
  }

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

  [[nodiscard]] Result<Bf16> f32_to_bf16(Fp32 value,
                                         ConversionControl control = {}) const {
    return convert<Bf16>(value, control);
  }
  [[nodiscard]] Result<Fp32> bf16_to_f32(Bf16 value) const {
    return convert<Fp32>(value);
  }
  [[nodiscard]] Result<Tf32> f32_to_tf32(Fp32 value,
                                         ConversionControl control = {}) const {
    return convert<Tf32>(value, control);
  }
  [[nodiscard]] Result<Fp32> tf32_to_f32(Tf32 value) const {
    return convert<Fp32>(value);
  }

 private:
  [[nodiscard]] Result<Bf16> convert_impl(std::type_identity<Bf16>, Fp32,
                                          ConversionControl) const;
  [[nodiscard]] Result<Tf32> convert_impl(std::type_identity<Tf32>, Fp32,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp8E4M3> convert_impl(std::type_identity<Fp8E4M3>, Fp32,
                                             ConversionControl) const;
  [[nodiscard]] Result<Fp8E5M2> convert_impl(std::type_identity<Fp8E5M2>, Fp32,
                                             ConversionControl) const;
  [[nodiscard]] Result<Fp4E2M1> convert_impl(std::type_identity<Fp4E2M1>, Fp32,
                                             ConversionControl) const;
  [[nodiscard]] Result<Fp32> convert_impl(std::type_identity<Fp32>, Bf16,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp32> convert_impl(std::type_identity<Fp32>, Tf32,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp32> convert_impl(std::type_identity<Fp32>, Fp8E4M3,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp32> convert_impl(std::type_identity<Fp32>, Fp8E5M2,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp32> convert_impl(std::type_identity<Fp32>, Fp4E2M1,
                                          ConversionControl) const;
};

}  // namespace ptxsim::fp
