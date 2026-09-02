#pragma once

#include <compare>
#include <cstdint>

namespace ptxsim::memory {

inline constexpr std::uint16_t kTensorMemoryLaneCount = 128;
inline constexpr std::uint16_t kTensorMemoryColumnCount = 512;

/**
 * @brief PTX 32-bit Tensor Memory address.
 *
 * Bits 31:16 encode the lane and bits 15:0 encode the column.
 */
class TensorMemoryAddress final {
 public:
  explicit constexpr TensorMemoryAddress(std::uint32_t value = 0) noexcept
      : value_(value) {}

  [[nodiscard]] static constexpr auto from_indices(std::uint16_t lane,
                                                    std::uint16_t column)
      noexcept -> TensorMemoryAddress {
    return TensorMemoryAddress{(static_cast<std::uint32_t>(lane) << 16) |
                               static_cast<std::uint32_t>(column)};
  }

  [[nodiscard]] constexpr auto value() const noexcept -> std::uint32_t {
    return value_;
  }

  [[nodiscard]] constexpr auto lane() const noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(value_ >> 16);
  }

  [[nodiscard]] constexpr auto column() const noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(value_);
  }

  constexpr auto operator<=>(const TensorMemoryAddress&) const noexcept =
      default;

 private:
  std::uint32_t value_ = 0;
};

}  // namespace ptxsim::memory
