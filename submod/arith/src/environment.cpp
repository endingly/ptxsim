#include "detail/backend.hpp"
#include <ptxsim/arith/concepts.hpp>
#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/detail/dispatch.hpp>
#include <ptxsim/arith/detail/format_traits.hpp>

#include "detail/approximation_backend.hpp"
#include "detail/comparison_backend.hpp"
#include "detail/low_precision_backend.hpp"
#include "detail/softfloat_backend.hpp"

namespace ptxsim::arith {

namespace detail {
struct tf32_factory {
  [[nodiscard]] static constexpr tfloat32_t make(float32_t value) noexcept {
    return tfloat32_t(value);
  }
};
}  // namespace detail

std::expected<bits32_t, arithmetic_error> encode(
    tfloat32_t value, const tf32_encoding_profile& profile) noexcept {
  if (profile.model != tf32_encoding_model::f32_top_19_bits)
    return std::unexpected(arithmetic_error::unsupported_operation);
  return value.canonical_value().bits() & 0xFFFFE000u;
}

std::expected<tfloat32_t, arithmetic_error> decode_tf32(
    bits32_t bits, const tf32_encoding_profile& profile) noexcept {
  if (profile.model != tf32_encoding_model::f32_top_19_bits)
    return std::unexpected(arithmetic_error::unsupported_operation);
  return detail::tf32_factory::make(float32_t::from_bits(bits & 0xFFFFE000u));
}

#define PTXSIM_DELEGATE_BINARY(Type, Name)                                \
  Result<Type> detail::backend::Name(Type lhs, Type rhs,                  \
                                     detail::ArithmeticControl control) { \
    return detail::SoftFloatBackend<Type>::Name(lhs, rhs, control);       \
  }
#define PTXSIM_DELEGATE_F16_BINARY(Name)                                       \
  Result<float16_t> detail::backend::Name(float16_t lhs, float16_t rhs,        \
                                          detail::ArithmeticControl control) { \
    return detail::SoftFloatBackend<float16_t>::Name(lhs, rhs, control);       \
  }
PTXSIM_DELEGATE_F16_BINARY(add)
PTXSIM_DELEGATE_F16_BINARY(sub)
PTXSIM_DELEGATE_F16_BINARY(mul)
#undef PTXSIM_DELEGATE_F16_BINARY

PTXSIM_DELEGATE_BINARY(float32_t, add)
PTXSIM_DELEGATE_BINARY(float32_t, sub)
PTXSIM_DELEGATE_BINARY(float32_t, mul)
PTXSIM_DELEGATE_BINARY(float32_t, div)
PTXSIM_DELEGATE_BINARY(float64_t, add)
PTXSIM_DELEGATE_BINARY(float64_t, sub)
PTXSIM_DELEGATE_BINARY(float64_t, mul)
PTXSIM_DELEGATE_BINARY(float64_t, div)
#undef PTXSIM_DELEGATE_BINARY

Result<float32_t> detail::backend::fma(float32_t a, float32_t b, float32_t c,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float32_t>::fma(a, b, c, control);
}
Result<float16_t> detail::backend::fma(float16_t a, float16_t b, float16_t c,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float16_t>::fma(a, b, c, control);
}
Result<float64_t> detail::backend::fma(float64_t a, float64_t b, float64_t c,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float64_t>::fma(a, b, c, control);
}
Result<float32_t> detail::backend::sqrt(float32_t value,
                                        detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float32_t>::sqrt(value, control);
}
Result<float32_t> detail::backend::mad(float32_t a, float32_t b, float32_t c,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float32_t>::mad(a, b, c, control);
}
Result<float32_t> detail::backend::rcp(float32_t value,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float32_t>::rcp(value, control);
}
Result<float32_t> detail::backend::div_approx(
    float32_t lhs, float32_t rhs, detail::ApproximationControl control) {
  return detail::div_approx(lhs, rhs, control);
}
Result<float32_t> detail::backend::div_full(
    float32_t lhs, float32_t rhs, detail::ApproximationControl control) {
  return detail::div_full(lhs, rhs, control);
}
Result<float32_t> detail::backend::rcp_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::rcp_approx(value, control);
}
Result<float32_t> detail::backend::sqrt_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::sqrt_approx(value, control);
}
Result<float32_t> detail::backend::rsqrt_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::rsqrt_approx(value, control);
}
Result<float32_t> detail::backend::sin_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::sin_approx(value, control);
}
Result<float32_t> detail::backend::cos_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::cos_approx(value, control);
}
Result<float32_t> detail::backend::lg2_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::lg2_approx(value, control);
}
Result<float32_t> detail::backend::ex2_approx(
    float32_t value, detail::ApproximationControl control) {
  return detail::ex2_approx(value, control);
}
Result<float32_t> detail::backend::tanh_approx(float32_t value,
                                                detail::ApproximationControl control) {
  return detail::tanh_approx(value, control);
}
Result<float64_t> detail::backend::sqrt(float64_t value,
                                        detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float64_t>::sqrt(value, control);
}
Result<float64_t> detail::backend::mad(float64_t a, float64_t b, float64_t c,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float64_t>::mad(a, b, c, control);
}
Result<float64_t> detail::backend::rcp(float64_t value,
                                       detail::ArithmeticControl control) {
  return detail::SoftFloatBackend<float64_t>::rcp(value, control);
}
Result<float64_t> detail::backend::rsqrt_approx(float64_t value) {
  return detail::rsqrt_approx(value);
}
Result<float64_t> detail::backend::rcp_approx_ftz(float64_t value) {
  return detail::rcp_approx_ftz(value);
}
Result<float64_t> detail::backend::rsqrt_approx_ftz(float64_t value) {
  return detail::rsqrt_approx_ftz(value);
}

Result<bfloat16_t> detail::backend::add(bfloat16_t lhs, bfloat16_t rhs,
                                        detail::ArithmeticControl control) {
  return detail::Bf16Backend::add(lhs, rhs, control);
}
Result<bfloat16_t> detail::backend::sub(bfloat16_t lhs, bfloat16_t rhs,
                                        detail::ArithmeticControl control) {
  return detail::Bf16Backend::sub(lhs, rhs, control);
}
Result<bfloat16_t> detail::backend::mul(bfloat16_t lhs, bfloat16_t rhs,
                                        detail::ArithmeticControl control) {
  return detail::Bf16Backend::mul(lhs, rhs, control);
}
Result<bfloat16_t> detail::backend::fma(bfloat16_t a, bfloat16_t b,
                                        bfloat16_t c,
                                        detail::ArithmeticControl control) {
  return detail::Bf16Backend::fma(a, b, c, control);
}

Result<float32_t> detail::backend::add(float16_t low, float32_t high,
                                       detail::ArithmeticControl control) {
  return detail::add(low, high, control);
}
Result<float32_t> detail::backend::sub(float16_t low, float32_t high,
                                       detail::ArithmeticControl control) {
  return detail::sub(low, high, control);
}
Result<float32_t> detail::backend::fma(float16_t a, float16_t b, float32_t c,
                                       detail::ArithmeticControl control) {
  return detail::fma(a, b, c, control);
}
Result<float32_t> detail::backend::add(bfloat16_t low, float32_t high,
                                       detail::ArithmeticControl control) {
  return detail::add(low, high, control);
}
Result<float32_t> detail::backend::sub(bfloat16_t low, float32_t high,
                                       detail::ArithmeticControl control) {
  return detail::sub(low, high, control);
}
Result<float32_t> detail::backend::fma(bfloat16_t a, bfloat16_t b, float32_t c,
                                       detail::ArithmeticControl control) {
  return detail::fma(a, b, c, control);
}

#define PTXSIM_DELEGATE_UNARY(Type, Name)                                 \
  Result<Type> detail::backend::Name(Type value,                          \
                                     detail::ArithmeticControl control) { \
    return detail::comparison::Name(value, control);                      \
  }
PTXSIM_DELEGATE_UNARY(float16_t, abs)
PTXSIM_DELEGATE_UNARY(bfloat16_t, abs)
PTXSIM_DELEGATE_UNARY(float32_t, abs)
PTXSIM_DELEGATE_UNARY(float64_t, abs)
PTXSIM_DELEGATE_UNARY(float16_t, neg)
PTXSIM_DELEGATE_UNARY(bfloat16_t, neg)
PTXSIM_DELEGATE_UNARY(float32_t, neg)
PTXSIM_DELEGATE_UNARY(float64_t, neg)
#undef PTXSIM_DELEGATE_UNARY

#define PTXSIM_DELEGATE_MINMAX(Type, Name, SelectMin)                     \
  Result<Type> detail::backend::Name(Type lhs, Type rhs,                  \
                                     MinMaxControl modifiers,             \
                                     detail::ArithmeticControl control) { \
    return detail::comparison::minmax(lhs, rhs, modifiers, control,       \
                                      SelectMin);                         \
  }
PTXSIM_DELEGATE_MINMAX(float16_t, min, true)
PTXSIM_DELEGATE_MINMAX(bfloat16_t, min, true)
PTXSIM_DELEGATE_MINMAX(float32_t, min, true)
PTXSIM_DELEGATE_MINMAX(float64_t, min, true)
PTXSIM_DELEGATE_MINMAX(float16_t, max, false)
PTXSIM_DELEGATE_MINMAX(bfloat16_t, max, false)
PTXSIM_DELEGATE_MINMAX(float32_t, max, false)
PTXSIM_DELEGATE_MINMAX(float64_t, max, false)
#undef PTXSIM_DELEGATE_MINMAX

Result<float32_t> detail::backend::min(float32_t a, float32_t b, float32_t c,
                                       MinMaxControl modifiers,
                                       detail::ArithmeticControl control) {
  return detail::comparison::minmax(a, b, c, modifiers, control, true);
}
Result<float32_t> detail::backend::max(float32_t a, float32_t b, float32_t c,
                                       MinMaxControl modifiers,
                                       detail::ArithmeticControl control) {
  return detail::comparison::minmax(a, b, c, modifiers, control, false);
}

#define PTXSIM_DELEGATE_COMPARE(Type)                                        \
  Result<bool> detail::backend::compare(Type lhs, Type rhs,                  \
                                        detail::CompareOp operation,         \
                                        detail::ArithmeticControl control) { \
    return detail::comparison::compare(lhs, rhs, operation, control);        \
  }
PTXSIM_DELEGATE_COMPARE(float16_t)
PTXSIM_DELEGATE_COMPARE(bfloat16_t)
PTXSIM_DELEGATE_COMPARE(float32_t)
PTXSIM_DELEGATE_COMPARE(float64_t)
#undef PTXSIM_DELEGATE_COMPARE

Result<float32_t> detail::backend::copysign(float32_t sign,
                                            float32_t magnitude) {
  return detail::comparison::copysign(sign, magnitude);
}
Result<float64_t> detail::backend::copysign(float64_t sign,
                                            float64_t magnitude) {
  return detail::comparison::copysign(sign, magnitude);
}
Result<bool> detail::backend::testp(float32_t value,
                                    detail::TestpOp operation) {
  return detail::comparison::testp(value, operation);
}
Result<bool> detail::backend::testp(float64_t value,
                                    detail::TestpOp operation) {
  return detail::comparison::testp(value, operation);
}
Result<float16_t> detail::backend::tanh_approx(float16_t value,
                                                detail::ApproximationControl control) {
  return detail::tanh_approx(value, control);
}
Result<float16_t> detail::backend::ex2_approx(float16_t value,
                                               detail::ApproximationControl control) {
  return detail::ex2_approx(value, control);
}
Result<bfloat16_t> detail::backend::tanh_approx(bfloat16_t value,
                                                 detail::ApproximationControl control) {
  return detail::tanh_approx(value, control);
}
Result<bfloat16_t> detail::backend::ex2_approx(bfloat16_t value,
                                                detail::ApproximationControl control) {
  return detail::ex2_approx(value, control);
}

Result<float32_t> detail::backend::i32_to_f32(std::int32_t value,
                                              detail::RoundingMode rounding) {
  return detail::i32_to_f32(value, rounding);
}
Result<std::int32_t> detail::backend::f32_to_i32(
    float32_t value, detail::RoundingMode rounding) {
  return detail::f32_to_i32(value, rounding);
}
Result<float32_t> detail::backend::u32_to_f32(std::uint32_t value,
                                              detail::RoundingMode rounding) {
  return detail::u32_to_f32(value, rounding);
}
Result<std::uint32_t> detail::backend::f32_to_u32(
    float32_t value, detail::RoundingMode rounding) {
  return detail::f32_to_u32(value, rounding);
}
Result<float32_t> detail::backend::i64_to_f32(std::int64_t value,
                                              detail::RoundingMode rounding) {
  return detail::i64_to_f32(value, rounding);
}
Result<std::int64_t> detail::backend::f32_to_i64(
    float32_t value, detail::RoundingMode rounding) {
  return detail::f32_to_i64(value, rounding);
}
Result<float32_t> detail::backend::u64_to_f32(std::uint64_t value,
                                              detail::RoundingMode rounding) {
  return detail::u64_to_f32(value, rounding);
}
Result<std::uint64_t> detail::backend::f32_to_u64(
    float32_t value, detail::RoundingMode rounding) {
  return detail::f32_to_u64(value, rounding);
}
Result<float64_t> detail::backend::i32_to_f64(std::int32_t value,
                                              detail::RoundingMode rounding) {
  return detail::i32_to_f64(value, rounding);
}
Result<std::int32_t> detail::backend::f64_to_i32(
    float64_t value, detail::RoundingMode rounding) {
  return detail::f64_to_i32(value, rounding);
}
Result<float64_t> detail::backend::u32_to_f64(std::uint32_t value,
                                              detail::RoundingMode rounding) {
  return detail::u32_to_f64(value, rounding);
}
Result<std::uint32_t> detail::backend::f64_to_u32(
    float64_t value, detail::RoundingMode rounding) {
  return detail::f64_to_u32(value, rounding);
}
Result<float64_t> detail::backend::f32_to_f64(float32_t value) {
  return detail::f32_to_f64(value);
}
Result<float32_t> detail::backend::f64_to_f32(float64_t value,
                                              detail::RoundingMode rounding) {
  return detail::f64_to_f32(value, rounding);
}
Result<float16_t> detail::backend::f32_to_f16(float32_t value,
                                              detail::RoundingMode rounding) {
  return detail::f32_to_f16(value, rounding);
}
Result<float32_t> detail::backend::f16_to_f32(float16_t value) {
  return detail::f16_to_f32(value);
}
Result<float16_t> detail::backend::f64_to_f16(float64_t value,
                                              detail::RoundingMode rounding) {
  return detail::f64_to_f16(value, rounding);
}
Result<float64_t> detail::backend::f16_to_f64(float16_t value) {
  return detail::f16_to_f64(value);
}

}  // namespace ptxsim::arith

namespace ptxsim::arith::detail::dispatch {
namespace {

RoundingMode legacy_rounding(rounding_mode mode) {
  switch (mode) {
    case rounding_mode::nearest_even: return RoundingMode::NearestEven;
    case rounding_mode::toward_zero: return RoundingMode::TowardZero;
    case rounding_mode::toward_negative: return RoundingMode::TowardNegative;
    case rounding_mode::toward_positive: return RoundingMode::TowardPositive;
    default: std::unreachable();
  }
}

ArithmeticControl legacy(floating_control control) {
  return {legacy_rounding(control.rounding), false, SubnormalMode::Preserve};
}
ConversionControl legacy(conversion_control control) {
  return {legacy_rounding(control.rounding), false};
}
floating_status status(ExceptionFlags flags) {
  return {flags.contains(ExceptionFlag::Invalid),
          flags.contains(ExceptionFlag::DivideByZero),
          flags.contains(ExceptionFlag::Overflow),
          flags.contains(ExceptionFlag::Underflow),
          flags.contains(ExceptionFlag::Inexact)};
}
template <typename T>
T input_ftz(T value, subnormal_mode mode) {
  return mode == subnormal_mode::flush_input ||
                 mode == subnormal_mode::flush_input_and_output
             ? flush_subnormal(value)
             : value;
}
template <typename T>
T output_ftz(T value, subnormal_mode mode) {
  return mode == subnormal_mode::flush_output ||
                 mode == subnormal_mode::flush_input_and_output
             ? flush_subnormal(value)
             : value;
}
template <typename T>
[[nodiscard]] T apply_result_controls(T value, floating_control control) {
  if (control.saturation == saturation_mode::zero_to_one) {
    if (is_nan(value) || is_negative(value))
      return T{};
    using Traits = FormatTraits<T>;
    const auto one = T::from_bits(static_cast<typename Traits::Bits>(
        static_cast<typename Traits::Bits>(Traits::exponent_bias)
        << Traits::fraction_bits));
    if (value.bits() > one.bits())
      return one;
  }
  if (control.activation == activation_mode::relu) {
    if (is_nan(value)) {
      using Traits = FormatTraits<T>;
      return T::from_bits(static_cast<typename Traits::Bits>(
          (static_cast<typename Traits::Bits>(Traits::canonical_nan_exponent_field)
           << Traits::fraction_bits) |
          static_cast<typename Traits::Bits>(
              Traits::canonical_nan_fraction_field)));
    }
    if (is_negative(value))
      return T{};
  }
  return value;
}
template <scalar_operation Op, typename T, typename... Operands>
std::expected<void, arithmetic_error> validate(floating_control control) {
  using Capability = floating_operation_control_capability<Op, T, Operands...>;
  if (!Capability::supported)
    return std::unexpected(arithmetic_error::unsupported_operation);
  if (!Capability::supports(control.rounding))
    return std::unexpected(arithmetic_error::unsupported_rounding);
  if (!Capability::supports(control.subnormal))
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);
  if (!Capability::supports(control.saturation))
    return std::unexpected(arithmetic_error::unsupported_saturation);
  if (!Capability::supports(control.activation))
    return std::unexpected(arithmetic_error::unsupported_activation);
  if (!Capability::supports(control))
    return std::unexpected(arithmetic_error::unsupported_activation);
  return {};
}
template <scalar_operation Op, typename T, typename F>
std::expected<result<T, floating_status>, arithmetic_error> execute(
    floating_control control, F&& operation, T a) {
  if (auto valid = validate<Op, T>(control); !valid)
    return std::unexpected(valid.error());
  auto raw = operation(input_ftz(a, control.subnormal));
  return result<T, floating_status>{
      apply_result_controls(output_ftz(raw.value, control.subnormal), control),
      status(raw.flags)};
}
template <scalar_operation Op, typename T, typename F>
std::expected<result<T, floating_status>, arithmetic_error> execute(
    floating_control control, F&& operation, T a, T b) {
  if (auto valid = validate<Op, T>(control); !valid)
    return std::unexpected(valid.error());
  auto raw = operation(input_ftz(a, control.subnormal),
                       input_ftz(b, control.subnormal));
  return result<T, floating_status>{
      apply_result_controls(output_ftz(raw.value, control.subnormal), control),
      status(raw.flags)};
}
template <scalar_operation Op, typename T, typename F>
std::expected<result<T, floating_status>, arithmetic_error> execute(
    floating_control control, F&& operation, T a, T b, T c) {
  if (auto valid = validate<Op, T>(control); !valid)
    return std::unexpected(valid.error());
  auto raw = operation(input_ftz(a, control.subnormal),
                       input_ftz(b, control.subnormal),
                       input_ftz(c, control.subnormal));
  return result<T, floating_status>{
      apply_result_controls(output_ftz(raw.value, control.subnormal), control),
      status(raw.flags)};
}
}  // namespace

#define PTXSIM_DISPATCH_BINARY(T, name)                                    \
  std::expected<result<T, floating_status>, arithmetic_error> name(        \
      T a, T b, floating_control c) {                                      \
    return execute<scalar_operation::name, T>(c, [&](T x, T y) { return backend::name(x, y, legacy(c)); }, a, b); \
  }
#define PTXSIM_DISPATCH_BINARY_SET(T) \
  PTXSIM_DISPATCH_BINARY(T, add) PTXSIM_DISPATCH_BINARY(T, sub) PTXSIM_DISPATCH_BINARY(T, mul)
PTXSIM_DISPATCH_BINARY_SET(float16_t)
PTXSIM_DISPATCH_BINARY_SET(bfloat16_t)
PTXSIM_DISPATCH_BINARY_SET(float32_t)
PTXSIM_DISPATCH_BINARY_SET(float64_t)
#undef PTXSIM_DISPATCH_BINARY_SET
#undef PTXSIM_DISPATCH_BINARY

std::expected<result<tfloat32_t, floating_status>, arithmetic_error>
quantize_tf32(float32_t value, conversion_control control,
              const tf32_encoding_profile& profile) {
  if (profile.model != tf32_encoding_model::f32_top_19_bits)
    return std::unexpected(arithmetic_error::unsupported_operation);
  if (control.rounding == rounding_mode::stochastic)
    return std::unexpected(arithmetic_error::unsupported_rounding);
  if (control.saturation != saturation_mode::none &&
      control.saturation != saturation_mode::finite)
    return std::unexpected(arithmetic_error::unsupported_saturation);
  if (control.activation != activation_mode::none &&
      control.activation != activation_mode::relu)
    return std::unexpected(arithmetic_error::unsupported_activation);
  if (control.source_subnormal != subnormal_mode::preserve ||
      control.destination_subnormal != subnormal_mode::preserve)
    return std::unexpected(arithmetic_error::unsupported_subnormal_mode);

  auto bits = value.bits();
  if (control.saturation == saturation_mode::finite &&
      (bits & 0x7fffffffu) == 0x7f800000u)
    bits = (bits & 0x80000000u) | 0x7f7fffffu;
  std::uint32_t quantized = bits & 0xFFFFE000u;
  bool saturated = false;
  if ((bits & 0x7F800000u) != 0x7F800000u) {
    const bool negative = (bits & 0x80000000u) != 0;
    std::uint32_t increment = 0;
    switch (control.rounding) {
      case rounding_mode::nearest_even:
        increment = 0x0FFFu + ((bits >> 13) & 1u);
        break;
      case rounding_mode::nearest_away:
        increment = 0x1000u;
        break;
      case rounding_mode::toward_positive:
        increment = negative ? 0 : 0x1FFFu;
        break;
      case rounding_mode::toward_negative:
        increment = negative ? 0x1FFFu : 0;
        break;
      case rounding_mode::toward_zero:
        break;
      default:
        std::unreachable();
    }
    quantized = (bits + increment) & 0xFFFFE000u;
  } else if ((bits & 0x007FFFFFu) != 0 &&
             (quantized & 0x007FFFFFu) == 0) {
    // Retain NaN classification after trimming an all-low payload.
    quantized |= 0x00002000u;
  }
  if (control.saturation == saturation_mode::finite &&
      (quantized & 0x7fffffffu) == 0x7f800000u) {
    quantized = (quantized & 0x80000000u) | 0x7f7fe000u;
    saturated = true;
  }
  return {{tf32_factory::make(float32_t::from_bits(quantized)),
           {false, false, saturated, false, quantized != bits}}};
}

#define PTXSIM_DISPATCH_MIXED(name, T)                                    \
  std::expected<result<float32_t, floating_status>, arithmetic_error> name(\
      T a, float32_t b, floating_control c) {                              \
    if (auto valid = validate<scalar_operation::name, float32_t, T, float32_t>(c); !valid) \
      return std::unexpected(valid.error());                               \
    auto raw = backend::name(input_ftz(a, c.subnormal),                    \
                             input_ftz(b, c.subnormal), legacy(c));        \
    return result<float32_t, floating_status>{                             \
        apply_result_controls(output_ftz(raw.value, c.subnormal), c),      \
        status(raw.flags)};                                                 \
  }
PTXSIM_DISPATCH_MIXED(add, float16_t)
PTXSIM_DISPATCH_MIXED(sub, float16_t)
PTXSIM_DISPATCH_MIXED(add, bfloat16_t)
PTXSIM_DISPATCH_MIXED(sub, bfloat16_t)
#undef PTXSIM_DISPATCH_MIXED

std::expected<result<float32_t, floating_status>, arithmetic_error> div(
    float32_t a, float32_t b, floating_control c) { return execute<scalar_operation::div, float32_t>(c, [&](auto x, auto y) { return backend::div(x, y, legacy(c)); }, a, b); }
std::expected<result<float64_t, floating_status>, arithmetic_error> div(
    float64_t a, float64_t b, floating_control c) { return execute<scalar_operation::div, float64_t>(c, [&](auto x, auto y) { return backend::div(x, y, legacy(c)); }, a, b); }

#define PTXSIM_DISPATCH_FMA(T)                                             \
  std::expected<result<T, floating_status>, arithmetic_error> fma(         \
      T a, T b, T z, floating_control c) {                                 \
    return execute<scalar_operation::fma, T>(c, [&](T x, T y, T q) { return backend::fma(x, y, q, legacy(c)); }, a, b, z); \
  }
PTXSIM_DISPATCH_FMA(float16_t)
PTXSIM_DISPATCH_FMA(bfloat16_t)
PTXSIM_DISPATCH_FMA(float32_t)
PTXSIM_DISPATCH_FMA(float64_t)
#undef PTXSIM_DISPATCH_FMA
std::expected<result<float32_t, floating_status>, arithmetic_error> fma(
    float16_t a, float16_t b, float32_t z, floating_control c) {
  if (auto valid = validate<scalar_operation::fma, float32_t, float16_t, float16_t, float32_t>(c); !valid) return std::unexpected(valid.error());
  auto raw = backend::fma(input_ftz(a, c.subnormal), input_ftz(b, c.subnormal),
                          input_ftz(z, c.subnormal), legacy(c));
  return result<float32_t, floating_status>{
      apply_result_controls(output_ftz(raw.value, c.subnormal), c),
      status(raw.flags)};
}
std::expected<result<float32_t, floating_status>, arithmetic_error> fma(
    bfloat16_t a, bfloat16_t b, float32_t z, floating_control c) {
  if (auto valid = validate<scalar_operation::fma, float32_t, bfloat16_t, bfloat16_t, float32_t>(c); !valid) return std::unexpected(valid.error());
  auto raw = backend::fma(input_ftz(a, c.subnormal), input_ftz(b, c.subnormal),
                          input_ftz(z, c.subnormal), legacy(c));
  return result<float32_t, floating_status>{
      apply_result_controls(output_ftz(raw.value, c.subnormal), c),
      status(raw.flags)};
}

#define PTXSIM_DISPATCH_UNARY(T, name)                                     \
  std::expected<result<T, floating_status>, arithmetic_error> name(        \
      T a, floating_control c) { return execute<scalar_operation::name, T>(c, [&](T x) { return backend::name(x, legacy(c)); }, a); }
#define PTXSIM_DISPATCH_MINMAX(T, name)                                    \
  std::expected<result<T, floating_status>, arithmetic_error> name(        \
      T a, T b, floating_control c) { return execute<scalar_operation::name, T>(c, [&](T x, T y) { return backend::name(x, y, {}, legacy(c)); }, a, b); }
#define PTXSIM_DISPATCH_UNARY_SET(T) \
  PTXSIM_DISPATCH_UNARY(T, abs) PTXSIM_DISPATCH_UNARY(T, neg) \
  PTXSIM_DISPATCH_MINMAX(T, min) PTXSIM_DISPATCH_MINMAX(T, max)
PTXSIM_DISPATCH_UNARY_SET(float16_t)
PTXSIM_DISPATCH_UNARY_SET(bfloat16_t)
PTXSIM_DISPATCH_UNARY_SET(float32_t)
PTXSIM_DISPATCH_UNARY_SET(float64_t)
#undef PTXSIM_DISPATCH_UNARY_SET
#undef PTXSIM_DISPATCH_MINMAX
#undef PTXSIM_DISPATCH_UNARY

#define PTXSIM_DISPATCH_F32F64_UNARY(T, name)                              \
  std::expected<result<T, floating_status>, arithmetic_error> name(        \
      T a, floating_control c) { return execute<scalar_operation::name, T>(c, [&](T x) { return backend::name(x, legacy(c)); }, a); }
PTXSIM_DISPATCH_F32F64_UNARY(float32_t, sqrt)
PTXSIM_DISPATCH_F32F64_UNARY(float64_t, sqrt)
PTXSIM_DISPATCH_F32F64_UNARY(float32_t, rcp)
PTXSIM_DISPATCH_F32F64_UNARY(float64_t, rcp)
#undef PTXSIM_DISPATCH_F32F64_UNARY

namespace {
ApproximationControl legacy(special_function_control control) {
  return {control.subnormal == subnormal_mode::flush_input ||
          control.subnormal == subnormal_mode::flush_input_and_output,
          {}};
}
ApproximationControl legacy(special_function_control control,
                            const approximation_profile& profile) {
  auto result = legacy(control);
  result.profile = profile;
  return result;
}
template <typename T, typename F>
std::expected<result<T, floating_status>, arithmetic_error> approximate(
    T value, special_function_control control, F&& operation) {
  const auto input = input_ftz(value, control.subnormal);
  auto raw = operation(input);
  auto out = result<T, floating_status>{output_ftz(raw.value, control.subnormal),
                                        status(raw.flags)};
  out.status.model_dependent = true;
  return out;
}
}  // namespace

std::expected<result<float32_t, floating_status>, arithmetic_error> div_approx(
    float32_t a, float32_t b, special_function_control c,
    const approximation_profile& profile) {
  return execute<scalar_operation::div, float32_t>(
      {.subnormal = c.subnormal}, [&](auto x, auto y) {
    return backend::div_approx(x, y, legacy(c, profile)); }, a, b);
}
std::expected<result<float32_t, floating_status>, arithmetic_error> div_full(
    float32_t a, float32_t b, special_function_control c,
    const approximation_profile& profile) {
  return execute<scalar_operation::div, float32_t>(
      {.subnormal = c.subnormal}, [&](auto x, auto y) {
    return backend::div_full(x, y, legacy(c, profile)); }, a, b);
}
#define PTXSIM_APPROX_F32(name)                                            \
  std::expected<result<float32_t, floating_status>, arithmetic_error> name(\
      float32_t v, special_function_control c, const approximation_profile& p) { return approximate(v, c, [&](float32_t x) { return backend::name(x, legacy(c, p)); }); }
PTXSIM_APPROX_F32(rcp_approx)
PTXSIM_APPROX_F32(sqrt_approx)
PTXSIM_APPROX_F32(rsqrt_approx)
PTXSIM_APPROX_F32(sin_approx)
PTXSIM_APPROX_F32(cos_approx)
PTXSIM_APPROX_F32(lg2_approx)
PTXSIM_APPROX_F32(ex2_approx)
#undef PTXSIM_APPROX_F32
std::expected<result<float64_t, floating_status>, arithmetic_error>
rcp_approx_ftz(float64_t v, special_function_control c,
               const approximation_profile&) {
  return approximate(v, c, [](float64_t x) { return backend::rcp_approx_ftz(x); });
}
std::expected<result<float64_t, floating_status>, arithmetic_error>
rsqrt_approx(float64_t v, special_function_control c,
             const approximation_profile&) {
  return approximate(v, c, [](float64_t x) { return backend::rsqrt_approx(x); });
}
std::expected<result<float64_t, floating_status>, arithmetic_error>
rsqrt_approx_ftz(float64_t v, special_function_control c,
                 const approximation_profile&) {
  return approximate(v, c,
                     [](float64_t x) { return backend::rsqrt_approx_ftz(x); });
}
std::expected<result<float32_t, floating_status>, arithmetic_error> tanh_approx(
    float32_t v, special_function_control c, const approximation_profile& p) {
  return approximate(v, c, [&](float32_t x) { return backend::tanh_approx(x, legacy(c, p)); });
}
#define PTXSIM_APPROX_LOW(T, name)                                         \
  std::expected<result<T, floating_status>, arithmetic_error> name(        \
      T v, special_function_control c, const approximation_profile& p) { return approximate(v, c, [&](T x) { return backend::name(x, legacy(c, p)); }); }
PTXSIM_APPROX_LOW(float16_t, tanh_approx)
PTXSIM_APPROX_LOW(bfloat16_t, tanh_approx)
PTXSIM_APPROX_LOW(float16_t, ex2_approx)
PTXSIM_APPROX_LOW(bfloat16_t, ex2_approx)
#undef PTXSIM_APPROX_LOW

}  // namespace ptxsim::arith::detail::dispatch
