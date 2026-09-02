#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

#include <ptxsim/memory/tmem/tensor_memory_allocation.hpp>

namespace ptxsim::memory {

enum class TensorMemoryErrorCode : std::uint8_t {
  stale_space,
  invalid_group,
  invalid_column_count,
  allocation_request_increase,
  allocation_permit_relinquished,
  allocation_exhausted,
  outstanding_allocations,
  allocation_mismatch,
  invalid_address,
  unallocated_address,
};

/**
 * @brief Structured failure from Tensor Memory storage or allocation.
 */
struct TensorMemoryError {
  TensorMemoryErrorCode code;
  std::optional<std::size_t> space_index;
  std::optional<TensorMemoryAddress> address;
  std::uint32_t requested_columns = 0;

  constexpr bool operator==(const TensorMemoryError&) const noexcept = default;
};

/**
 * @brief Owns fixed-geometry Tensor Memory spaces and their column allocations.
 *
 * Each space contains 128 lanes by 512 columns of 32-bit cells. Snapshot order
 * is lane-major: all requested columns of lane 0, then lane 1, and so on.
 * Newly allocated cells are cleared to zero for deterministic inspection.
 */
class TensorMemoryManager final {
 public:
  TensorMemoryManager();
  ~TensorMemoryManager();

  TensorMemoryManager(const TensorMemoryManager&) = delete;
  TensorMemoryManager& operator=(const TensorMemoryManager&) = delete;
  TensorMemoryManager(TensorMemoryManager&&) = delete;
  TensorMemoryManager& operator=(TensorMemoryManager&&) = delete;

  [[nodiscard]] auto create_space() -> TensorMemorySpaceHandle;
  [[nodiscard]] auto destroy(TensorMemorySpaceHandle handle)
      -> std::expected<void, TensorMemoryError>;

  [[nodiscard]] auto allocation_permitted(
      TensorMemorySpaceHandle handle) const
      -> std::expected<bool, TensorMemoryError>;

  [[nodiscard]] auto allocate(TensorMemorySpaceHandle handle,
                              std::uint32_t column_count)
      -> std::expected<TensorMemoryAllocation, TensorMemoryError>;
  [[nodiscard]] auto allocate(TensorMemorySpaceHandle first,
                              TensorMemorySpaceHandle second,
                              std::uint32_t column_count)
      -> std::expected<TensorMemoryAllocation, TensorMemoryError>;

  [[nodiscard]] auto deallocate(TensorMemorySpaceHandle handle,
                                TensorMemoryAllocation allocation)
      -> std::expected<void, TensorMemoryError>;
  [[nodiscard]] auto deallocate(TensorMemorySpaceHandle first,
                                TensorMemorySpaceHandle second,
                                TensorMemoryAllocation allocation)
      -> std::expected<void, TensorMemoryError>;

  [[nodiscard]] auto relinquish_allocation_permit(
      TensorMemorySpaceHandle handle)
      -> std::expected<void, TensorMemoryError>;
  [[nodiscard]] auto relinquish_allocation_permit(
      TensorMemorySpaceHandle first, TensorMemorySpaceHandle second)
      -> std::expected<void, TensorMemoryError>;

  [[nodiscard]] auto read(TensorMemorySpaceHandle handle,
                          TensorMemoryAddress address) const
      -> std::expected<std::uint32_t, TensorMemoryError>;
  [[nodiscard]] auto write(TensorMemorySpaceHandle handle,
                           TensorMemoryAddress address, std::uint32_t value)
      -> std::expected<void, TensorMemoryError>;
  [[nodiscard]] auto snapshot(TensorMemorySpaceHandle handle,
                              TensorMemoryAllocation allocation) const
      -> std::expected<std::vector<std::uint32_t>, TensorMemoryError>;

 private:
  std::shared_ptr<detail::TensorMemoryManagerState> state_;
};

}  // namespace ptxsim::memory
