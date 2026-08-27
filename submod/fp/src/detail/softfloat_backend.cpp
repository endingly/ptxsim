#include "softfloat_backend.hpp"

#include "nan_policy.hpp"
#include "operation_policy.hpp"
#include "softfloat_context.hpp"

extern "C" {
#include <softfloat/softfloat.h>
}

namespace ptxsim::fp::detail {
namespace {

float32_t sf(Fp32 value) noexcept {
  return float32_t{.v = value.bits};
}
float64_t sf(Fp64 value) noexcept {
  return float64_t{.v = value.bits};
}
Fp32 fp(float32_t value) noexcept {
  return Fp32{value.v};
}
Fp64 fp(float64_t value) noexcept {
  return Fp64{value.v};
}

template <typename T, typename Function>
Result<T> execute(RoundingMode rounding, Function function) {
  validate_rounding(rounding);
  SoftFloatContext context{rounding};
  const T value = function();
  return {value, context.flags()};
}

Fp32 input(Fp32 value, ArithmeticControl control) noexcept {
  return resolve_subnormal(control) == SubnormalMode::FlushToSignedZero
             ? flush_subnormal(value)
             : value;
}

Result<Fp32> output(Result<Fp32> result, ArithmeticControl control) noexcept {
  if (resolve_subnormal(control) == SubnormalMode::FlushToSignedZero)
    result.value = flush_subnormal(result.value);
  return result;
}

}  // namespace

#define PTXSIM_F32_BINARY(Name, Op)                                      \
  Result<Fp32> SoftFloatBackend<Fp32>::Name(Fp32 lhs, Fp32 rhs,          \
                                            ArithmeticControl control) { \
    validate_control<Operation::Op, Fp32>(control);                      \
    lhs = input(lhs, control);                                           \
    rhs = input(rhs, control);                                           \
    if (is_nan(lhs) || is_nan(rhs))                                      \
      return propagate_nan(lhs, rhs);                                    \
    return output(                                                       \
        execute<Fp32>(control.rounding,                                  \
                      [=] { return fp(f32_##Name(sf(lhs), sf(rhs))); }), \
        control);                                                        \
  }
PTXSIM_F32_BINARY(add, Add)
PTXSIM_F32_BINARY(sub, Sub)
PTXSIM_F32_BINARY(mul, Mul)
PTXSIM_F32_BINARY(div, Div)
#undef PTXSIM_F32_BINARY

Result<Fp32> SoftFloatBackend<Fp32>::fma(Fp32 a, Fp32 b, Fp32 c,
                                         ArithmeticControl control) {
  validate_control<Operation::Fma, Fp32>(control);
  a = input(a, control);
  b = input(b, control);
  c = input(c, control);
  if (is_nan(a) || is_nan(b))
    return propagate_nan(a, b, c);
  if ((is_infinity(a) && is_zero(b)) ||
      (is_zero(a) && is_infinity(b))) {
    if (is_nan(c))
      return canonical_invalid_nan<Fp32>();
  } else if (is_nan(c)) {
    return propagate_nan(a, b, c);
  }
  return output(
      execute<Fp32>(control.rounding,
                    [=] { return fp(f32_mulAdd(sf(a), sf(b), sf(c))); }),
      control);
}

Result<Fp32> SoftFloatBackend<Fp32>::sqrt(Fp32 value,
                                          ArithmeticControl control) {
  validate_control<Operation::Sqrt, Fp32>(control);
  value = input(value, control);
  if (is_nan(value))
    return propagate_nan(value);
  return output(
      execute<Fp32>(control.rounding, [=] { return fp(f32_sqrt(sf(value))); }),
      control);
}

#define PTXSIM_F64_BINARY(Name, Op)                                         \
  Result<Fp64> SoftFloatBackend<Fp64>::Name(Fp64 lhs, Fp64 rhs,             \
                                            ArithmeticControl control) {    \
    validate_control<Operation::Op, Fp64>(control);                         \
    if (is_nan(lhs) || is_nan(rhs))                                         \
      return propagate_nan(lhs, rhs);                                       \
    return execute<Fp64>(control.rounding,                                  \
                         [=] { return fp(f64_##Name(sf(lhs), sf(rhs))); }); \
  }
PTXSIM_F64_BINARY(add, Add)
PTXSIM_F64_BINARY(sub, Sub)
PTXSIM_F64_BINARY(mul, Mul)
PTXSIM_F64_BINARY(div, Div)
#undef PTXSIM_F64_BINARY

Result<Fp64> SoftFloatBackend<Fp64>::fma(Fp64 a, Fp64 b, Fp64 c,
                                         ArithmeticControl control) {
  validate_control<Operation::Fma, Fp64>(control);
  if (is_nan(a) || is_nan(b))
    return propagate_nan(a, b, c);
  if ((is_infinity(a) && is_zero(b)) ||
      (is_zero(a) && is_infinity(b))) {
    if (is_nan(c))
      return canonical_invalid_nan<Fp64>();
  } else if (is_nan(c)) {
    return propagate_nan(a, b, c);
  }
  return execute<Fp64>(control.rounding,
                       [=] { return fp(f64_mulAdd(sf(a), sf(b), sf(c))); });
}
Result<Fp64> SoftFloatBackend<Fp64>::sqrt(Fp64 value,
                                          ArithmeticControl control) {
  validate_control<Operation::Sqrt, Fp64>(control);
  if (is_nan(value))
    return propagate_nan(value);
  return execute<Fp64>(control.rounding,
                       [=] { return fp(f64_sqrt(sf(value))); });
}

Result<Fp32> i32_to_f32(std::int32_t value, RoundingMode rounding) {
  return execute<Fp32>(rounding, [=] { return fp(::i32_to_f32(value)); });
}
Result<std::int32_t> f32_to_i32(Fp32 value, RoundingMode rounding) {
  return execute<std::int32_t>(rounding, [=] {
    return static_cast<std::int32_t>(
        ::f32_to_i32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<Fp32> u32_to_f32(std::uint32_t value, RoundingMode rounding) {
  return execute<Fp32>(rounding, [=] { return fp(ui32_to_f32(value)); });
}
Result<std::uint32_t> f32_to_u32(Fp32 value, RoundingMode rounding) {
  return execute<std::uint32_t>(rounding, [=] {
    return static_cast<std::uint32_t>(
        f32_to_ui32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<Fp64> i32_to_f64(std::int32_t value, RoundingMode rounding) {
  return execute<Fp64>(rounding, [=] { return fp(::i32_to_f64(value)); });
}
Result<std::int32_t> f64_to_i32(Fp64 value, RoundingMode rounding) {
  return execute<std::int32_t>(rounding, [=] {
    return static_cast<std::int32_t>(
        ::f64_to_i32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<Fp64> u32_to_f64(std::uint32_t value, RoundingMode rounding) {
  return execute<Fp64>(rounding, [=] { return fp(ui32_to_f64(value)); });
}
Result<std::uint32_t> f64_to_u32(Fp64 value, RoundingMode rounding) {
  return execute<std::uint32_t>(rounding, [=] {
    return static_cast<std::uint32_t>(
        f64_to_ui32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<Fp64> f32_to_f64(Fp32 value) {
  return execute<Fp64>(RoundingMode::NearestEven,
                       [=] { return fp(::f32_to_f64(sf(value))); });
}
Result<Fp32> f64_to_f32(Fp64 value, RoundingMode rounding) {
  return execute<Fp32>(rounding, [=] { return fp(::f64_to_f32(sf(value))); });
}

}  // namespace ptxsim::fp::detail
