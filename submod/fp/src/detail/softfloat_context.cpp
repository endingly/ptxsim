#include "softfloat_context.hpp"

#include <utility>

extern "C" {
#include <softfloat/softfloat.h>
}

namespace ptxsim::fp::detail {

std::uint_fast8_t to_softfloat_rounding_mode(RoundingMode mode) noexcept {
  switch (mode) {
    case RoundingMode::NearestEven:
      return softfloat_round_near_even;
    case RoundingMode::TowardZero:
      return softfloat_round_minMag;
    case RoundingMode::TowardNegative:
      return softfloat_round_min;
    case RoundingMode::TowardPositive:
      return softfloat_round_max;
  }
  std::unreachable();
}

SoftFloatContext::SoftFloatContext(RoundingMode rounding) noexcept
    : rounding_(softfloat_roundingMode),
      tininess_(softfloat_detectTininess),
      flags_(softfloat_exceptionFlags) {
  // The configured SoftFloat target declares these variables THREAD_LOCAL.
  softfloat_roundingMode = to_softfloat_rounding_mode(rounding);
  softfloat_detectTininess = softfloat_tininess_afterRounding;
  softfloat_exceptionFlags = 0;
}

SoftFloatContext::~SoftFloatContext() {
  softfloat_roundingMode = rounding_;
  softfloat_detectTininess = tininess_;
  softfloat_exceptionFlags = flags_;
}

ExceptionFlags SoftFloatContext::flags() const noexcept {
  std::uint8_t result = 0;
  if (softfloat_exceptionFlags & softfloat_flag_inexact)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Inexact);
  if (softfloat_exceptionFlags & softfloat_flag_underflow)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Underflow);
  if (softfloat_exceptionFlags & softfloat_flag_overflow)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Overflow);
  if (softfloat_exceptionFlags & softfloat_flag_infinite)
    result |= static_cast<std::uint8_t>(ExceptionFlag::DivideByZero);
  if (softfloat_exceptionFlags & softfloat_flag_invalid)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Invalid);
  return ExceptionFlags{result};
}

}  // namespace ptxsim::fp::detail
