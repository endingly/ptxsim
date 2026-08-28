#include <ptxsim/fp/environment.hpp>

#include "detail/approximation_backend.hpp"
#include "detail/comparison_backend.hpp"
#include "detail/low_precision_backend.hpp"
#include "detail/softfloat_backend.hpp"

namespace ptxsim::fp {

#define PTXSIM_DELEGATE_BINARY(Type, Name)                          \
  Result<Type> Environment::Name(Type lhs, Type rhs,                \
                                 ArithmeticControl control) const { \
    return detail::SoftFloatBackend<Type>::Name(lhs, rhs, control); \
  }
#define PTXSIM_DELEGATE_F16_BINARY(Name)                            \
  Result<Fp16> Environment::Name(Fp16 lhs, Fp16 rhs,                \
                                 ArithmeticControl control) const { \
    return detail::SoftFloatBackend<Fp16>::Name(lhs, rhs, control); \
  }
PTXSIM_DELEGATE_F16_BINARY(add)
PTXSIM_DELEGATE_F16_BINARY(sub)
PTXSIM_DELEGATE_F16_BINARY(mul)
#undef PTXSIM_DELEGATE_F16_BINARY

PTXSIM_DELEGATE_BINARY(Fp32, add)
PTXSIM_DELEGATE_BINARY(Fp32, sub)
PTXSIM_DELEGATE_BINARY(Fp32, mul)
PTXSIM_DELEGATE_BINARY(Fp32, div)
PTXSIM_DELEGATE_BINARY(Fp64, add)
PTXSIM_DELEGATE_BINARY(Fp64, sub)
PTXSIM_DELEGATE_BINARY(Fp64, mul)
PTXSIM_DELEGATE_BINARY(Fp64, div)
#undef PTXSIM_DELEGATE_BINARY

Result<Fp32> Environment::fma(Fp32 a, Fp32 b, Fp32 c,
                              ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp32>::fma(a, b, c, control);
}
Result<Fp16> Environment::fma(Fp16 a, Fp16 b, Fp16 c,
                              ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp16>::fma(a, b, c, control);
}
Result<Fp64> Environment::fma(Fp64 a, Fp64 b, Fp64 c,
                              ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp64>::fma(a, b, c, control);
}
Result<Fp32> Environment::sqrt(Fp32 value, ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp32>::sqrt(value, control);
}
Result<Fp32> Environment::mad(Fp32 a, Fp32 b, Fp32 c,
                              ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp32>::mad(a, b, c, control);
}
Result<Fp32> Environment::rcp(Fp32 value, ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp32>::rcp(value, control);
}
Result<Fp32> Environment::div_approx(Fp32 lhs, Fp32 rhs,
                                     ApproximationControl control) const {
  return detail::div_approx(lhs, rhs, control);
}
Result<Fp32> Environment::div_full(Fp32 lhs, Fp32 rhs,
                                   ApproximationControl control) const {
  return detail::div_full(lhs, rhs, control);
}
Result<Fp32> Environment::rcp_approx(Fp32 value,
                                     ApproximationControl control) const {
  return detail::rcp_approx(value, control);
}
Result<Fp32> Environment::sqrt_approx(Fp32 value,
                                      ApproximationControl control) const {
  return detail::sqrt_approx(value, control);
}
Result<Fp32> Environment::rsqrt_approx(Fp32 value,
                                       ApproximationControl control) const {
  return detail::rsqrt_approx(value, control);
}
Result<Fp32> Environment::sin_approx(Fp32 value,
                                     ApproximationControl control) const {
  return detail::sin_approx(value, control);
}
Result<Fp32> Environment::cos_approx(Fp32 value,
                                     ApproximationControl control) const {
  return detail::cos_approx(value, control);
}
Result<Fp32> Environment::lg2_approx(Fp32 value,
                                     ApproximationControl control) const {
  return detail::lg2_approx(value, control);
}
Result<Fp32> Environment::ex2_approx(Fp32 value,
                                     ApproximationControl control) const {
  return detail::ex2_approx(value, control);
}
Result<Fp32> Environment::tanh_approx(Fp32 value) const {
  return detail::tanh_approx(value);
}
Result<Fp64> Environment::sqrt(Fp64 value, ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp64>::sqrt(value, control);
}
Result<Fp64> Environment::mad(Fp64 a, Fp64 b, Fp64 c,
                              ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp64>::mad(a, b, c, control);
}
Result<Fp64> Environment::rcp(Fp64 value, ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp64>::rcp(value, control);
}
Result<Fp64> Environment::rsqrt_approx(Fp64 value) const {
  return detail::rsqrt_approx(value);
}
Result<Fp64> Environment::rcp_approx_ftz(Fp64 value) const {
  return detail::rcp_approx_ftz(value);
}
Result<Fp64> Environment::rsqrt_approx_ftz(Fp64 value) const {
  return detail::rsqrt_approx_ftz(value);
}

Result<Bf16> Environment::add(Bf16 lhs, Bf16 rhs,
                              ArithmeticControl control) const {
  return detail::Bf16Backend::add(lhs, rhs, control);
}
Result<Bf16> Environment::sub(Bf16 lhs, Bf16 rhs,
                              ArithmeticControl control) const {
  return detail::Bf16Backend::sub(lhs, rhs, control);
}
Result<Bf16> Environment::mul(Bf16 lhs, Bf16 rhs,
                              ArithmeticControl control) const {
  return detail::Bf16Backend::mul(lhs, rhs, control);
}
Result<Bf16> Environment::fma(Bf16 a, Bf16 b, Bf16 c,
                              ArithmeticControl control) const {
  return detail::Bf16Backend::fma(a, b, c, control);
}

Result<Fp32> Environment::add(Fp16 low, Fp32 high,
                              ArithmeticControl control) const {
  return detail::add(low, high, control);
}
Result<Fp32> Environment::sub(Fp16 low, Fp32 high,
                              ArithmeticControl control) const {
  return detail::sub(low, high, control);
}
Result<Fp32> Environment::fma(Fp16 a, Fp16 b, Fp32 c,
                              ArithmeticControl control) const {
  return detail::fma(a, b, c, control);
}
Result<Fp32> Environment::add(Bf16 low, Fp32 high,
                              ArithmeticControl control) const {
  return detail::add(low, high, control);
}
Result<Fp32> Environment::sub(Bf16 low, Fp32 high,
                              ArithmeticControl control) const {
  return detail::sub(low, high, control);
}
Result<Fp32> Environment::fma(Bf16 a, Bf16 b, Fp32 c,
                              ArithmeticControl control) const {
  return detail::fma(a, b, c, control);
}

#define PTXSIM_DELEGATE_UNARY(Type, Name)                               \
  Result<Type> Environment::Name(Type value, ArithmeticControl control) \
      const {                                                           \
    return detail::comparison::Name(value, control);                    \
  }
PTXSIM_DELEGATE_UNARY(Fp16, abs)
PTXSIM_DELEGATE_UNARY(Bf16, abs)
PTXSIM_DELEGATE_UNARY(Fp32, abs)
PTXSIM_DELEGATE_UNARY(Fp64, abs)
PTXSIM_DELEGATE_UNARY(Fp16, neg)
PTXSIM_DELEGATE_UNARY(Bf16, neg)
PTXSIM_DELEGATE_UNARY(Fp32, neg)
PTXSIM_DELEGATE_UNARY(Fp64, neg)
#undef PTXSIM_DELEGATE_UNARY

#define PTXSIM_DELEGATE_MINMAX(Type, Name, SelectMin)                         \
  Result<Type> Environment::Name(Type lhs, Type rhs, MinMaxControl modifiers, \
                                 ArithmeticControl control) const {           \
    return detail::comparison::minmax(lhs, rhs, modifiers, control,           \
                                      SelectMin);                             \
  }
PTXSIM_DELEGATE_MINMAX(Fp16, min, true)
PTXSIM_DELEGATE_MINMAX(Bf16, min, true)
PTXSIM_DELEGATE_MINMAX(Fp32, min, true)
PTXSIM_DELEGATE_MINMAX(Fp64, min, true)
PTXSIM_DELEGATE_MINMAX(Fp16, max, false)
PTXSIM_DELEGATE_MINMAX(Bf16, max, false)
PTXSIM_DELEGATE_MINMAX(Fp32, max, false)
PTXSIM_DELEGATE_MINMAX(Fp64, max, false)
#undef PTXSIM_DELEGATE_MINMAX

Result<Fp32> Environment::min(Fp32 a, Fp32 b, Fp32 c, MinMaxControl modifiers,
                              ArithmeticControl control) const {
  return detail::comparison::minmax(a, b, c, modifiers, control, true);
}
Result<Fp32> Environment::max(Fp32 a, Fp32 b, Fp32 c, MinMaxControl modifiers,
                              ArithmeticControl control) const {
  return detail::comparison::minmax(a, b, c, modifiers, control, false);
}

#define PTXSIM_DELEGATE_COMPARE(Type)                                        \
  Result<bool> Environment::compare(Type lhs, Type rhs, CompareOp operation, \
                                    ArithmeticControl control) const {       \
    return detail::comparison::compare(lhs, rhs, operation, control);        \
  }
PTXSIM_DELEGATE_COMPARE(Fp16)
PTXSIM_DELEGATE_COMPARE(Bf16)
PTXSIM_DELEGATE_COMPARE(Fp32)
PTXSIM_DELEGATE_COMPARE(Fp64)
#undef PTXSIM_DELEGATE_COMPARE

Result<Fp32> Environment::copysign(Fp32 sign, Fp32 magnitude) const {
  return detail::comparison::copysign(sign, magnitude);
}
Result<Fp64> Environment::copysign(Fp64 sign, Fp64 magnitude) const {
  return detail::comparison::copysign(sign, magnitude);
}
Result<bool> Environment::testp(Fp32 value, TestpOp operation) const {
  return detail::comparison::testp(value, operation);
}
Result<bool> Environment::testp(Fp64 value, TestpOp operation) const {
  return detail::comparison::testp(value, operation);
}
Result<Fp16> Environment::tanh_approx(Fp16 value) const {
  return detail::tanh_approx(value);
}
Result<Fp16> Environment::ex2_approx(Fp16 value) const {
  return detail::ex2_approx(value);
}
Result<Bf16> Environment::tanh_approx(Bf16 value) const {
  return detail::tanh_approx(value);
}
Result<Bf16> Environment::ex2_approx(Bf16 value) const {
  return detail::ex2_approx(value);
}

Result<Fp32> Environment::i32_to_f32(std::int32_t value,
                                     RoundingMode rounding) const {
  return detail::i32_to_f32(value, rounding);
}
Result<std::int32_t> Environment::f32_to_i32(Fp32 value,
                                             RoundingMode rounding) const {
  return detail::f32_to_i32(value, rounding);
}
Result<Fp32> Environment::u32_to_f32(std::uint32_t value,
                                     RoundingMode rounding) const {
  return detail::u32_to_f32(value, rounding);
}
Result<std::uint32_t> Environment::f32_to_u32(Fp32 value,
                                              RoundingMode rounding) const {
  return detail::f32_to_u32(value, rounding);
}
Result<Fp64> Environment::i32_to_f64(std::int32_t value,
                                     RoundingMode rounding) const {
  return detail::i32_to_f64(value, rounding);
}
Result<std::int32_t> Environment::f64_to_i32(Fp64 value,
                                             RoundingMode rounding) const {
  return detail::f64_to_i32(value, rounding);
}
Result<Fp64> Environment::u32_to_f64(std::uint32_t value,
                                     RoundingMode rounding) const {
  return detail::u32_to_f64(value, rounding);
}
Result<std::uint32_t> Environment::f64_to_u32(Fp64 value,
                                              RoundingMode rounding) const {
  return detail::f64_to_u32(value, rounding);
}
Result<Fp64> Environment::f32_to_f64(Fp32 value) const {
  return detail::f32_to_f64(value);
}
Result<Fp32> Environment::f64_to_f32(Fp64 value, RoundingMode rounding) const {
  return detail::f64_to_f32(value, rounding);
}
Result<Fp16> Environment::f32_to_f16(Fp32 value, RoundingMode rounding) const {
  return detail::f32_to_f16(value, rounding);
}
Result<Fp32> Environment::f16_to_f32(Fp16 value) const {
  return detail::f16_to_f32(value);
}
Result<Fp16> Environment::f64_to_f16(Fp64 value, RoundingMode rounding) const {
  return detail::f64_to_f16(value, rounding);
}
Result<Fp64> Environment::f16_to_f64(Fp16 value) const {
  return detail::f16_to_f64(value);
}

Result<Fp16> Environment::convert_impl(std::type_identity<Fp16>, Fp32 value,
                                       ConversionControl control) const {
  if (control.satfinite)
    throw std::invalid_argument(
        "f32-to-f16 conversion does not support satfinite");
  return detail::f32_to_f16(value, control.rounding);
}
Result<Fp32> Environment::convert_impl(std::type_identity<Fp32>, Fp16 value,
                                       ConversionControl control) const {
  detail::validate_exact_widening_control(control);
  return detail::f16_to_f32(value);
}
Result<Fp16> Environment::convert_impl(std::type_identity<Fp16>, Fp64 value,
                                       ConversionControl control) const {
  if (control.satfinite)
    throw std::invalid_argument(
        "f64-to-f16 conversion does not support satfinite");
  return detail::f64_to_f16(value, control.rounding);
}
Result<Fp64> Environment::convert_impl(std::type_identity<Fp64>, Fp16 value,
                                       ConversionControl control) const {
  detail::validate_exact_widening_control(control);
  return detail::f16_to_f64(value);
}

#define PTXSIM_NARROW(Type)                                                    \
  Result<Type> Environment::convert_impl(std::type_identity<Type>, Fp32 value, \
                                         ConversionControl control) const {    \
    return detail::narrow_from_f32<Type>(value, control);                      \
  }
PTXSIM_NARROW(Bf16)
PTXSIM_NARROW(Tf32)
PTXSIM_NARROW(Fp8E4M3)
PTXSIM_NARROW(Fp8E5M2)
PTXSIM_NARROW(Fp4E2M1)
#undef PTXSIM_NARROW

#define PTXSIM_WIDEN(Type)                                                     \
  Result<Fp32> Environment::convert_impl(std::type_identity<Fp32>, Type value, \
                                         ConversionControl control) const {    \
    return detail::widen_to_f32<Type>(value, control);                         \
  }
PTXSIM_WIDEN(Bf16)
PTXSIM_WIDEN(Tf32)
PTXSIM_WIDEN(Fp8E4M3)
PTXSIM_WIDEN(Fp8E5M2)
PTXSIM_WIDEN(Fp4E2M1)
#undef PTXSIM_WIDEN

}  // namespace ptxsim::fp
