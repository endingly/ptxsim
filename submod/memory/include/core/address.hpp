#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace ptxsim::memory {

/**
 * @brief Byte address within one resolved byte-addressable memory region.
 *
 * Address does not identify a PTX state space or execution scope by itself.
 * Those properties are supplied by the surrounding address-space manager.
 *
 * The value is interpreted as a byte offset into a MemoryRegion after address
 * resolution has completed.
 */
struct Address {
  std::uint64_t value = 0;

  auto operator<=>(const Address&) const = default;
};

/**
 * @brief A contiguous byte range beginning at an Address.
 */
struct AddressRange {
  Address begin{};
  std::size_t size = 0;

  auto operator<=>(const AddressRange&) const = default;
};

/**
 * @brief Return true if @p value is a non-zero power of two.
 */
[[nodiscard]]
constexpr bool is_power_of_two(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

/**
 * @brief Return whether an address satisfies the requested byte alignment.
 *
 * Alignment must be a non-zero power of two.
 */
[[nodiscard]]
constexpr bool is_aligned(Address address, std::size_t alignment) noexcept {
  if (!is_power_of_two(alignment)) {
    return false;
  }

  return (address.value & static_cast<std::uint64_t>(alignment - 1)) == 0;
}

/**
 * @brief Add a byte offset to an address with overflow detection.
 *
 * Returns std::nullopt when the resulting architectural address would exceed
 * the 64-bit address representation.
 */
[[nodiscard]]
constexpr std::optional<Address> checked_add(Address address,
                                             std::size_t offset) noexcept {
  const auto delta = static_cast<std::uint64_t>(offset);

  if (delta > std::numeric_limits<std::uint64_t>::max() - address.value) {
    return std::nullopt;
  }

  return Address{
      address.value + delta,
  };
}

}  // namespace ptxsim::memory