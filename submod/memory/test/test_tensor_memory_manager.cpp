#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <ptxsim/memory/tmem/tensor_memory_manager.hpp>

namespace ptxsim::memory::test {

TEST(TensorMemoryAddress, EncodesLaneAndColumnInPtxLayout) {
  static_assert(sizeof(TensorMemoryAddress) == sizeof(std::uint32_t));
  constexpr auto address =
      TensorMemoryAddress::from_indices(127, 511);
  static_assert(address.value() == 0x007f'01ffU);
  static_assert(address.lane() == 127);
  static_assert(address.column() == 511);
}

TEST(TensorMemoryManager, StoresCellsInIndependentSpacesAndValidatesAddress) {
  TensorMemoryManager manager;
  const auto first = manager.create_space();
  const auto second = manager.create_space();
  const auto first_allocation = manager.allocate(first, 32);
  const auto second_allocation = manager.allocate(second, 32);
  ASSERT_TRUE(first_allocation);
  ASSERT_TRUE(second_allocation);

  constexpr auto address = TensorMemoryAddress::from_indices(7, 3);
  ASSERT_TRUE(manager.write(first, address, 11));
  ASSERT_TRUE(manager.write(second, address, 22));
  EXPECT_EQ(*manager.read(first, address), 11U);
  EXPECT_EQ(*manager.read(second, address), 22U);

  const auto unallocated =
      manager.read(first, TensorMemoryAddress::from_indices(0, 32));
  ASSERT_FALSE(unallocated);
  EXPECT_EQ(unallocated.error().code,
            TensorMemoryErrorCode::unallocated_address);

  for (const auto invalid : {
           TensorMemoryAddress::from_indices(kTensorMemoryLaneCount, 0),
           TensorMemoryAddress::from_indices(0, kTensorMemoryColumnCount)}) {
    const auto result = manager.read(first, invalid);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, TensorMemoryErrorCode::invalid_address);
  }
}

TEST(TensorMemoryManager,
     AllocatesLowestRangeAndValidatesDeallocationAndRequestOrder) {
  TensorMemoryManager manager;
  const auto space = manager.create_space();
  const auto other = manager.create_space();

  const auto first = manager.allocate(space, 64);
  const auto second = manager.allocate(space, 32);
  const auto foreign = manager.allocate(other, 32);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(foreign);
  EXPECT_EQ(first->base(), TensorMemoryAddress::from_indices(0, 0));
  EXPECT_EQ(second->base(), TensorMemoryAddress::from_indices(0, 64));

  const auto mismatch = manager.deallocate(space, *foreign);
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().code, TensorMemoryErrorCode::allocation_mismatch);

  ASSERT_TRUE(manager.deallocate(space, *first));
  const auto replacement = manager.allocate(space, 32);
  ASSERT_TRUE(replacement);
  EXPECT_EQ(replacement->base(), TensorMemoryAddress::from_indices(0, 0));

  const auto increase = manager.allocate(space, 64);
  ASSERT_FALSE(increase);
  EXPECT_EQ(increase.error().code,
            TensorMemoryErrorCode::allocation_request_increase);

  ASSERT_TRUE(manager.write(space, replacement->base(), 99));
  ASSERT_TRUE(manager.deallocate(space, *replacement));
  const auto reused = manager.allocate(space, 32);
  ASSERT_TRUE(reused);
  EXPECT_EQ(*manager.read(space, reused->base()), 0U);
}

TEST(TensorMemoryManager, RejectsInvalidColumnCountsAndExhaustion) {
  TensorMemoryManager manager;
  const auto space = manager.create_space();

  for (const std::uint32_t count : {0U, 31U, 96U, 513U}) {
    const auto result = manager.allocate(space, count);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              TensorMemoryErrorCode::invalid_column_count);
    EXPECT_EQ(result.error().requested_columns, count);
  }

  const auto whole_space = manager.allocate(space, 512);
  ASSERT_TRUE(whole_space);
  const auto exhausted = manager.allocate(space, 32);
  ASSERT_FALSE(exhausted);
  EXPECT_EQ(exhausted.error().code,
            TensorMemoryErrorCode::allocation_exhausted);
}

TEST(TensorMemoryManager,
     CoordinatesTwoSpacesAtLowestCommonRangeAndDeallocatesCollectively) {
  TensorMemoryManager manager;
  const auto first = manager.create_space();
  const auto second = manager.create_space();
  const auto first_prefix = manager.allocate(first, 32);
  const auto second_prefix = manager.allocate(second, 64);
  ASSERT_TRUE(first_prefix);
  ASSERT_TRUE(second_prefix);

  const auto group = manager.allocate(first, second, 32);
  ASSERT_TRUE(group);
  EXPECT_EQ(group->base(), TensorMemoryAddress::from_indices(0, 64));
  ASSERT_TRUE(manager.write(first, group->base(), 1));
  ASSERT_TRUE(manager.write(second, group->base(), 2));
  EXPECT_EQ(*manager.read(first, group->base()), 1U);
  EXPECT_EQ(*manager.read(second, group->base()), 2U);

  const auto single_deallocation = manager.deallocate(first, *group);
  ASSERT_FALSE(single_deallocation);
  EXPECT_EQ(single_deallocation.error().code,
            TensorMemoryErrorCode::allocation_mismatch);
  EXPECT_EQ(*manager.read(first, group->base()), 1U);

  const auto unrelated = manager.create_space();
  const auto unrelated_allocation = manager.allocate(unrelated, 32);
  ASSERT_TRUE(unrelated_allocation);
  const auto wrong_pair = manager.deallocate(first, unrelated, *group);
  ASSERT_FALSE(wrong_pair);
  EXPECT_EQ(wrong_pair.error().code,
            TensorMemoryErrorCode::allocation_mismatch);
  EXPECT_EQ(*manager.read(first, group->base()), 1U);

  ASSERT_TRUE(manager.deallocate(first, second, *group));
  EXPECT_EQ(manager.read(first, group->base()).error().code,
            TensorMemoryErrorCode::unallocated_address);
  EXPECT_EQ(manager.read(second, group->base()).error().code,
            TensorMemoryErrorCode::unallocated_address);

  const auto repeated_space = manager.allocate(first, first, 32);
  ASSERT_FALSE(repeated_space);
  EXPECT_EQ(repeated_space.error().code, TensorMemoryErrorCode::invalid_group);
}

TEST(TensorMemoryManager, GroupAllocationFailureDoesNotPartiallyMutateState) {
  TensorMemoryManager manager;
  const auto first = manager.create_space();
  const auto second = manager.create_space();
  ASSERT_TRUE(manager.relinquish_allocation_permit(second));

  const auto group = manager.allocate(first, second, 32);
  ASSERT_FALSE(group);
  EXPECT_EQ(group.error().code,
            TensorMemoryErrorCode::allocation_permit_relinquished);

  const auto first_allocation = manager.allocate(first, 64);
  ASSERT_TRUE(first_allocation);
  EXPECT_EQ(first_allocation->base(),
            TensorMemoryAddress::from_indices(0, 0));
}

TEST(TensorMemoryManager, GroupExhaustionDoesNotReserveTheFreeSpace) {
  TensorMemoryManager manager;
  const auto full = manager.create_space();
  const auto free = manager.create_space();
  ASSERT_TRUE(manager.allocate(full, 512));

  const auto group = manager.allocate(full, free, 32);
  ASSERT_FALSE(group);
  EXPECT_EQ(group.error().code, TensorMemoryErrorCode::allocation_exhausted);

  const auto free_allocation = manager.allocate(free, 32);
  ASSERT_TRUE(free_allocation);
  EXPECT_EQ(free_allocation->base(),
            TensorMemoryAddress::from_indices(0, 0));
}

TEST(TensorMemoryManager, RelinquishesSingleAndTwoSpaceAllocationPermits) {
  TensorMemoryManager manager;
  const auto first = manager.create_space();
  const auto second = manager.create_space();
  ASSERT_TRUE(manager.relinquish_allocation_permit(first, second));
  EXPECT_FALSE(*manager.allocation_permitted(first));
  EXPECT_FALSE(*manager.allocation_permitted(second));
  EXPECT_EQ(manager.allocate(first, 32).error().code,
            TensorMemoryErrorCode::allocation_permit_relinquished);

  TensorMemoryManager other_manager;
  const auto local = manager.create_space();
  const auto foreign = other_manager.create_space();
  const auto failed_group =
      manager.relinquish_allocation_permit(local, foreign);
  ASSERT_FALSE(failed_group);
  EXPECT_EQ(failed_group.error().code, TensorMemoryErrorCode::stale_space);
  EXPECT_TRUE(*manager.allocation_permitted(local));
}

TEST(TensorMemoryManager, RejectsDestroyedReusedAndCrossManagerHandles) {
  TensorMemoryManager manager;
  const auto old = manager.create_space();
  const auto allocation = manager.allocate(old, 32);
  ASSERT_TRUE(allocation);
  const auto active_destroy = manager.destroy(old);
  ASSERT_FALSE(active_destroy);
  EXPECT_EQ(active_destroy.error().code,
            TensorMemoryErrorCode::outstanding_allocations);
  ASSERT_TRUE(manager.deallocate(old, *allocation));
  ASSERT_TRUE(manager.destroy(old));
  EXPECT_EQ(manager.allocate(old, 32).error().code,
            TensorMemoryErrorCode::stale_space);
  EXPECT_EQ(manager.destroy(old).error().code,
            TensorMemoryErrorCode::stale_space);

  const auto replacement = manager.create_space();
  EXPECT_NE(old, replacement);
  TensorMemoryManager other_manager;
  EXPECT_EQ(other_manager.allocation_permitted(replacement).error().code,
            TensorMemoryErrorCode::stale_space);
}

TEST(TensorMemoryManager, AllocationCredentialsCannotCrossManagers) {
  TensorMemoryManager first_manager;
  TensorMemoryManager second_manager;
  const auto first_space = first_manager.create_space();
  const auto second_space = second_manager.create_space();
  const auto first_allocation = first_manager.allocate(first_space, 32);
  const auto second_allocation = second_manager.allocate(second_space, 32);
  ASSERT_TRUE(first_allocation);
  ASSERT_TRUE(second_allocation);

  const auto deallocation =
      second_manager.deallocate(second_space, *first_allocation);
  ASSERT_FALSE(deallocation);
  EXPECT_EQ(deallocation.error().code,
            TensorMemoryErrorCode::allocation_mismatch);
  const auto snapshot =
      second_manager.snapshot(second_space, *first_allocation);
  ASSERT_FALSE(snapshot);
  EXPECT_EQ(snapshot.error().code, TensorMemoryErrorCode::allocation_mismatch);

  ASSERT_TRUE(second_manager.write(second_space, second_allocation->base(), 7));
  EXPECT_EQ(*second_manager.read(second_space, second_allocation->base()), 7U);
  ASSERT_TRUE(second_manager.deallocate(second_space, *second_allocation));
}

TEST(TensorMemoryManager, SnapshotIsLaneMajorAndBoundToExactAllocation) {
  TensorMemoryManager manager;
  const auto space = manager.create_space();
  const auto allocation = manager.allocate(space, 32);
  ASSERT_TRUE(allocation);
  ASSERT_TRUE(manager.write(
      space, TensorMemoryAddress::from_indices(0, 0), 10));
  ASSERT_TRUE(manager.write(
      space, TensorMemoryAddress::from_indices(0, 1), 11));
  ASSERT_TRUE(manager.write(
      space, TensorMemoryAddress::from_indices(1, 0), 20));

  const auto snapshot = manager.snapshot(space, *allocation);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->size(),
            static_cast<std::size_t>(kTensorMemoryLaneCount) * 32);
  EXPECT_EQ((*snapshot)[0], 10U);
  EXPECT_EQ((*snapshot)[1], 11U);
  EXPECT_EQ((*snapshot)[32], 20U);

  const auto other = manager.create_space();
  const auto other_allocation = manager.allocate(other, 32);
  ASSERT_TRUE(other_allocation);
  const auto mismatch = manager.snapshot(space, *other_allocation);
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().code, TensorMemoryErrorCode::allocation_mismatch);
}

}  // namespace ptxsim::memory::test
