#pragma once
#include <ptxsim/arith/context.hpp>
namespace ptxsim::arith::detail {
enum class RoundingMode {
  NearestEven,
  TowardZero,
  TowardNegative,
  TowardPositive
};
enum class SubnormalMode { Preserve, FlushToSignedZero };
enum class CompareOp {
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  EqualUnordered,
  NotEqualUnordered,
  LessUnordered,
  LessEqualUnordered,
  GreaterUnordered,
  GreaterEqualUnordered,
  Number,
  NaN
};
enum class TestpOp { Finite, Infinite, Number, NotANumber, Normal, Subnormal };
struct ArithmeticControl {
  RoundingMode rounding = RoundingMode::NearestEven;
  bool flush_subnormal = false;
  SubnormalMode subnormal = SubnormalMode::Preserve;
};
struct ConversionControl {
  RoundingMode rounding = RoundingMode::NearestEven;
  bool satfinite = false;
};
struct MinMaxControl {
  bool propagate_nan = false;
  bool absolute = false;
  bool xor_sign = false;
};
struct ApproximationControl {
  bool flush_subnormal = false;
  // Unlike a raw integer selector, this carries the exact PTX revision and
  // provenance all the way to the approximation backend.
  approximation_profile profile{};
};
}  // namespace ptxsim::arith::detail
