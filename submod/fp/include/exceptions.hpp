#pragma once

#include <cstdint>

namespace ptxsim::fp {

enum class ExceptionFlag : std::uint8_t {
  Inexact = 1u << 0,
  Underflow = 1u << 1,
  Overflow = 1u << 2,
  DivideByZero = 1u << 3,
  Invalid = 1u << 4,
};

class ExceptionFlags {
 public:
  constexpr ExceptionFlags() = default;

  constexpr explicit ExceptionFlags(std::uint8_t bits) : bits_(bits) {}

  [[nodiscard]]
  constexpr std::uint8_t bits() const noexcept {
    return bits_;
  }

  [[nodiscard]]
  constexpr bool contains(ExceptionFlag flag) const noexcept {
    return (bits_ & static_cast<std::uint8_t>(flag)) != 0;
  }

  friend constexpr ExceptionFlags operator|(ExceptionFlags lhs,
                                            ExceptionFlags rhs) noexcept {
    return ExceptionFlags{static_cast<std::uint8_t>(lhs.bits_ | rhs.bits_)};
  }

 private:
  std::uint8_t bits_{};
};

template <typename T>
struct Result {
  T value;
  ExceptionFlags flags;
};

}  // namespace ptxsim::fp
