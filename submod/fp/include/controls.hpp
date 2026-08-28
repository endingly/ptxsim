#pragma once

namespace ptxsim::fp {

enum class RoundingMode {
  NearestEven,
  TowardZero,
  TowardNegative,
  TowardPositive,
};

enum class SubnormalMode { Preserve, FlushToSignedZero };

// PTX set/setp comparison predicates.  Boolean composition belongs to the
// instruction executor, which owns predicate registers.
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
  NaN,
};

enum class TestpOp { Finite, Infinite, Number, NotANumber, Normal, Subnormal };

struct ArithmeticControl {
  RoundingMode rounding = RoundingMode::NearestEven;
  // Source compatibility for the original aggregate API. Policy code is the
  // only code allowed to interpret this compatibility field.
  bool flush_subnormal = false;
  SubnormalMode subnormal = SubnormalMode::Preserve;
};

struct ConversionControl {
  RoundingMode rounding = RoundingMode::NearestEven;
  bool satfinite = false;
};

struct MinMaxControl {
  // Maps to PTX's .NaN modifier: any NaN input produces canonical NaN.
  // Without it, one NaN selects the numeric operand and two NaNs select a
  // deterministic canonical NaN.
  bool propagate_nan = false;
  bool absolute = false;
  bool xor_sign = false;
};

struct ApproximationControl {
  // PTX .ftz is optional only on operations whose API accepts this type.
  bool flush_subnormal = false;
};

}  // namespace ptxsim::fp
