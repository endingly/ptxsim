#pragma once

#include <ptxsim/fp/controls.hpp>
#include <ptxsim/fp/exceptions.hpp>

#include <cstdint>

namespace ptxsim::fp::detail {

class SoftFloatContext {
 public:
  explicit SoftFloatContext(RoundingMode rounding) noexcept;
  ~SoftFloatContext();
  SoftFloatContext(const SoftFloatContext&) = delete;
  SoftFloatContext& operator=(const SoftFloatContext&) = delete;

  [[nodiscard]] ExceptionFlags flags() const noexcept;

 private:
  std::uint_fast8_t rounding_;
  std::uint_fast8_t tininess_;
  std::uint_fast8_t flags_;
};

[[nodiscard]] std::uint_fast8_t to_softfloat_rounding_mode(
    RoundingMode mode) noexcept;

}  // namespace ptxsim::fp::detail
