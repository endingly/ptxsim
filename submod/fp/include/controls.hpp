#pragma once

namespace ptxsim::fp {

enum class RoundingMode {
  NearestEven,
  TowardZero,
  TowardNegative,
  TowardPositive,
};

enum class SubnormalMode { Preserve, FlushToSignedZero };

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

}  // namespace ptxsim::fp
