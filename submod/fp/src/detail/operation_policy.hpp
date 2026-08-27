#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/types.hpp>

#include <stdexcept>
#include <type_traits>

namespace ptxsim::fp::detail {

enum class Operation { Add, Sub, Mul, Fma, Div, Sqrt, Convert };

template <typename T, Operation Op>
struct OperationTraits {
  static constexpr bool supported = false;
  static constexpr bool supports_ftz = false;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma ||
           Op == Operation::Div || Op == Operation::Sqrt)
struct OperationTraits<Fp32, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma ||
           Op == Operation::Div || Op == Operation::Sqrt)
struct OperationTraits<Fp64, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub || Op == Operation::Mul ||
           Op == Operation::Fma)
struct OperationTraits<Bf16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
};

[[nodiscard]] inline SubnormalMode resolve_subnormal(
    ArithmeticControl control) noexcept {
  return control.flush_subnormal ? SubnormalMode::FlushToSignedZero
                                 : control.subnormal;
}

inline void validate_rounding(RoundingMode rounding) {
  switch (rounding) {
    case RoundingMode::NearestEven:
    case RoundingMode::TowardZero:
    case RoundingMode::TowardNegative:
    case RoundingMode::TowardPositive:
      return;
  }
  throw std::invalid_argument("invalid floating-point rounding mode");
}

template <Operation Op, typename T>
inline void validate_control(ArithmeticControl control) {
  static_assert(OperationTraits<T, Op>::supported,
                "operation is not supported for this format");
  validate_rounding(control.rounding);
  if (resolve_subnormal(control) != SubnormalMode::Preserve &&
      !OperationTraits<T, Op>::supports_ftz) {
    throw std::invalid_argument("operation/format does not support FTZ");
  }
}

inline void validate_conversion_control(ConversionControl control) {
  validate_rounding(control.rounding);
}

inline void validate_exact_widening_control(ConversionControl control) {
  validate_conversion_control(control);
  if (control.rounding != RoundingMode::NearestEven || control.satfinite) {
    throw std::invalid_argument(
        "exact widening conversion accepts no rounding or saturation control");
  }
}

}  // namespace ptxsim::fp::detail
