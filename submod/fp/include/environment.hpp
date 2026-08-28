#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/detail/conversion_traits.hpp>
#include <ptxsim/fp/exceptions.hpp>
#include <ptxsim/fp/types.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace ptxsim::fp {

class Environment {
 public:
  // IEEE binary16.  PTX scalar f16 arithmetic permits .rn only.
  [[nodiscard]] Result<Fp16> add(Fp16, Fp16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp16> sub(Fp16, Fp16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp16> mul(Fp16, Fp16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp16> fma(Fp16, Fp16, Fp16,
                                 ArithmeticControl = {}) const;

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
  [[nodiscard]] Result<Fp32> mad(Fp32, Fp32, Fp32,
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> rcp(Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> div_approx(Fp32, Fp32,
                                        ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> div_full(Fp32, Fp32,
                                      ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> rcp_approx(Fp32, ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> sqrt_approx(Fp32, ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> rsqrt_approx(Fp32,
                                          ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> sin_approx(Fp32, ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> cos_approx(Fp32, ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> lg2_approx(Fp32, ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> ex2_approx(Fp32, ApproximationControl = {}) const;
  [[nodiscard]] Result<Fp32> tanh_approx(Fp32) const;

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
  [[nodiscard]] Result<Fp64> mad(Fp64, Fp64, Fp64,
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp64> rcp(Fp64, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp64> rsqrt_approx(Fp64) const;
  [[nodiscard]] Result<Fp64> rcp_approx_ftz(Fp64) const;
  [[nodiscard]] Result<Fp64> rsqrt_approx_ftz(Fp64) const;

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

  // PTX mixed-precision scalar operations.  The low-precision operands are
  // exactly widened and the operation rounds once to binary32.
  [[nodiscard]] Result<Fp32> add(Fp16, Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> sub(Fp16, Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> fma(Fp16, Fp16, Fp32,
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> add(Bf16, Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> sub(Bf16, Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> fma(Bf16, Bf16, Fp32,
                                 ArithmeticControl = {}) const;

  [[nodiscard]] Result<Fp16> abs(Fp16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Bf16> abs(Bf16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> abs(Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp64> abs(Fp64, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp16> neg(Fp16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Bf16> neg(Bf16, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> neg(Fp32, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp64> neg(Fp64, ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp16> min(Fp16, Fp16, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Bf16> min(Bf16, Bf16, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> min(Fp32, Fp32, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> min(Fp32, Fp32, Fp32, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp64> min(Fp64, Fp64, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp16> max(Fp16, Fp16, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Bf16> max(Bf16, Bf16, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> max(Fp32, Fp32, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp32> max(Fp32, Fp32, Fp32, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<Fp64> max(Fp64, Fp64, MinMaxControl = {},
                                 ArithmeticControl = {}) const;
  [[nodiscard]] Result<bool> compare(Fp16, Fp16, CompareOp,
                                     ArithmeticControl = {}) const;
  [[nodiscard]] Result<bool> compare(Bf16, Bf16, CompareOp,
                                     ArithmeticControl = {}) const;
  [[nodiscard]] Result<bool> compare(Fp32, Fp32, CompareOp,
                                     ArithmeticControl = {}) const;
  [[nodiscard]] Result<bool> compare(Fp64, Fp64, CompareOp,
                                     ArithmeticControl = {}) const;
  // PTX operand order: copy sign of the first argument into the magnitude of
  // the second argument.
  [[nodiscard]] Result<Fp32> copysign(Fp32 sign, Fp32 magnitude) const;
  [[nodiscard]] Result<Fp64> copysign(Fp64 sign, Fp64 magnitude) const;
  [[nodiscard]] Result<bool> testp(Fp32, TestpOp) const;
  [[nodiscard]] Result<bool> testp(Fp64, TestpOp) const;
  [[nodiscard]] Result<Fp16> tanh_approx(Fp16) const;
  [[nodiscard]] Result<Fp16> ex2_approx(Fp16) const;
  [[nodiscard]] Result<Bf16> tanh_approx(Bf16) const;
  [[nodiscard]] Result<Bf16> ex2_approx(Bf16) const;

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
  [[nodiscard]] Result<Fp16> f32_to_f16(Fp32 value,
                                        RoundingMode rounding) const;
  [[nodiscard]] Result<Fp32> f16_to_f32(Fp16 value) const;
  [[nodiscard]] Result<Fp16> f64_to_f16(Fp64 value,
                                        RoundingMode rounding) const;
  [[nodiscard]] Result<Fp64> f16_to_f64(Fp16 value) const;

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
  [[nodiscard]] Result<Fp16> convert_impl(std::type_identity<Fp16>, Fp32,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp32> convert_impl(std::type_identity<Fp32>, Fp16,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp16> convert_impl(std::type_identity<Fp16>, Fp64,
                                          ConversionControl) const;
  [[nodiscard]] Result<Fp64> convert_impl(std::type_identity<Fp64>, Fp16,
                                          ConversionControl) const;
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

template <typename T>
concept SaturateFormat = std::same_as<T, Fp16> || std::same_as<T, Fp32>;

template <SaturateFormat T>
[[nodiscard]] constexpr Result<T> saturate(Result<T> result) noexcept {
  using Traits = FormatTraits<T>;
  using Bits = typename Traits::Bits;
  constexpr unsigned fraction_lsb = std::countr_zero(Traits::fraction_mask);
  constexpr T one{static_cast<Bits>(static_cast<Bits>(Traits::exponent_bias)
                                    << (Traits::fraction_bits + fraction_lsb))};
  if (is_nan(result.value) || is_negative(result.value))
    result.value = {};
  else if (normalize_encoding(result.value).bits > one.bits)
    result.value = one;
  return result;
}

template <typename T>
concept ReluFormat = std::same_as<T, Fp16> || std::same_as<T, Bf16>;

template <ReluFormat T>
[[nodiscard]] constexpr Result<T> relu(Result<T> result) noexcept {
  using Traits = FormatTraits<T>;
  using Bits = typename Traits::Bits;
  constexpr unsigned fraction_lsb = std::countr_zero(Traits::fraction_mask);
  if (is_nan(result.value)) {
    result.value = T{static_cast<Bits>(
        (static_cast<Bits>(Traits::canonical_nan_exponent_field)
         << (Traits::fraction_bits + fraction_lsb)) |
        (static_cast<Bits>(Traits::canonical_nan_fraction_field)
         << fraction_lsb))};
  } else if (is_negative(result.value)) {
    result.value = {};
  }
  return result;
}

}  // namespace ptxsim::fp
