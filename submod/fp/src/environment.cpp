#include <ptxsim/fp/environment.hpp>

#include <stdexcept>
#include <utility>

extern "C" {
#include <softfloat/softfloat.h>
}

namespace ptxsim::fp {
namespace {

[[nodiscard]] std::uint_fast8_t to_softfloat_rounding_mode(
    RoundingMode mode) noexcept {
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

[[nodiscard]] ExceptionFlags to_exception_flags(
    std::uint_fast8_t flags) noexcept {
  std::uint8_t result = 0;
  if ((flags & softfloat_flag_inexact) != 0)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Inexact);
  if ((flags & softfloat_flag_underflow) != 0)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Underflow);
  if ((flags & softfloat_flag_overflow) != 0)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Overflow);
  if ((flags & softfloat_flag_infinite) != 0)
    result |= static_cast<std::uint8_t>(ExceptionFlag::DivideByZero);
  if ((flags & softfloat_flag_invalid) != 0)
    result |= static_cast<std::uint8_t>(ExceptionFlag::Invalid);
  return ExceptionFlags{result};
}

class ScopedSoftFloatState {
 public:
  explicit ScopedSoftFloatState(RoundingMode rounding) noexcept
      : rounding_(softfloat_roundingMode),
        tininess_(softfloat_detectTininess),
        flags_(softfloat_exceptionFlags) {
    softfloat_roundingMode = to_softfloat_rounding_mode(rounding);
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
  }

  ~ScopedSoftFloatState() {
    softfloat_roundingMode = rounding_;
    softfloat_detectTininess = tininess_;
    softfloat_exceptionFlags = flags_;
  }

  ScopedSoftFloatState(const ScopedSoftFloatState&) = delete;
  ScopedSoftFloatState& operator=(const ScopedSoftFloatState&) = delete;

  [[nodiscard]] ExceptionFlags flags() const noexcept {
    return to_exception_flags(softfloat_exceptionFlags);
  }

 private:
  std::uint_fast8_t rounding_;
  std::uint_fast8_t tininess_;
  std::uint_fast8_t flags_;
};

void require_f64_control(ArithmeticControl control) {
  if (control.flush_subnormal) {
    throw std::invalid_argument(
        "F64 operations do not support flush_subnormal; PTX .ftz is F32-only");
  }
}

template <typename Operation>
[[nodiscard]] Result<Fp32> execute_f32(ArithmeticControl control,
                                       Operation operation) {
  ScopedSoftFloatState state{control.rounding};
  auto value = operation();
  if (control.flush_subnormal)
    value = flush_subnormal(value);
  return Result<Fp32>{value, state.flags()};
}

template <typename Operation>
[[nodiscard]] Result<Fp64> execute_f64(ArithmeticControl control,
                                       Operation operation) {
  require_f64_control(control);
  ScopedSoftFloatState state{control.rounding};
  return Result<Fp64>{operation(), state.flags()};
}

template <typename T, typename Operation>
[[nodiscard]] Result<T> execute_conversion(RoundingMode rounding,
                                           Operation operation) {
  ScopedSoftFloatState state{rounding};
  return Result<T>{operation(), state.flags()};
}

[[nodiscard]] float32_t to_softfloat(Fp32 value) noexcept {
  return float32_t{.v = value.bits};
}

[[nodiscard]] float64_t to_softfloat(Fp64 value) noexcept {
  return float64_t{.v = value.bits};
}

[[nodiscard]] Fp32 from_softfloat(float32_t value) noexcept {
  return Fp32{value.v};
}

[[nodiscard]] Fp64 from_softfloat(float64_t value) noexcept {
  return Fp64{value.v};
}

[[nodiscard]] Fp32 ftz_input(Fp32 value, ArithmeticControl control) noexcept {
  return control.flush_subnormal ? flush_subnormal(value) : value;
}

}  // namespace

Result<Fp32> Environment::add(Fp32 lhs, Fp32 rhs,
                              ArithmeticControl control) const {
  lhs = ftz_input(lhs, control);
  rhs = ftz_input(rhs, control);
  return execute_f32(control, [=] {
    return from_softfloat(f32_add(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp32> Environment::sub(Fp32 lhs, Fp32 rhs,
                              ArithmeticControl control) const {
  lhs = ftz_input(lhs, control);
  rhs = ftz_input(rhs, control);
  return execute_f32(control, [=] {
    return from_softfloat(f32_sub(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp32> Environment::mul(Fp32 lhs, Fp32 rhs,
                              ArithmeticControl control) const {
  lhs = ftz_input(lhs, control);
  rhs = ftz_input(rhs, control);
  return execute_f32(control, [=] {
    return from_softfloat(f32_mul(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp32> Environment::fma(Fp32 a, Fp32 b, Fp32 c,
                              ArithmeticControl control) const {
  a = ftz_input(a, control);
  b = ftz_input(b, control);
  c = ftz_input(c, control);
  return execute_f32(control, [=] {
    return from_softfloat(
        f32_mulAdd(to_softfloat(a), to_softfloat(b), to_softfloat(c)));
  });
}

Result<Fp32> Environment::div(Fp32 lhs, Fp32 rhs,
                              ArithmeticControl control) const {
  lhs = ftz_input(lhs, control);
  rhs = ftz_input(rhs, control);
  return execute_f32(control, [=] {
    return from_softfloat(f32_div(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp32> Environment::sqrt(Fp32 value, ArithmeticControl control) const {
  value = ftz_input(value, control);
  return execute_f32(
      control, [=] { return from_softfloat(f32_sqrt(to_softfloat(value))); });
}

Result<Fp64> Environment::add(Fp64 lhs, Fp64 rhs,
                              ArithmeticControl control) const {
  return execute_f64(control, [=] {
    return from_softfloat(f64_add(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp64> Environment::sub(Fp64 lhs, Fp64 rhs,
                              ArithmeticControl control) const {
  return execute_f64(control, [=] {
    return from_softfloat(f64_sub(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp64> Environment::mul(Fp64 lhs, Fp64 rhs,
                              ArithmeticControl control) const {
  return execute_f64(control, [=] {
    return from_softfloat(f64_mul(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp64> Environment::fma(Fp64 a, Fp64 b, Fp64 c,
                              ArithmeticControl control) const {
  return execute_f64(control, [=] {
    return from_softfloat(
        f64_mulAdd(to_softfloat(a), to_softfloat(b), to_softfloat(c)));
  });
}

Result<Fp64> Environment::div(Fp64 lhs, Fp64 rhs,
                              ArithmeticControl control) const {
  return execute_f64(control, [=] {
    return from_softfloat(f64_div(to_softfloat(lhs), to_softfloat(rhs)));
  });
}

Result<Fp64> Environment::sqrt(Fp64 value, ArithmeticControl control) const {
  return execute_f64(
      control, [=] { return from_softfloat(f64_sqrt(to_softfloat(value))); });
}

Result<Fp32> Environment::i32_to_f32(std::int32_t value,
                                     RoundingMode rounding) const {
  return execute_conversion<Fp32>(
      rounding, [=] { return from_softfloat(::i32_to_f32(value)); });
}

Result<std::int32_t> Environment::f32_to_i32(Fp32 value,
                                             RoundingMode rounding) const {
  return execute_conversion<std::int32_t>(rounding, [=] {
    return static_cast<std::int32_t>(::f32_to_i32(
        to_softfloat(value), to_softfloat_rounding_mode(rounding), true));
  });
}

Result<Fp32> Environment::u32_to_f32(std::uint32_t value,
                                     RoundingMode rounding) const {
  return execute_conversion<Fp32>(
      rounding, [=] { return from_softfloat(ui32_to_f32(value)); });
}

Result<std::uint32_t> Environment::f32_to_u32(Fp32 value,
                                              RoundingMode rounding) const {
  return execute_conversion<std::uint32_t>(rounding, [=] {
    return static_cast<std::uint32_t>(f32_to_ui32(
        to_softfloat(value), to_softfloat_rounding_mode(rounding), true));
  });
}

Result<Fp64> Environment::i32_to_f64(std::int32_t value,
                                     RoundingMode rounding) const {
  return execute_conversion<Fp64>(
      rounding, [=] { return from_softfloat(::i32_to_f64(value)); });
}

Result<std::int32_t> Environment::f64_to_i32(Fp64 value,
                                             RoundingMode rounding) const {
  return execute_conversion<std::int32_t>(rounding, [=] {
    return static_cast<std::int32_t>(::f64_to_i32(
        to_softfloat(value), to_softfloat_rounding_mode(rounding), true));
  });
}

Result<Fp64> Environment::u32_to_f64(std::uint32_t value,
                                     RoundingMode rounding) const {
  return execute_conversion<Fp64>(
      rounding, [=] { return from_softfloat(ui32_to_f64(value)); });
}

Result<std::uint32_t> Environment::f64_to_u32(Fp64 value,
                                              RoundingMode rounding) const {
  return execute_conversion<std::uint32_t>(rounding, [=] {
    return static_cast<std::uint32_t>(f64_to_ui32(
        to_softfloat(value), to_softfloat_rounding_mode(rounding), true));
  });
}

Result<Fp64> Environment::f32_to_f64(Fp32 value) const {
  return execute_conversion<Fp64>(RoundingMode::NearestEven, [=] {
    return from_softfloat(::f32_to_f64(to_softfloat(value)));
  });
}

Result<Fp32> Environment::f64_to_f32(Fp64 value, RoundingMode rounding) const {
  return execute_conversion<Fp32>(rounding, [=] {
    return from_softfloat(::f64_to_f32(to_softfloat(value)));
  });
}

}  // namespace ptxsim::fp
