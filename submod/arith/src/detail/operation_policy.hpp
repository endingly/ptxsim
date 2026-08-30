#pragma once

#include <ptxsim/arith/concepts.hpp>
#include "internal_controls.hpp"
#include <ptxsim/arith/types.hpp>

#include <stdexcept>
#include <type_traits>

namespace ptxsim::arith::detail {

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
struct PublicOperation;
#define PTXSIM_PUBLIC_OPERATION(Internal, Public) \
  template <>                                      \
  struct PublicOperation<Operation::Internal> {    \
    static constexpr auto value = scalar_operation::Public; \
  }
PTXSIM_PUBLIC_OPERATION(Add, add);
PTXSIM_PUBLIC_OPERATION(Sub, sub);
PTXSIM_PUBLIC_OPERATION(Mul, mul);
PTXSIM_PUBLIC_OPERATION(Fma, fma);
PTXSIM_PUBLIC_OPERATION(Mad, mad);
PTXSIM_PUBLIC_OPERATION(Div, div);
PTXSIM_PUBLIC_OPERATION(Sqrt, sqrt);
PTXSIM_PUBLIC_OPERATION(Rcp, rcp);
PTXSIM_PUBLIC_OPERATION(Abs, abs);
PTXSIM_PUBLIC_OPERATION(Neg, neg);
PTXSIM_PUBLIC_OPERATION(Min, min);
PTXSIM_PUBLIC_OPERATION(Max, max);
PTXSIM_PUBLIC_OPERATION(Compare, compare);
#undef PTXSIM_PUBLIC_OPERATION

template <typename T, Operation Op>
struct PublicOperationTraits
    : floating_operation_control_capability<PublicOperation<Op>::value, T> {
  static constexpr bool supports_ftz =
      floating_operation_control_capability<PublicOperation<Op>::value,
                                            T>::supports(
          subnormal_mode::flush_input_and_output);
  static constexpr bool supports_directed_rounding =
      floating_operation_control_capability<PublicOperation<Op>::value,
                                            T>::supports(
          rounding_mode::toward_zero);
};

template <typename T, Operation Op>
  requires(Op == Operation::Add || Op == Operation::Sub ||
           Op == Operation::Mul || Op == Operation::Fma ||
           Op == Operation::Div || Op == Operation::Sqrt ||
           Op == Operation::Mad || Op == Operation::Rcp ||
           Op == Operation::Abs || Op == Operation::Neg ||
           Op == Operation::Min || Op == Operation::Max ||
           Op == Operation::Compare)
struct OperationTraits<T, Op> : PublicOperationTraits<T, Op> {};

template <Operation Op>
  requires(Op == Operation::Copysign || Op == Operation::Testp)
struct OperationTraits<float32_t, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};
template <Operation Op>
  requires(Op == Operation::Copysign || Op == Operation::Testp)
struct OperationTraits<float64_t, Op> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};

template <scalar_operation Op, approximation_mode Mode, typename T>
struct ApproximationOperationTraits {
  using Capability = special_function_operation_capability<Op, T>;
  static constexpr bool supported = Capability::supported &&
                                    Capability::supports(Mode);
  static constexpr bool supports_ftz =
      Capability::supports(subnormal_mode::flush_input_and_output);
  static constexpr bool supports_directed_rounding = false;
};
#define PTXSIM_APPROXIMATION_OPERATION(Internal, Public, Mode)             \
  template <typename T>                                                     \
  struct OperationTraits<T, Operation::Internal>                            \
      : ApproximationOperationTraits<scalar_operation::Public,             \
                                     approximation_mode::Mode, T> {}
PTXSIM_APPROXIMATION_OPERATION(DivApprox, div, ptx_approximate);
PTXSIM_APPROXIMATION_OPERATION(DivFull, div, ptx_full);
PTXSIM_APPROXIMATION_OPERATION(RcpApprox, rcp, ptx_approximate);
PTXSIM_APPROXIMATION_OPERATION(SqrtApprox, sqrt, ptx_approximate);
PTXSIM_APPROXIMATION_OPERATION(SinApprox, sin, ptx_approximate);
PTXSIM_APPROXIMATION_OPERATION(CosApprox, cos, ptx_approximate);
PTXSIM_APPROXIMATION_OPERATION(Lg2Approx, lg2, ptx_approximate);
#undef PTXSIM_APPROXIMATION_OPERATION

template <>
struct OperationTraits<float32_t, Operation::RsqrtApprox>
    : ApproximationOperationTraits<scalar_operation::rsqrt,
                                   approximation_mode::ptx_approximate,
                                   float32_t> {};
template <>
struct OperationTraits<float32_t, Operation::Ex2Approx>
    : ApproximationOperationTraits<scalar_operation::ex2,
                                   approximation_mode::ptx_approximate,
                                   float32_t> {};
template <>
struct OperationTraits<float16_t, Operation::Ex2Approx>
    : ApproximationOperationTraits<scalar_operation::ex2,
                                   approximation_mode::ptx_approximate,
                                   float16_t> {};
template <>
struct OperationTraits<bfloat16_t, Operation::Ex2Approx>
    : ApproximationOperationTraits<scalar_operation::ex2,
                                   approximation_mode::ptx_approximate,
                                   bfloat16_t> {};

template <>
struct OperationTraits<float32_t, Operation::TanhApprox> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};
template <>
struct OperationTraits<float64_t, Operation::RcpApprox> {
  static constexpr bool supported = false;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};
template <>
struct OperationTraits<float64_t, Operation::RsqrtApprox> {
  static constexpr bool supported =
      special_function_operation_capability<scalar_operation::rsqrt,
                                            float64_t>::supports(
          {.approximation = approximation_mode::ptx_approximate,
           .subnormal = subnormal_mode::preserve});
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};
template <Operation Op>
  requires(Op == Operation::RcpApproxFtz || Op == Operation::RsqrtApproxFtz)
struct OperationTraits<float64_t, Op> {
  static constexpr auto operation = Op == Operation::RcpApproxFtz
                                        ? scalar_operation::rcp
                                        : scalar_operation::rsqrt;
  static constexpr bool supported =
      special_function_operation_capability<operation, float64_t>::supports(
          {.approximation = approximation_mode::ptx_approximate,
           .subnormal = subnormal_mode::flush_input_and_output});
  static constexpr bool supports_ftz = true;
  static constexpr bool supports_directed_rounding = false;
};
template <>
struct OperationTraits<float16_t, Operation::TanhApprox> {
  static constexpr bool supported = true;
  static constexpr bool supports_ftz = false;
  static constexpr bool supports_directed_rounding = false;
};
template <>
struct OperationTraits<bfloat16_t, Operation::TanhApprox> {
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

template <scalar_operation Op, typename Result, typename... Operands>
inline void validate_mixed_control(ArithmeticControl control) {
  using Capability =
      floating_operation_control_capability<Op, Result, Operands...>;
  static_assert(Capability::supported,
                "mixed operation is not supported for these formats");
  validate_rounding(control.rounding);
  const auto directed = control.rounding != RoundingMode::NearestEven;
  if (directed && !Capability::supports(rounding_mode::toward_zero))
    throw std::invalid_argument(
        "mixed floating-point operation only supports nearest-even rounding");
  if (resolve_subnormal(control) != SubnormalMode::Preserve &&
      !Capability::supports(subnormal_mode::flush_input_and_output))
    throw std::invalid_argument(
        "mixed floating-point operations do not support FTZ");
}

template <Operation Op, typename T>
inline void validate_approximation_control(ApproximationControl control) {
  if (!OperationTraits<T, Op>::supported)
    throw std::invalid_argument("approximation is not supported for this format");
  if (control.flush_subnormal && !OperationTraits<T, Op>::supports_ftz) {
    throw std::invalid_argument("approximation/format does not support FTZ");
  }
}

}  // namespace ptxsim::arith::detail
