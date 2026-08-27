#include <ptxsim/fp/environment.hpp>

#include "detail/low_precision_backend.hpp"
#include "detail/softfloat_backend.hpp"

namespace ptxsim::fp {

#define PTXSIM_DELEGATE_BINARY(Type, Name)                          \
  Result<Type> Environment::Name(Type lhs, Type rhs,                \
                                 ArithmeticControl control) const { \
    return detail::SoftFloatBackend<Type>::Name(lhs, rhs, control); \
  }
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
Result<Fp64> Environment::fma(Fp64 a, Fp64 b, Fp64 c,
                              ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp64>::fma(a, b, c, control);
}
Result<Fp32> Environment::sqrt(Fp32 value, ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp32>::sqrt(value, control);
}
Result<Fp64> Environment::sqrt(Fp64 value, ArithmeticControl control) const {
  return detail::SoftFloatBackend<Fp64>::sqrt(value, control);
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
