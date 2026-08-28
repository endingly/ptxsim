#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/types.hpp>

#include <stdexcept>
#include <type_traits>

namespace ptxsim::fp::detail {

enum class Operation {
  Add,
  Sub,
  Mul,
  Fma,
  Div,
  Sqrt,
  Abs,
  Neg,
  Min,
  Max,
  Compare,
  Copysign,
  Testp,
  Mad,
  Rcp,
  DivApprox,
  DivFull,
  RcpApprox,
  SqrtApprox,
  RsqrtApprox,
  SinApprox,
  CosApprox,
  Lg2Approx,
  Ex2Approx,
  TanhApprox,
  RcpApproxFtz,
  RsqrtApproxFtz,
  Convert,
};

template <typename T, Operation Op>
struct OperationTraits {
  static constexpr bool supported = false;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma ||
           Op == Operation::Div || Op == Operation::Sqrt ||
           Op == Operation::Mad || Op == Operation::Rcp)
struct OperationTraits<Fp32, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = true;
};

template <Operation Op>
  requires(Op == Operation::Abs || Op == Operation::Neg ||
           Op == Operation::Min || Op == Operation::Max ||
           Op == Operation::Compare)
struct OperationTraits<Fp32, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Copysign || Op == Operation::Testp)
struct OperationTraits<Fp32, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma ||
           Op == Operation::Div || Op == Operation::Sqrt ||
           Op == Operation::Mad || Op == Operation::Rcp)
struct OperationTraits<Fp64, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = true;
};

template <Operation Op>
  requires(Op == Operation::DivApprox || Op == Operation::DivFull ||
           Op == Operation::RcpApprox || Op == Operation::SqrtApprox ||
           Op == Operation::RsqrtApprox || Op == Operation::SinApprox ||
           Op == Operation::CosApprox || Op == Operation::Lg2Approx ||
           Op == Operation::Ex2Approx)
struct OperationTraits<Fp32, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = false;
};

template <>
struct OperationTraits<Fp32, Operation::TanhApprox> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <>
struct OperationTraits<Fp64, Operation::RsqrtApprox> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::RcpApproxFtz || Op == Operation::RsqrtApproxFtz)
struct OperationTraits<Fp64, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Abs || Op == Operation::Neg ||
           Op == Operation::Min || Op == Operation::Max ||
           Op == Operation::Compare || Op == Operation::Copysign ||
           Op == Operation::Testp)
struct OperationTraits<Fp64, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma)
struct OperationTraits<Fp16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Abs || Op == Operation::Neg ||
           Op == Operation::Min || Op == Operation::Max ||
           Op == Operation::Compare)
struct OperationTraits<Fp16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma)
struct OperationTraits<Bf16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::TanhApprox || Op == Operation::Ex2Approx)
struct OperationTraits<Fp16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::TanhApprox || Op == Operation::Ex2Approx)
struct OperationTraits<Bf16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = Op == Operation::Ex2Approx;
  static constexpr bool supports_directed_rounding = false;
};

template <Operation Op>
  requires(Op == Operation::Abs || Op == Operation::Neg ||
           Op == Operation::Min || Op == Operation::Max ||
           Op == Operation::Compare)
struct OperationTraits<Bf16, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
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
  if (control.rounding != RoundingMode::NearestEven &&
      !OperationTraits<T, Op>::supports_directed_rounding) {
    throw std::invalid_argument(
        "operation/format only supports nearest-even rounding");
  }
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

inline void validate_mixed_control(ArithmeticControl control) {
  validate_rounding(control.rounding);
  if (resolve_subnormal(control) != SubnormalMode::Preserve)
    throw std::invalid_argument(
        "mixed floating-point operations do not support FTZ");
}

template <Operation Op, typename T>
inline void validate_approximation_control(ApproximationControl control) {
  static_assert(OperationTraits<T, Op>::supported,
                "approximation is not supported for this format");
  if (control.flush_subnormal && !OperationTraits<T, Op>::supports_ftz) {
    throw std::invalid_argument("approximation/format does not support FTZ");
  }
}

}  // namespace ptxsim::fp::detail
