#include "softfloat_backend.hpp"

#include "low_precision_backend.hpp"
#include "nan_policy.hpp"
#include "operation_policy.hpp"
#include "softfloat_context.hpp"

extern "C" {
#include <softfloat/softfloat.h>
}

namespace ptxsim::arith::detail {
namespace {

::float32_t sf(float32_t value) noexcept {
  return ::float32_t{.v = value.bits()};
}
::float16_t sf(float16_t value) noexcept {
  return ::float16_t{.v = value.bits()};
}
::float64_t sf(float64_t value) noexcept {
  return ::float64_t{.v = value.bits()};
}
float32_t fp(::float32_t value) noexcept {
  return float32_t::from_bits(value.v);
}
float16_t fp(::float16_t value) noexcept {
  return float16_t::from_bits(value.v);
}
float64_t fp(::float64_t value) noexcept {
  return float64_t::from_bits(value.v);
}

template <typename T, typename Function>
Result<T> execute(RoundingMode rounding, Function function) {
  validate_rounding(rounding);
  SoftFloatContext context{rounding};
  const T value = function();
  return {value, context.flags()};
}

float32_t input(float32_t value, ArithmeticControl control) noexcept {
  return resolve_subnormal(control) == SubnormalMode::FlushToSignedZero
             ? flush_subnormal(value)
             : value;
}

float16_t input(float16_t value, ArithmeticControl control) noexcept {
  return resolve_subnormal(control) == SubnormalMode::FlushToSignedZero
             ? flush_subnormal(value)
             : value;
}

Result<float32_t> output(Result<float32_t> result,
                         ArithmeticControl control) noexcept {
  if (resolve_subnormal(control) == SubnormalMode::FlushToSignedZero)
    result.value = flush_subnormal(result.value);
  return result;
}

Result<float16_t> output(Result<float16_t> result,
                         ArithmeticControl control) noexcept {
  if (resolve_subnormal(control) == SubnormalMode::FlushToSignedZero)
    result.value = flush_subnormal(result.value);
  return result;
}

}  // namespace

#define PTXSIM_F16_BINARY(Name, Op)                                           \
  Result<float16_t> SoftFloatBackend<float16_t>::Name(                        \
      float16_t lhs, float16_t rhs, ArithmeticControl control) {              \
    validate_control<Operation::Op, float16_t>(control);                      \
    lhs = input(lhs, control);                                                \
    rhs = input(rhs, control);                                                \
    if (is_nan(lhs) || is_nan(rhs))                                           \
      return propagate_nan(lhs, rhs);                                         \
    return output(                                                            \
        execute<float16_t>(control.rounding,                                  \
                           [=] { return fp(f16_##Name(sf(lhs), sf(rhs))); }), \
        control);                                                             \
  }
PTXSIM_F16_BINARY(add, Add)
PTXSIM_F16_BINARY(sub, Sub)
PTXSIM_F16_BINARY(mul, Mul)
#undef PTXSIM_F16_BINARY

Result<float16_t> SoftFloatBackend<float16_t>::fma(float16_t a, float16_t b,
                                                   float16_t c,
                                                   ArithmeticControl control) {
  validate_control<Operation::Fma, float16_t>(control);
  a = input(a, control);
  b = input(b, control);
  c = input(c, control);
  if (is_nan(a) || is_nan(b))
    return propagate_nan(a, b, c);
  if ((is_infinity(a) && is_zero(b)) || (is_zero(a) && is_infinity(b))) {
    if (is_nan(c))
      return canonical_invalid_nan<float16_t>();
  } else if (is_nan(c)) {
    return propagate_nan(a, b, c);
  }
  return output(
      execute<float16_t>(control.rounding,
                         [=] { return fp(f16_mulAdd(sf(a), sf(b), sf(c))); }),
      control);
}

#define PTXSIM_F32_BINARY(Name, Op)                                           \
  Result<float32_t> SoftFloatBackend<float32_t>::Name(                        \
      float32_t lhs, float32_t rhs, ArithmeticControl control) {              \
    validate_control<Operation::Op, float32_t>(control);                      \
    lhs = input(lhs, control);                                                \
    rhs = input(rhs, control);                                                \
    if (is_nan(lhs) || is_nan(rhs))                                           \
      return propagate_nan(lhs, rhs);                                         \
    return output(                                                            \
        execute<float32_t>(control.rounding,                                  \
                           [=] { return fp(f32_##Name(sf(lhs), sf(rhs))); }), \
        control);                                                             \
  }
PTXSIM_F32_BINARY(add, Add)
PTXSIM_F32_BINARY(sub, Sub)
PTXSIM_F32_BINARY(mul, Mul)
PTXSIM_F32_BINARY(div, Div)
#undef PTXSIM_F32_BINARY

Result<float32_t> SoftFloatBackend<float32_t>::fma(float32_t a, float32_t b,
                                                   float32_t c,
                                                   ArithmeticControl control) {
  validate_control<Operation::Fma, float32_t>(control);
  a = input(a, control);
  b = input(b, control);
  c = input(c, control);
  if (is_nan(a) || is_nan(b))
    return propagate_nan(a, b, c);
  if ((is_infinity(a) && is_zero(b)) || (is_zero(a) && is_infinity(b))) {
    if (is_nan(c))
      return canonical_invalid_nan<float32_t>();
  } else if (is_nan(c)) {
    return propagate_nan(a, b, c);
  }
  return output(
      execute<float32_t>(control.rounding,
                         [=] { return fp(f32_mulAdd(sf(a), sf(b), sf(c))); }),
      control);
}

Result<float32_t> SoftFloatBackend<float32_t>::mad(float32_t a, float32_t b,
                                                   float32_t c,
                                                   ArithmeticControl control) {
  validate_control<Operation::Mad, float32_t>(control);
  return fma(a, b, c, control);
}

Result<float32_t> SoftFloatBackend<float32_t>::rcp(float32_t value,
                                                   ArithmeticControl control) {
  validate_control<Operation::Rcp, float32_t>(control);
  return div(float32_t::from_bits(0x3F800000u), value, control);
}

Result<float32_t> SoftFloatBackend<float32_t>::sqrt(float32_t value,
                                                    ArithmeticControl control) {
  validate_control<Operation::Sqrt, float32_t>(control);
  value = input(value, control);
  if (is_nan(value))
    return propagate_nan(value);
  return output(execute<float32_t>(control.rounding,
                                   [=] { return fp(f32_sqrt(sf(value))); }),
                control);
}

#define PTXSIM_F64_BINARY(Name, Op)                                          \
  Result<float64_t> SoftFloatBackend<float64_t>::Name(                       \
      float64_t lhs, float64_t rhs, ArithmeticControl control) {             \
    validate_control<Operation::Op, float64_t>(control);                     \
    if (is_nan(lhs) || is_nan(rhs))                                          \
      return propagate_nan(lhs, rhs);                                        \
    return execute<float64_t>(                                               \
        control.rounding, [=] { return fp(f64_##Name(sf(lhs), sf(rhs))); }); \
  }
PTXSIM_F64_BINARY(add, Add)
PTXSIM_F64_BINARY(sub, Sub)
PTXSIM_F64_BINARY(mul, Mul)
PTXSIM_F64_BINARY(div, Div)
#undef PTXSIM_F64_BINARY

Result<float64_t> SoftFloatBackend<float64_t>::fma(float64_t a, float64_t b,
                                                   float64_t c,
                                                   ArithmeticControl control) {
  validate_control<Operation::Fma, float64_t>(control);
  if (is_nan(a) || is_nan(b))
    return propagate_nan(a, b, c);
  if ((is_infinity(a) && is_zero(b)) || (is_zero(a) && is_infinity(b))) {
    if (is_nan(c))
      return canonical_invalid_nan<float64_t>();
  } else if (is_nan(c)) {
    return propagate_nan(a, b, c);
  }
  return execute<float64_t>(
      control.rounding, [=] { return fp(f64_mulAdd(sf(a), sf(b), sf(c))); });
}
Result<float64_t> SoftFloatBackend<float64_t>::sqrt(float64_t value,
                                                    ArithmeticControl control) {
  validate_control<Operation::Sqrt, float64_t>(control);
  if (is_nan(value))
    return propagate_nan(value);
  return execute<float64_t>(control.rounding,
                            [=] { return fp(f64_sqrt(sf(value))); });
}

Result<float64_t> SoftFloatBackend<float64_t>::mad(float64_t a, float64_t b,
                                                   float64_t c,
                                                   ArithmeticControl control) {
  validate_control<Operation::Mad, float64_t>(control);
  return fma(a, b, c, control);
}

Result<float64_t> SoftFloatBackend<float64_t>::rcp(float64_t value,
                                                   ArithmeticControl control) {
  validate_control<Operation::Rcp, float64_t>(control);
  return div(float64_t::from_bits(0x3FF0000000000000ULL), value, control);
}

Result<float32_t> i32_to_f32(std::int32_t value, RoundingMode rounding) {
  return execute<float32_t>(rounding, [=] { return fp(::i32_to_f32(value)); });
}
Result<std::int32_t> f32_to_i32(float32_t value, RoundingMode rounding) {
  return execute<std::int32_t>(rounding, [=] {
    return static_cast<std::int32_t>(
        ::f32_to_i32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float32_t> u32_to_f32(std::uint32_t value, RoundingMode rounding) {
  return execute<float32_t>(rounding, [=] { return fp(ui32_to_f32(value)); });
}
Result<std::uint32_t> f32_to_u32(float32_t value, RoundingMode rounding) {
  return execute<std::uint32_t>(rounding, [=] {
    return static_cast<std::uint32_t>(
        f32_to_ui32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float32_t> i64_to_f32(std::int64_t value, RoundingMode rounding) {
  return execute<float32_t>(rounding, [=] { return fp(::i64_to_f32(value)); });
}
Result<std::int64_t> f32_to_i64(float32_t value, RoundingMode rounding) {
  return execute<std::int64_t>(rounding, [=] {
    return static_cast<std::int64_t>(
        ::f32_to_i64(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float32_t> u64_to_f32(std::uint64_t value, RoundingMode rounding) {
  return execute<float32_t>(rounding, [=] { return fp(::ui64_to_f32(value)); });
}
Result<std::uint64_t> f32_to_u64(float32_t value, RoundingMode rounding) {
  return execute<std::uint64_t>(rounding, [=] {
    return static_cast<std::uint64_t>(
        ::f32_to_ui64(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float64_t> i32_to_f64(std::int32_t value, RoundingMode rounding) {
  return execute<float64_t>(rounding, [=] { return fp(::i32_to_f64(value)); });
}
Result<std::int32_t> f64_to_i32(float64_t value, RoundingMode rounding) {
  return execute<std::int32_t>(rounding, [=] {
    return static_cast<std::int32_t>(
        ::f64_to_i32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float64_t> u32_to_f64(std::uint32_t value, RoundingMode rounding) {
  return execute<float64_t>(rounding, [=] { return fp(ui32_to_f64(value)); });
}
Result<std::uint32_t> f64_to_u32(float64_t value, RoundingMode rounding) {
  return execute<std::uint32_t>(rounding, [=] {
    return static_cast<std::uint32_t>(
        f64_to_ui32(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float64_t> i64_to_f64(std::int64_t value, RoundingMode rounding) {
  return execute<float64_t>(rounding, [=] { return fp(::i64_to_f64(value)); });
}
Result<std::int64_t> f64_to_i64(float64_t value, RoundingMode rounding) {
  return execute<std::int64_t>(rounding, [=] {
    return static_cast<std::int64_t>(
        ::f64_to_i64(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float64_t> u64_to_f64(std::uint64_t value, RoundingMode rounding) {
  return execute<float64_t>(rounding, [=] { return fp(::ui64_to_f64(value)); });
}
Result<std::uint64_t> f64_to_u64(float64_t value, RoundingMode rounding) {
  return execute<std::uint64_t>(rounding, [=] {
    return static_cast<std::uint64_t>(
        ::f64_to_ui64(sf(value), to_softfloat_rounding_mode(rounding), true));
  });
}
Result<float64_t> f32_to_f64(float32_t value) {
  return execute<float64_t>(RoundingMode::NearestEven,
                            [=] { return fp(::f32_to_f64(sf(value))); });
}
Result<float32_t> f64_to_f32(float64_t value, RoundingMode rounding) {
  return execute<float32_t>(rounding,
                            [=] { return fp(::f64_to_f32(sf(value))); });
}

Result<float16_t> f32_to_f16(float32_t value, RoundingMode rounding) {
  return execute<float16_t>(rounding,
                            [=] { return fp(::f32_to_f16(sf(value))); });
}
Result<float32_t> f16_to_f32(float16_t value) {
  return execute<float32_t>(RoundingMode::NearestEven,
                            [=] { return fp(::f16_to_f32(sf(value))); });
}
Result<float16_t> f64_to_f16(float64_t value, RoundingMode rounding) {
  return execute<float16_t>(rounding,
                            [=] { return fp(::f64_to_f16(sf(value))); });
}
Result<float64_t> f16_to_f64(float16_t value) {
  return execute<float64_t>(RoundingMode::NearestEven,
                            [=] { return fp(::f16_to_f64(sf(value))); });
}

namespace {

Result<float32_t> widen_for_mixed(float16_t value) {
  return f16_to_f32(value);
}
Result<float32_t> widen_for_mixed(bfloat16_t value) {
  return widen_to_f32(value, {});
}

template <scalar_operation Op, typename T>
Result<float32_t> mixed_add(T low, float32_t high, ArithmeticControl control) {
  validate_mixed_control<Op, float32_t, T, float32_t>(control);
  const auto widened = widen_for_mixed(low);
  auto result = Op == scalar_operation::sub
                    ? SoftFloatBackend<float32_t>::sub(widened.value, high,
                                                        control)
                    : SoftFloatBackend<float32_t>::add(widened.value, high,
                                                        control);
  result.flags |= widened.flags;
  return result;
}

template <typename T>
Result<float32_t> mixed_fma(T a, T b, float32_t c, ArithmeticControl control) {
  validate_mixed_control<scalar_operation::fma, float32_t, T, T, float32_t>(
      control);
  const auto widened_a = widen_for_mixed(a);
  const auto widened_b = widen_for_mixed(b);
  auto result = SoftFloatBackend<float32_t>::fma(widened_a.value,
                                                 widened_b.value, c, control);
  result.flags |= widened_a.flags;
  result.flags |= widened_b.flags;
  return result;
}

}  // namespace

Result<float32_t> add(float16_t low, float32_t high,
                      ArithmeticControl control) {
  return mixed_add<scalar_operation::add>(low, high, control);
}
Result<float32_t> sub(float16_t low, float32_t high,
                      ArithmeticControl control) {
  return mixed_add<scalar_operation::sub>(low, high, control);
}
Result<float32_t> fma(float16_t a, float16_t b, float32_t c,
                      ArithmeticControl control) {
  return mixed_fma(a, b, c, control);
}
Result<float32_t> add(bfloat16_t low, float32_t high,
                      ArithmeticControl control) {
  return mixed_add<scalar_operation::add>(low, high, control);
}
Result<float32_t> sub(bfloat16_t low, float32_t high,
                      ArithmeticControl control) {
  return mixed_add<scalar_operation::sub>(low, high, control);
}
Result<float32_t> fma(bfloat16_t a, bfloat16_t b, float32_t c,
                      ArithmeticControl control) {
  return mixed_fma(a, b, c, control);
}

}  // namespace ptxsim::arith::detail
