#include "approximation_backend.hpp"

#include "backend.hpp"

#include "low_precision_backend.hpp"
#include "nan_policy.hpp"
#include "operation_policy.hpp"
#include "softfloat_backend.hpp"

#include <bit>

namespace ptxsim::arith::detail {
namespace {

[[nodiscard]] ArithmeticControl exact_control(
    ApproximationControl control = {}) noexcept {
  return {.subnormal = control.flush_subnormal
                           ? SubnormalMode::FlushToSignedZero
                           : SubnormalMode::Preserve};
}

[[nodiscard]] constexpr bool is_reference_profile(
    const ApproximationControl& control) noexcept {
  return control.profile.revision == ptx_numeric_revision::v9_3 &&
         control.profile.model == approximation_model::ptx_9_3_reference &&
         control.profile.provenance ==
             approximation_provenance::model_dependent_reference;
}

enum class Unary { Sin, Cos, Lg2, Ex2, Tanh };

using F32 = Result<float32_t>;

[[nodiscard]] constexpr float32_t f32(std::uint32_t bits) {
  return float32_t::from_bits(bits);
}
[[nodiscard]] constexpr bool is_large_divisor(float32_t value) {
  constexpr auto two_to_126 = 0x7e800000u;
  constexpr auto two_to_128 = 0x7f800000u;
  const auto magnitude = value.bits() & 0x7fffffffu;
  return magnitude > two_to_126 && magnitude < two_to_128;
}
[[nodiscard]] F32 join(F32 a, F32 b, F32 value) {
  value.flags |= a.flags;
  value.flags |= b.flags;
  return value;
}
[[nodiscard]] F32 plus(F32 a, F32 b) {
  return join(a, b, SoftFloatBackend<float32_t>::add(a.value, b.value, {}));
}
[[nodiscard]] F32 minus(F32 a, F32 b) {
  return join(a, b, SoftFloatBackend<float32_t>::sub(a.value, b.value, {}));
}
[[nodiscard]] F32 times(F32 a, F32 b) {
  return join(a, b, SoftFloatBackend<float32_t>::mul(a.value, b.value, {}));
}
[[nodiscard]] F32 over(F32 a, F32 b) {
  return join(a, b, SoftFloatBackend<float32_t>::div(a.value, b.value, {}));
}
[[nodiscard]] F32 negate(F32 a) {
  a.value = f32(a.value.bits() ^ 0x80000000u);
  return a;
}
[[nodiscard]] F32 constant(std::uint32_t bits) { return {f32(bits), {}}; }

[[nodiscard]] F32 prepare(float32_t value, Unary operation,
                          ApproximationControl control, bool& handled) {
  handled = true;
  if (control.flush_subnormal)
    value = flush_subnormal(value);
  if (is_nan(value))
    return propagate_nan(value);

  const bool negative = is_negative(value);
  if (is_zero(value)) {
    switch (operation) {
      case Unary::Sin:
      case Unary::Tanh:
        return {value, {}};
      case Unary::Cos:
      case Unary::Ex2:
        return {f32(0x3F800000u), {}};
      case Unary::Lg2:
        return {f32(0xFF800000u), {}};
    }
  }
  if ((operation == Unary::Sin || operation == Unary::Cos) &&
      is_infinity(value))
    return canonical_invalid_nan<float32_t>();
  if (operation == Unary::Lg2) {
    if (is_zero(value))
      return {float32_t::from_bits(0xFF800000u), {}};
    if (negative)
      return canonical_invalid_nan<float32_t>();
    if (is_infinity(value))
      return {value, {}};
  }
  if (operation == Unary::Ex2 && is_infinity(value))
    return {negative ? float32_t{} : value, {}};
  if (operation == Unary::Tanh && is_infinity(value))
    return {negative ? f32(0xBF800000u) : f32(0x3F800000u),
            {}};
  handled = false;
  return {value, {}};
}

[[nodiscard]] F32 sin_or_cos(float32_t value, bool cosine,
                              ApproximationControl control) {
  bool handled{};
  auto input = prepare(value, cosine ? Unary::Cos : Unary::Sin, control, handled);
  if (handled)
    return input;
  const bool input_negative = (input.value.bits() >> 31) != 0;
  F32 x{f32(input.value.bits() & 0x7fffffffu), input.flags};
  bool sine_negative = input_negative, cosine_negative = false;
  constexpr auto pi = 0x40490fdbu, half_pi = 0x3fc90fdbu;
  // PTX's accuracy bound applies to [-2pi, 2pi].  Larger reductions are
  // model-dependent; the reference model reduces this bounded range only.
  if (x.value.bits() > pi) {
    x = minus(x, constant(pi));
    sine_negative = !sine_negative;
    cosine_negative = !cosine_negative;
  }
  if (x.value.bits() > half_pi) {
    x = minus(constant(pi), x);
    cosine_negative = !cosine_negative;
  }
  auto x2 = times(x, x);
  F32 p = cosine ? constant(0xb493f27e) : constant(0xb2d7322b);
  const std::uint32_t coeffs[] = {
      cosine ? 0x37d00d01u : 0x3638ef1du,
      cosine ? 0xbab60b61u : 0xb9500d01u,
      cosine ? 0x3d2aaaabu : 0x3c088889u,
      cosine ? 0xbf000000u : 0xbe2aaaabu};
  for (const auto coefficient : coeffs)
    p = plus(constant(coefficient), times(x2, p));
  auto out = cosine ? plus(constant(0x3f800000u), times(x2, p))
                    : times(x, plus(constant(0x3f800000u), times(x2, p)));
  if (cosine ? cosine_negative : sine_negative)
    out = negate(out);
  if (control.flush_subnormal)
    out.value = flush_subnormal(out.value);
  return out;
}

[[nodiscard]] F32 log2_core(float32_t value, ApproximationControl control) {
  bool handled{};
  auto input = prepare(value, Unary::Lg2, control, handled);
  if (handled)
    return input;
  const auto bits = input.value.bits();
  const auto exponent = (bits >> 23) & 0xffu;
  const auto fraction = bits & 0x7fffffu;
  int exponent_value;
  std::uint32_t mantissa;
  if (exponent == 0) {
    const unsigned lead = 31u - std::countl_zero(fraction);
    exponent_value = static_cast<int>(lead) - 149;
    mantissa = 0x3f800000u | ((fraction << (23u - lead)) & 0x7fffffu);
  } else {
    exponent_value = static_cast<int>(exponent) - 127;
    mantissa = 0x3f800000u | fraction;
  }
  F32 m{f32(mantissa), input.flags};
  if (m.value.bits() > 0x3fb504f3u) {  // sqrt(2)
    m = times(m, constant(0x3f000000u));
    ++exponent_value;
  }
  auto z = over(minus(m, constant(0x3f800000u)),
                plus(m, constant(0x3f800000u)));
  auto z2 = times(z, z);
  F32 p = constant(0x3d888889u);
  constexpr std::uint32_t coefficients[] = {0x3d9d89d9u, 0x3dba2e8cu,
                                              0x3de38e39u, 0x3e124925u,
                                              0x3e4ccccdu, 0x3eaaaaabu,
                                              0x3f800000u};
  for (const auto coefficient : coefficients)
    p = plus(constant(coefficient), times(z2, p));
  auto logarithm = times(constant(0x40000000u), times(z, p));
  auto e = backend::i32_to_f32(exponent_value, RoundingMode::NearestEven);
  auto out = plus({e.value, logarithm.flags}, times(logarithm, constant(0x3fb8aa3bu)));
  out.flags |= input.flags;
  if (control.flush_subnormal)
    out.value = flush_subnormal(out.value);
  return out;
}

[[nodiscard]] F32 exp2_core(float32_t value, ApproximationControl control) {
  bool handled{};
  auto input = prepare(value, Unary::Ex2, control, handled);
  if (handled)
    return input;
  const auto bits = input.value.bits();
  const bool negative = (bits >> 31) != 0;
  const unsigned exponent = (bits >> 23) & 0xffu;
  const auto fraction = bits & 0x7fffffu;
  int integer{};
  if (exponent >= 158)  // |x| >= 2^31
    return {negative ? float32_t{} : f32(0x7f800000u), input.flags};
  if (exponent >= 127) {
    const unsigned shift = 150u - exponent;
    const std::uint32_t significand = 0x800000u | fraction;
    const auto magnitude = significand >> shift;
    const bool fractional = shift != 0 && (significand & ((1u << shift) - 1u));
    integer = negative ? -static_cast<int>(magnitude) - static_cast<int>(fractional)
                       : static_cast<int>(magnitude);
  } else if (negative) {
    integer = -1;
  }
  auto n = backend::i32_to_f32(integer, RoundingMode::NearestEven);
  auto r = minus(input, {n.value, n.flags});
  F32 p = constant(0x2de1deb3u);
  constexpr std::uint32_t coefficients[] = {
      0x2ff46564u, 0x31f267a9u, 0x33da929fu, 0x35b16011u, 0x377fe5feu,
      0x39218489u, 0x3aaec3ffu, 0x3c1d955bu, 0x3d635847u, 0x3e75fdf0u,
      0x3f317218u, 0x3f800000u};
  for (const auto coefficient : coefficients)
    p = plus(constant(coefficient), times(r, p));
  if (integer > 127)
    return {f32(0x7f800000u), p.flags};
  if (integer < -149)
    return {float32_t{}, p.flags};
  const auto scale = integer >= -126
                         ? f32(static_cast<std::uint32_t>(integer + 127) << 23)
                         : f32(1u << static_cast<unsigned>(integer + 149));
  auto out = times(p, {scale, {}});
  if (control.flush_subnormal)
    out.value = flush_subnormal(out.value);
  return out;
}

[[nodiscard]] F32 tanh_core(float32_t value, ApproximationControl control) {
  bool handled{};
  auto input = prepare(value, Unary::Tanh, control, handled);
  if (handled)
    return input;
  const bool negative = (input.value.bits() >> 31) != 0;
  auto magnitude = f32(input.value.bits() & 0x7fffffffu);
  if (magnitude.bits() >= 0x41100000u)  // tanh(9) is within the PTX bound of 1.
    return {negative ? f32(0xbf800000u) : f32(0x3f800000u), input.flags};
  auto e = exp2_core(times({magnitude, input.flags}, constant(0x4038aa3bu)).value,
                     control);  // exp(2x) = exp2(2x / ln(2))
  auto out = over(minus(e, constant(0x3f800000u)), plus(e, constant(0x3f800000u)));
  if (negative)
    out = negate(out);
  return out;
}

template <typename T>
[[nodiscard]] Result<float32_t> widen_low(T value);

template <>
[[nodiscard]] Result<float32_t> widen_low(float16_t value) {
  return f16_to_f32(value);
}

template <>
[[nodiscard]] Result<float32_t> widen_low(bfloat16_t value) {
  return widen_to_f32(value, {});
}

template <typename T>
[[nodiscard]] Result<T> narrow_low(float32_t value);

template <>
[[nodiscard]] Result<float16_t> narrow_low(float32_t value) {
  return f32_to_f16(value, RoundingMode::NearestEven);
}

template <>
[[nodiscard]] Result<bfloat16_t> narrow_low(float32_t value) {
  return narrow_from_f32<bfloat16_t>(value, {});
}

template <typename T>
[[nodiscard]] Result<T> low_unary(T value, Unary operation,
                                  bool fixed_ftz) {
  if (fixed_ftz)
    value = flush_subnormal(value);
  const auto widened = widen_low(value);
  const auto approximate = operation == Unary::Tanh
                               ? tanh_core(widened.value, {})
                               : exp2_core(widened.value, {});
  auto result = narrow_low<T>(approximate.value);
  result.flags |= widened.flags;
  result.flags |= approximate.flags;
  if (fixed_ftz)
    result.value = flush_subnormal(result.value);
  return result;
}

[[nodiscard]] Result<float64_t> canonical_nan_f64(float64_t value) {
  constexpr auto ptx_nan = float64_t::from_bits(0x7FFFFFFF00000000ULL);
  if (is_signaling_nan(value))
    return {ptx_nan,
            ExceptionFlags{static_cast<std::uint8_t>(ExceptionFlag::Invalid)}};
  return {ptx_nan, {}};
}

[[nodiscard]] Result<float64_t> invalid_nan_f64() {
  return {float64_t::from_bits(0x7FFFFFFF00000000ULL),
          ExceptionFlags{static_cast<std::uint8_t>(ExceptionFlag::Invalid)}};
}

template <typename Function>
[[nodiscard]] Result<float64_t> f64_ftz_approx(float64_t value,
                                               Function function) {
  value = flush_subnormal(value);
  if (is_nan(value))
    return canonical_nan_f64(value);
  value = float64_t::from_bits(value.bits() & 0xFFFFFFFF00000000ULL);
  auto result = function(value);
  if (is_nan(result.value)) {
    auto canonical = canonical_nan_f64(result.value);
    canonical.flags |= result.flags;
    return canonical;
  }
  result.value = flush_subnormal(result.value);
  result.value =
      float64_t::from_bits(result.value.bits() & 0xFFFFFFFF00000000ULL);
  return result;
}

}  // namespace

Result<float32_t> div_approx(float32_t lhs, float32_t rhs,
                             ApproximationControl control) {
  validate_approximation_control<Operation::DivApprox, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  if (is_large_divisor(rhs) && !is_nan(lhs)) {
    if (is_infinity(lhs))
      return canonical_invalid_nan<float32_t>();
    return {f32((lhs.bits() ^ rhs.bits()) & 0x80000000u), {}};
  }
  // PTX div.approx is specified through an approximate reciprocal path; keep
  // that composition explicit rather than accidentally making it an alias of
  // correctly rounded division.
  const auto reciprocal =
      SoftFloatBackend<float32_t>::rcp(rhs, exact_control(control));
  auto result = SoftFloatBackend<float32_t>::mul(lhs, reciprocal.value,
                                                 exact_control(control));
  result.flags |= reciprocal.flags;
  return result;
}

Result<float32_t> div_full(float32_t lhs, float32_t rhs,
                           ApproximationControl control) {
  validate_approximation_control<Operation::DivFull, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return SoftFloatBackend<float32_t>::div(lhs, rhs, exact_control(control));
}

Result<float32_t> rcp_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::RcpApprox, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return SoftFloatBackend<float32_t>::rcp(value, exact_control(control));
}

Result<float32_t> sqrt_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::SqrtApprox, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return SoftFloatBackend<float32_t>::sqrt(value, exact_control(control));
}

Result<float32_t> rsqrt_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::RsqrtApprox, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  const auto root =
      SoftFloatBackend<float32_t>::sqrt(value, exact_control(control));
  auto result =
      SoftFloatBackend<float32_t>::rcp(root.value, exact_control(control));
  result.flags |= root.flags;
  return result;
}

Result<float32_t> sin_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::SinApprox, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return sin_or_cos(value, false, control);
}

Result<float32_t> cos_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::CosApprox, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return sin_or_cos(value, true, control);
}

Result<float32_t> lg2_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::Lg2Approx, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return log2_core(value, control);
}

Result<float32_t> ex2_approx(float32_t value, ApproximationControl control) {
  validate_approximation_control<Operation::Ex2Approx, float32_t>(control);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  return exp2_core(value, control);
}

Result<float32_t> tanh_approx(float32_t value, ApproximationControl control) {
  static_assert(OperationTraits<float32_t, Operation::TanhApprox>::supported);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float32_t>();
  if (is_subnormal(value))
    return {value, {}};
  return tanh_core(value, {});
}

Result<float64_t> rsqrt_approx(float64_t value) {
  static_assert(OperationTraits<float64_t, Operation::RsqrtApprox>::supported);
  if (is_nan(value))
    return propagate_nan(value);
  if (is_negative(value) && !is_zero(value))
    return canonical_invalid_nan<float64_t>();
  const auto root = SoftFloatBackend<float64_t>::sqrt(value, {});
  auto result = SoftFloatBackend<float64_t>::rcp(root.value, {});
  result.flags |= root.flags;
  return result;
}

Result<float64_t> rcp_approx_ftz(float64_t value) {
  static_assert(OperationTraits<float64_t, Operation::RcpApproxFtz>::supported);
  return f64_ftz_approx(value, [](float64_t input) {
    return SoftFloatBackend<float64_t>::rcp(input, {});
  });
}

Result<float64_t> rsqrt_approx_ftz(float64_t value) {
  static_assert(
      OperationTraits<float64_t, Operation::RsqrtApproxFtz>::supported);
  return f64_ftz_approx(value, [](float64_t input) {
    if (is_negative(input) && !is_zero(input))
      return invalid_nan_f64();
    const auto root = SoftFloatBackend<float64_t>::sqrt(input, {});
    auto result = SoftFloatBackend<float64_t>::rcp(root.value, {});
    result.flags |= root.flags;
    return result;
  });
}

Result<float16_t> tanh_approx(float16_t value, ApproximationControl control) {
  static_assert(OperationTraits<float16_t, Operation::TanhApprox>::supported);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float16_t>();
  return low_unary(value, Unary::Tanh, false);
}

Result<float16_t> ex2_approx(float16_t value, ApproximationControl control) {
  static_assert(OperationTraits<float16_t, Operation::Ex2Approx>::supported);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<float16_t>();
  return low_unary(value, Unary::Ex2, false);
}

Result<bfloat16_t> tanh_approx(bfloat16_t value, ApproximationControl control) {
  static_assert(OperationTraits<bfloat16_t, Operation::TanhApprox>::supported);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<bfloat16_t>();
  return low_unary(value, Unary::Tanh, false);
}

Result<bfloat16_t> ex2_approx(bfloat16_t value, ApproximationControl control) {
  static_assert(OperationTraits<bfloat16_t, Operation::Ex2Approx>::supported);
  if (!is_reference_profile(control))
    return canonical_invalid_nan<bfloat16_t>();
  return low_unary(value, Unary::Ex2, true);
}

}  // namespace ptxsim::arith::detail
