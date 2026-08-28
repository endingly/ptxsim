#pragma once

#include <ptxsim/arith/controls.hpp>
#include "internal_controls.hpp"
#include "internal_result.hpp"

#include <cstdint>

namespace ptxsim::arith::detail {

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

}  // namespace ptxsim::arith::detail
