#include <ptxsim/memory/tmem/tensor_memory_manager.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ptxsim::memory {
namespace {

auto next_manager_token() noexcept -> std::uint64_t {
  static std::atomic<std::uint64_t> next{1};
  auto token = next.fetch_add(1, std::memory_order_relaxed);
  return token == 0 ? next.fetch_add(1, std::memory_order_relaxed) : token;
}

auto error(TensorMemoryErrorCode code,
           std::optional<std::size_t> space_index = std::nullopt,
           std::optional<TensorMemoryAddress> address = std::nullopt,
           std::uint32_t requested_columns = 0) -> TensorMemoryError {
  return {code, space_index, address, requested_columns};
}

auto valid_column_count(std::uint32_t count) noexcept -> bool {
  return count >= 32 && count <= kTensorMemoryColumnCount &&
         count % 32 == 0 && (count & (count - 1)) == 0;
}

auto cell_index(std::uint16_t lane, std::uint16_t column) noexcept
    -> std::size_t {
  return static_cast<std::size_t>(lane) * kTensorMemoryColumnCount + column;
}

}  // namespace

namespace detail {

struct TensorMemoryManagerState {
  struct AllocationRecord {
    std::uint16_t column_count;
    std::uint64_t incarnation;
    std::uint8_t group_size;
  };

  struct Space {
    std::vector<std::uint32_t> cells = std::vector<std::uint32_t>(
        static_cast<std::size_t>(kTensorMemoryLaneCount) *
        kTensorMemoryColumnCount);
    std::array<bool, kTensorMemoryColumnCount> occupied{};
    std::map<std::uint16_t, AllocationRecord> allocations;
    std::optional<std::uint32_t> previous_request;
    bool allocation_permitted = true;
  };

  struct Slot {
    std::optional<Space> space;
    std::uint64_t generation = 1;
  };

  explicit TensorMemoryManagerState(std::uint64_t token) : token(token) {}

  [[nodiscard]] auto find(const TensorMemorySpaceHandle& handle)
      -> std::expected<Space*, TensorMemoryError> {
    if (handle.manager_token_ != token || handle.index_ >= spaces.size()) {
      return std::unexpected(
          error(TensorMemoryErrorCode::stale_space, handle.index_));
    }
    auto& slot = spaces[handle.index_];
    if (!slot.space || slot.generation != handle.generation_) {
      return std::unexpected(
          error(TensorMemoryErrorCode::stale_space, handle.index_));
    }
    return &*slot.space;
  }

  std::uint64_t token;
  std::uint64_t next_allocation_incarnation = 1;
  std::vector<Slot> spaces;
  std::vector<std::size_t> free_indices;
};

}  // namespace detail

namespace {

using Space = detail::TensorMemoryManagerState::Space;
using AllocationRecord = detail::TensorMemoryManagerState::AllocationRecord;

auto validate_request(const Space& space, std::size_t index,
                      std::uint32_t count)
    -> std::expected<void, TensorMemoryError> {
  if (!valid_column_count(count)) {
    return std::unexpected(error(TensorMemoryErrorCode::invalid_column_count,
                                 index, std::nullopt, count));
  }
  if (!space.allocation_permitted) {
    return std::unexpected(
        error(TensorMemoryErrorCode::allocation_permit_relinquished, index,
              std::nullopt, count));
  }
  if (space.previous_request && count > *space.previous_request) {
    return std::unexpected(
        error(TensorMemoryErrorCode::allocation_request_increase, index,
              std::nullopt, count));
  }
  return {};
}

auto range_is_free(const Space& space, std::uint16_t begin,
                   std::uint16_t count) noexcept -> bool {
  return std::none_of(space.occupied.begin() + begin,
                      space.occupied.begin() + begin + count,
                      [](bool occupied) { return occupied; });
}

auto lowest_free_range(const Space& space, std::uint16_t count)
    -> std::optional<std::uint16_t> {
  for (std::uint16_t begin = 0;
       begin <= kTensorMemoryColumnCount - count; begin += 32) {
    if (range_is_free(space, begin, count)) {
      return begin;
    }
  }
  return std::nullopt;
}

auto lowest_common_range(const Space& first, const Space& second,
                         std::uint16_t count)
    -> std::optional<std::uint16_t> {
  for (std::uint16_t begin = 0;
       begin <= kTensorMemoryColumnCount - count; begin += 32) {
    if (range_is_free(first, begin, count) &&
        range_is_free(second, begin, count)) {
      return begin;
    }
  }
  return std::nullopt;
}

void reserve(Space& space, std::uint16_t begin, std::uint16_t count,
             std::uint64_t incarnation, std::uint8_t group_size) {
  std::fill(space.occupied.begin() + begin,
            space.occupied.begin() + begin + count, true);
  space.allocations.emplace(
      begin, AllocationRecord{count, incarnation, group_size});
  space.previous_request = count;
  for (std::uint16_t lane = 0; lane < kTensorMemoryLaneCount; ++lane) {
    const auto first = space.cells.begin() + cell_index(lane, begin);
    std::fill(first, first + count, 0);
  }
}

auto validate_allocation(const Space& space, std::size_t index,
                         TensorMemoryAddress base, std::uint16_t column_count,
                         std::uint64_t expected_manager_token,
                         std::uint64_t manager_token,
                         std::uint64_t incarnation,
                         std::uint8_t allocation_group_size,
                         std::optional<std::uint8_t> group_size = std::nullopt)
    -> std::expected<void, TensorMemoryError> {
  const auto begin = base.column();
  const auto found = space.allocations.find(begin);
  if (manager_token != expected_manager_token || base.lane() != 0 ||
      found == space.allocations.end() ||
      found->second.column_count != column_count ||
      found->second.incarnation != incarnation ||
      (group_size && found->second.group_size != *group_size) ||
      found->second.group_size != allocation_group_size) {
    return std::unexpected(error(TensorMemoryErrorCode::allocation_mismatch,
                                 index, base, column_count));
  }
  return {};
}

void release(Space& space, TensorMemoryAddress base,
             std::uint16_t column_count) {
  const auto begin = base.column();
  std::fill(space.occupied.begin() + begin,
            space.occupied.begin() + begin + column_count, false);
  space.allocations.erase(begin);
}

auto validate_address(const Space& space, std::size_t index,
                      TensorMemoryAddress address)
    -> std::expected<void, TensorMemoryError> {
  if (address.lane() >= kTensorMemoryLaneCount ||
      address.column() >= kTensorMemoryColumnCount) {
    return std::unexpected(
        error(TensorMemoryErrorCode::invalid_address, index, address));
  }
  if (!space.occupied[address.column()]) {
    return std::unexpected(
        error(TensorMemoryErrorCode::unallocated_address, index, address));
  }
  return {};
}

}  // namespace

TensorMemoryManager::TensorMemoryManager()
    : state_(std::make_shared<detail::TensorMemoryManagerState>(
          next_manager_token())) {}

TensorMemoryManager::~TensorMemoryManager() = default;

auto TensorMemoryManager::create_space() -> TensorMemorySpaceHandle {
  std::size_t index;
  if (state_->free_indices.empty()) {
    index = state_->spaces.size();
    state_->spaces.emplace_back();
  } else {
    index = state_->free_indices.back();
    state_->free_indices.pop_back();
  }
  auto& slot = state_->spaces[index];
  slot.space.emplace();
  return {state_->token, index, slot.generation};
}

auto TensorMemoryManager::destroy(TensorMemorySpaceHandle handle)
    -> std::expected<void, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (!(*space)->allocations.empty()) {
    return std::unexpected(error(TensorMemoryErrorCode::outstanding_allocations,
                                 handle.index_));
  }
  auto& slot = state_->spaces[handle.index_];
  slot.space.reset();
  if (slot.generation != std::numeric_limits<std::uint64_t>::max()) {
    ++slot.generation;
    state_->free_indices.push_back(handle.index_);
  }
  return {};
}

auto TensorMemoryManager::allocation_permitted(
    TensorMemorySpaceHandle handle) const
    -> std::expected<bool, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  return (*space)->allocation_permitted;
}

auto TensorMemoryManager::allocate(TensorMemorySpaceHandle handle,
                                   std::uint32_t column_count)
    -> std::expected<TensorMemoryAllocation, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (auto valid = validate_request(**space, handle.index_, column_count);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto count = static_cast<std::uint16_t>(column_count);
  const auto begin = lowest_free_range(**space, count);
  if (!begin || state_->next_allocation_incarnation == 0) {
    return std::unexpected(error(TensorMemoryErrorCode::allocation_exhausted,
                                 handle.index_, std::nullopt, column_count));
  }
  const auto incarnation = state_->next_allocation_incarnation++;
  reserve(**space, *begin, count, incarnation, 1);
  return TensorMemoryAllocation{
      TensorMemoryAddress::from_indices(0, *begin), count, state_->token,
      incarnation, 1};
}

auto TensorMemoryManager::allocate(TensorMemorySpaceHandle first,
                                   TensorMemorySpaceHandle second,
                                   std::uint32_t column_count)
    -> std::expected<TensorMemoryAllocation, TensorMemoryError> {
  if (first == second) {
    return std::unexpected(error(TensorMemoryErrorCode::invalid_group));
  }
  auto first_space = state_->find(first);
  if (!first_space) {
    return std::unexpected(first_space.error());
  }
  auto second_space = state_->find(second);
  if (!second_space) {
    return std::unexpected(second_space.error());
  }
  if (auto valid = validate_request(**first_space, first.index_, column_count);
      !valid) {
    return std::unexpected(valid.error());
  }
  if (auto valid = validate_request(**second_space, second.index_, column_count);
      !valid) {
    return std::unexpected(valid.error());
  }
  const auto count = static_cast<std::uint16_t>(column_count);
  const auto begin = lowest_common_range(**first_space, **second_space, count);
  if (!begin || state_->next_allocation_incarnation == 0) {
    return std::unexpected(error(TensorMemoryErrorCode::allocation_exhausted,
                                 std::nullopt, std::nullopt, column_count));
  }
  const auto incarnation = state_->next_allocation_incarnation++;
  reserve(**first_space, *begin, count, incarnation, 2);
  reserve(**second_space, *begin, count, incarnation, 2);
  return TensorMemoryAllocation{
      TensorMemoryAddress::from_indices(0, *begin), count, state_->token,
      incarnation, 2};
}

auto TensorMemoryManager::deallocate(TensorMemorySpaceHandle handle,
                                     TensorMemoryAllocation allocation)
    -> std::expected<void, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (auto valid = validate_allocation(
          **space, handle.index_, allocation.base_, allocation.column_count_,
          state_->token, allocation.manager_token_, allocation.incarnation_,
          allocation.group_size_, 1);
      !valid) {
    return std::unexpected(valid.error());
  }
  release(**space, allocation.base_, allocation.column_count_);
  return {};
}

auto TensorMemoryManager::deallocate(TensorMemorySpaceHandle first,
                                     TensorMemorySpaceHandle second,
                                     TensorMemoryAllocation allocation)
    -> std::expected<void, TensorMemoryError> {
  if (first == second) {
    return std::unexpected(error(TensorMemoryErrorCode::invalid_group));
  }
  auto first_space = state_->find(first);
  if (!first_space) {
    return std::unexpected(first_space.error());
  }
  auto second_space = state_->find(second);
  if (!second_space) {
    return std::unexpected(second_space.error());
  }
  if (auto valid = validate_allocation(
          **first_space, first.index_, allocation.base_,
          allocation.column_count_, state_->token, allocation.manager_token_,
          allocation.incarnation_, allocation.group_size_, 2);
      !valid) {
    return std::unexpected(valid.error());
  }
  if (auto valid = validate_allocation(
          **second_space, second.index_, allocation.base_,
          allocation.column_count_, state_->token, allocation.manager_token_,
          allocation.incarnation_, allocation.group_size_, 2);
      !valid) {
    return std::unexpected(valid.error());
  }
  release(**first_space, allocation.base_, allocation.column_count_);
  release(**second_space, allocation.base_, allocation.column_count_);
  return {};
}

auto TensorMemoryManager::relinquish_allocation_permit(
    TensorMemorySpaceHandle handle)
    -> std::expected<void, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  (*space)->allocation_permitted = false;
  return {};
}

auto TensorMemoryManager::relinquish_allocation_permit(
    TensorMemorySpaceHandle first, TensorMemorySpaceHandle second)
    -> std::expected<void, TensorMemoryError> {
  if (first == second) {
    return std::unexpected(error(TensorMemoryErrorCode::invalid_group));
  }
  auto first_space = state_->find(first);
  if (!first_space) {
    return std::unexpected(first_space.error());
  }
  auto second_space = state_->find(second);
  if (!second_space) {
    return std::unexpected(second_space.error());
  }
  (*first_space)->allocation_permitted = false;
  (*second_space)->allocation_permitted = false;
  return {};
}

auto TensorMemoryManager::read(TensorMemorySpaceHandle handle,
                               TensorMemoryAddress address) const
    -> std::expected<std::uint32_t, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (auto valid = validate_address(**space, handle.index_, address); !valid) {
    return std::unexpected(valid.error());
  }
  return (*space)->cells[cell_index(address.lane(), address.column())];
}

auto TensorMemoryManager::write(TensorMemorySpaceHandle handle,
                                TensorMemoryAddress address,
                                std::uint32_t value)
    -> std::expected<void, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (auto valid = validate_address(**space, handle.index_, address); !valid) {
    return std::unexpected(valid.error());
  }
  (*space)->cells[cell_index(address.lane(), address.column())] = value;
  return {};
}

auto TensorMemoryManager::snapshot(TensorMemorySpaceHandle handle,
                                   TensorMemoryAllocation allocation) const
    -> std::expected<std::vector<std::uint32_t>, TensorMemoryError> {
  auto space = state_->find(handle);
  if (!space) {
    return std::unexpected(space.error());
  }
  if (auto valid = validate_allocation(
          **space, handle.index_, allocation.base_, allocation.column_count_,
          state_->token, allocation.manager_token_, allocation.incarnation_,
          allocation.group_size_);
      !valid) {
    return std::unexpected(valid.error());
  }

  std::vector<std::uint32_t> result;
  result.reserve(static_cast<std::size_t>(kTensorMemoryLaneCount) *
                 allocation.column_count_);
  const auto begin = allocation.base_.column();
  for (std::uint16_t lane = 0; lane < kTensorMemoryLaneCount; ++lane) {
    const auto first = (*space)->cells.begin() + cell_index(lane, begin);
    result.insert(result.end(), first, first + allocation.column_count_);
  }
  return result;
}

}  // namespace ptxsim::memory
