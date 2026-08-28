#include "approximation_backend.hpp"

#include "low_precision_backend.hpp"
#include "nan_policy.hpp"
#include "operation_policy.hpp"
#include "softfloat_backend.hpp"

#include <bit>
#include <cmath>

namespace ptxsim::fp::detail {
namespace {

[[nodiscard]] ArithmeticControl exact_control(
    ApproximationControl control = {}) noexcept {
  return {.subnormal = control.flush_subnormal
                           ? SubnormalMode::FlushToSignedZero
                           : SubnormalMode::Preserve};
}

[[nodiscard]] Fp32 from_float(float value) noexcept {
  return Fp32{std::bit_cast<std::uint32_t>(value)};
}

[[nodiscard]] float to_float(Fp32 value) noexcept {
  return std::bit_cast<float>(value.bits);
}

enum class HostUnary { Sin, Cos, Lg2, Ex2, Tanh };

[[nodiscard]] Result<Fp32> host_unary(Fp32 value, HostUnary operation,
                                      ApproximationControl control = {}) {
  if (control.flush_subnormal)
    value = flush_subnormal(value);
  if (is_nan(value))
    return propagate_nan(value);

  const bool negative = is_negative(value);
  if (is_zero(value)) {
    switch (operation) {
      case HostUnary::Sin:
      case HostUnary::Tanh:
        return {value, {}};
      case HostUnary::Cos:
      case HostUnary::Ex2:
        return {Fp32{0x3F800000u}, {}};
      case HostUnary::Lg2:
        return {Fp32{0xFF800000u}, {}};
    }
  }
  if ((operation == HostUnary::Sin || operation == HostUnary::Cos) &&
      is_infinity(value))
    return canonical_invalid_nan<Fp32>();
  if (operation == HostUnary::Lg2) {
    if (is_zero(value))
      return {Fp32{0xFF800000u}, {}};
    if (negative)
      return canonical_invalid_nan<Fp32>();
    if (is_infinity(value))
      return {value, {}};
  }
  if (operation == HostUnary::Ex2 && is_infinity(value))
    return {negative ? Fp32{} : value, {}};
  if (operation == HostUnary::Tanh && is_infinity(value))
    return {negative ? Fp32{0xBF800000u} : Fp32{0x3F800000u}, {}};

  float result;
  switch (operation) {
    case HostUnary::Sin:
      result = std::sin(to_float(value));
      break;
    case HostUnary::Cos:
      result = std::cos(to_float(value));
      break;
    case HostUnary::Lg2:
      result = std::log2(to_float(value));
      break;
    case HostUnary::Ex2:
      result = std::exp2(to_float(value));
      break;
    case HostUnary::Tanh:
      result = std::tanh(to_float(value));
      break;
  }
  auto output = from_float(result);
  if (control.flush_subnormal)
    output = flush_subnormal(output);
  return {output, {}};
}

template <typename T>
[[nodiscard]] Result<Fp32> widen_low(T value);

template <>
[[nodiscard]] Result<Fp32> widen_low(Fp16 value) {
  return f16_to_f32(value);
}

template <>
[[nodiscard]] Result<Fp32> widen_low(Bf16 value) {
  return widen_to_f32(value, {});
}

template <typename T>
[[nodiscard]] Result<T> narrow_low(Fp32 value);

template <>
[[nodiscard]] Result<Fp16> narrow_low(Fp32 value) {
  return f32_to_f16(value, RoundingMode::NearestEven);
}

template <>
[[nodiscard]] Result<Bf16> narrow_low(Fp32 value) {
  return narrow_from_f32<Bf16>(value, {});
}

template <typename T>
[[nodiscard]] Result<T> low_unary(T value, HostUnary operation,
                                  bool fixed_ftz) {
  if (fixed_ftz)
    value = flush_subnormal(value);
  const auto widened = widen_low(value);
  const auto approximate = host_unary(widened.value, operation);
  auto result = narrow_low<T>(approximate.value);
  result.flags |= widened.flags;
  result.flags |= approximate.flags;
  if (fixed_ftz)
    result.value = flush_subnormal(result.value);
  return result;
}

[[nodiscard]] Result<Fp64> canonical_nan_f64(Fp64 value) {
  constexpr Fp64 ptx_nan{0x7FFFFFFF00000000ULL};
  if (is_signaling_nan(value))
    return {ptx_nan,
            ExceptionFlags{static_cast<std::uint8_t>(ExceptionFlag::Invalid)}};
  return {ptx_nan, {}};
}

[[nodiscard]] Result<Fp64> invalid_nan_f64() {
  return {Fp64{0x7FFFFFFF00000000ULL},
          ExceptionFlags{static_cast<std::uint8_t>(ExceptionFlag::Invalid)}};
}

template <typename Function>
[[nodiscard]] Result<Fp64> f64_ftz_approx(Fp64 value, Function function) {
  value = flush_subnormal(value);
  value.bits &= 0xFFFFFFFF00000000ULL;
  if (is_nan(value))
    return canonical_nan_f64(value);
  auto result = function(value);
  if (is_nan(result.value)) {
    auto canonical = canonical_nan_f64(result.value);
    canonical.flags |= result.flags;
    return canonical;
  }
  result.value = flush_subnormal(result.value);
  result.value.bits &= 0xFFFFFFFF00000000ULL;
  return result;
}

}  // namespace

Result<Fp32> div_approx(Fp32 lhs, Fp32 rhs, ApproximationControl control) {
  validate_approximation_control<Operation::DivApprox, Fp32>(control);
  return SoftFloatBackend<Fp32>::div(lhs, rhs, exact_control(control));
}

Result<Fp32> div_full(Fp32 lhs, Fp32 rhs, ApproximationControl control) {
  validate_approximation_control<Operation::DivFull, Fp32>(control);
  return SoftFloatBackend<Fp32>::div(lhs, rhs, exact_control(control));
}

Result<Fp32> rcp_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::RcpApprox, Fp32>(control);
  return SoftFloatBackend<Fp32>::rcp(value, exact_control(control));
}

Result<Fp32> sqrt_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::SqrtApprox, Fp32>(control);
  return SoftFloatBackend<Fp32>::sqrt(value, exact_control(control));
}

Result<Fp32> rsqrt_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::RsqrtApprox, Fp32>(control);
  const auto root = SoftFloatBackend<Fp32>::sqrt(value, exact_control(control));
  auto result = SoftFloatBackend<Fp32>::rcp(root.value, exact_control(control));
  result.flags |= root.flags;
  return result;
}

Result<Fp32> sin_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::SinApprox, Fp32>(control);
  return host_unary(value, HostUnary::Sin, control);
}

Result<Fp32> cos_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::CosApprox, Fp32>(control);
  return host_unary(value, HostUnary::Cos, control);
}

Result<Fp32> lg2_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::Lg2Approx, Fp32>(control);
  return host_unary(value, HostUnary::Lg2, control);
}

Result<Fp32> ex2_approx(Fp32 value, ApproximationControl control) {
  validate_approximation_control<Operation::Ex2Approx, Fp32>(control);
  return host_unary(value, HostUnary::Ex2, control);
}

Result<Fp32> tanh_approx(Fp32 value) {
  static_assert(OperationTraits<Fp32, Operation::TanhApprox>::supported);
  return host_unary(value, HostUnary::Tanh);
}

Result<Fp64> rsqrt_approx(Fp64 value) {
  static_assert(OperationTraits<Fp64, Operation::RsqrtApprox>::supported);
  if (is_nan(value))
    return propagate_nan(value);
  if (is_negative(value) && !is_zero(value))
    return canonical_invalid_nan<Fp64>();
  const auto root = SoftFloatBackend<Fp64>::sqrt(value, {});
  auto result = SoftFloatBackend<Fp64>::rcp(root.value, {});
  result.flags |= root.flags;
  return result;
}

Result<Fp64> rcp_approx_ftz(Fp64 value) {
  static_assert(OperationTraits<Fp64, Operation::RcpApproxFtz>::supported);
  return f64_ftz_approx(
      value, [](Fp64 input) { return SoftFloatBackend<Fp64>::rcp(input, {}); });
}

Result<Fp64> rsqrt_approx_ftz(Fp64 value) {
  static_assert(OperationTraits<Fp64, Operation::RsqrtApproxFtz>::supported);
  return f64_ftz_approx(value, [](Fp64 input) {
    if (is_negative(input) && !is_zero(input))
      return invalid_nan_f64();
    const auto root = SoftFloatBackend<Fp64>::sqrt(input, {});
    auto result = SoftFloatBackend<Fp64>::rcp(root.value, {});
    result.flags |= root.flags;
    return result;
  });
}

Result<Fp16> tanh_approx(Fp16 value) {
  static_assert(OperationTraits<Fp16, Operation::TanhApprox>::supported);
  return low_unary(value, HostUnary::Tanh, false);
}

Result<Fp16> ex2_approx(Fp16 value) {
  static_assert(OperationTraits<Fp16, Operation::Ex2Approx>::supported);
  return low_unary(value, HostUnary::Ex2, false);
}

Result<Bf16> tanh_approx(Bf16 value) {
  static_assert(OperationTraits<Bf16, Operation::TanhApprox>::supported);
  return low_unary(value, HostUnary::Tanh, false);
}

Result<Bf16> ex2_approx(Bf16 value) {
  static_assert(OperationTraits<Bf16, Operation::Ex2Approx>::supported);
  return low_unary(value, HostUnary::Ex2, true);
}

}  // namespace ptxsim::fp::detail
