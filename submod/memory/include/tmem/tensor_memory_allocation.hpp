#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <ptxsim/memory/tmem/tensor_memory_address.hpp>

namespace ptxsim::memory {

namespace detail {
struct TensorMemoryManagerState;
}

/**
 * @brief Stable identity for one manager-owned Tensor Memory space.
 */
class TensorMemorySpaceHandle final {
 public:
  constexpr bool operator==(const TensorMemorySpaceHandle&) const noexcept =
      default;

 private:
  constexpr TensorMemorySpaceHandle(std::uint64_t manager_token,
                                    std::size_t index,
                                    std::uint64_t generation) noexcept
      : manager_token_(manager_token),
        index_(index),
        generation_(generation) {}

  std::uint64_t manager_token_ = 0;
  std::size_t index_ = 0;
  std::uint64_t generation_ = 0;

  friend struct detail::TensorMemoryManagerState;
  friend class TensorMemoryManager;
};

/**
 * @brief Exact identity and column range of one Tensor Memory allocation.
 */
class TensorMemoryAllocation final {
 public:
  [[nodiscard]] constexpr auto base() const noexcept -> TensorMemoryAddress {
    return base_;
  }

  [[nodiscard]] constexpr auto column_count() const noexcept
      -> std::uint16_t {
    return column_count_;
  }

  constexpr bool operator==(const TensorMemoryAllocation&) const noexcept =
      default;

 private:
  constexpr TensorMemoryAllocation(TensorMemoryAddress base,
                                   std::uint16_t column_count,
                                   std::uint64_t manager_token,
                                   std::uint64_t incarnation,
                                   std::uint8_t group_size) noexcept
      : base_(base),
        column_count_(column_count),
        manager_token_(manager_token),
        incarnation_(incarnation),
        group_size_(group_size) {}

  TensorMemoryAddress base_;
  std::uint16_t column_count_;
  std::uint64_t manager_token_;
  std::uint64_t incarnation_;
  std::uint8_t group_size_;

  friend class TensorMemoryManager;
};

}  // namespace ptxsim::memory
