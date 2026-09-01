#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include <ptxsim/memory/address_space/address_space_manager.hpp>

namespace ptxsim::memory::test {
namespace {

constexpr std::uint32_t kMaxCount = (std::uint32_t{1} << 20) - 1;

auto shared_barriers(AddressSpaceManager& manager, std::size_t size = 64)
    -> std::pair<SharedSpaceHandle, MBarrierView> {
  const auto shared = manager.create_shared({size});
  return {shared, *manager.mbarriers(shared)};
}

}  // namespace

TEST(MBarrierState, InitReportsInvalidAndDuplicateUses) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager);
  static_cast<void>(shared);

  const auto invalid_count = barriers.init(Address{0}, 0);
  ASSERT_FALSE(invalid_count);
  EXPECT_EQ(invalid_count.error().code,
            MBarrierErrorCode::invalid_arrival_count);
  const auto excessive_count = barriers.init(Address{8}, kMaxCount + 1);
  ASSERT_FALSE(excessive_count);
  EXPECT_EQ(excessive_count.error().code,
            MBarrierErrorCode::invalid_arrival_count);
  const auto invalid_use = barriers.arrive(Address{0});
  ASSERT_FALSE(invalid_use);
  EXPECT_EQ(invalid_use.error().code, MBarrierErrorCode::invalid_barrier);

  ASSERT_TRUE(barriers.init(Address{0}, 2));
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{2, 2, 0, 0}));
  const auto duplicate = barriers.init(Address{0}, 1);
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error().code,
            MBarrierErrorCode::duplicate_initialization);
  ASSERT_TRUE(barriers.invalidate(Address{0}));
  EXPECT_EQ(barriers.snapshot(Address{0}).error().code,
            MBarrierErrorCode::invalid_barrier);
}

TEST(MBarrierState, ReinitializationRejectsOldTokens) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager);
  static_cast<void>(shared);
  ASSERT_TRUE(barriers.init(Address{0}, 1));
  const auto old_token = *barriers.arrive(Address{0});
  ASSERT_TRUE(barriers.invalidate(Address{0}));
  ASSERT_TRUE(barriers.init(Address{0}, 1));

  const auto wait = barriers.test_wait(Address{0}, old_token);
  ASSERT_FALSE(wait);
  EXPECT_EQ(wait.error().code, MBarrierErrorCode::invalid_token);
}

TEST(MBarrierState, TokensCannotCrossSharedResourcesOrManagers) {
  AddressSpaceManager manager;
  const auto first_shared = manager.create_shared({8});
  const auto second_shared = manager.create_shared({8});
  auto first = *manager.mbarriers(first_shared);
  auto second = *manager.mbarriers(second_shared);
  ASSERT_TRUE(first.init(Address{0}, 1));
  ASSERT_TRUE(second.init(Address{0}, 1));
  const auto token = *first.arrive(Address{0});
  const auto other_resource = second.test_wait(Address{0}, token);
  ASSERT_FALSE(other_resource);
  EXPECT_EQ(other_resource.error().code, MBarrierErrorCode::invalid_token);

  AddressSpaceManager other_manager;
  const auto other_shared = other_manager.create_shared({8});
  auto other = *other_manager.mbarriers(other_shared);
  ASSERT_TRUE(other.init(Address{0}, 1));
  const auto other_manager_result = other.test_wait(Address{0}, token);
  ASSERT_FALSE(other_manager_result);
  EXPECT_EQ(other_manager_result.error().code,
            MBarrierErrorCode::invalid_token);
}

TEST(MBarrierState, ArrivalsAdvancePhaseAcrossMultipleRounds) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager);
  static_cast<void>(shared);
  ASSERT_TRUE(barriers.init(Address{0}, 2));

  const auto first = *barriers.arrive(Address{0});
  EXPECT_FALSE(*barriers.test_wait(Address{0}, first));
  const auto second = *barriers.arrive(Address{0});
  EXPECT_TRUE(*barriers.test_wait(Address{0}, first));
  EXPECT_TRUE(*barriers.test_wait(Address{0}, second));
  ASSERT_TRUE(barriers.arrive(Address{0}));
  ASSERT_TRUE(barriers.arrive(Address{0}));
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{2, 2, 0, 2}));
}

TEST(MBarrierState, ArrivalUnderflowWaitsForPendingTransactions) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager);
  static_cast<void>(shared);
  ASSERT_TRUE(barriers.init(Address{0}, 2));
  ASSERT_TRUE(barriers.expect_tx(Address{0}, 1));
  ASSERT_TRUE(barriers.arrive(Address{0}));
  ASSERT_TRUE(barriers.arrive(Address{0}));
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{2, 0, 1, 0}));
  const auto underflow = barriers.arrive(Address{0});
  ASSERT_FALSE(underflow);
  EXPECT_EQ(underflow.error().code, MBarrierErrorCode::arrival_underflow);
  ASSERT_TRUE(barriers.complete_tx(Address{0}, 1));
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{2, 2, 0, 1}));
}

TEST(MBarrierState, ArriveCountSupportsPartialExactAndOverArrival) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager);
  static_cast<void>(shared);
  ASSERT_TRUE(barriers.init(Address{0}, 3));
  const auto partial = *barriers.arrive(Address{0}, 2);
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{3, 1, 0, 0}));
  EXPECT_FALSE(*barriers.test_wait(Address{0}, partial));
  ASSERT_TRUE(barriers.arrive(Address{0}, 1));
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{3, 3, 0, 1}));

  ASSERT_TRUE(barriers.init(Address{8}, 2));
  const auto over_arrival = barriers.arrive(Address{8}, 3);
  ASSERT_FALSE(over_arrival);
  EXPECT_EQ(over_arrival.error().code, MBarrierErrorCode::arrival_underflow);
  EXPECT_EQ(*barriers.snapshot(Address{8}), (MBarrierSnapshot{2, 2, 0, 0}));
  const auto zero_arrival = barriers.arrive(Address{8}, 0);
  ASSERT_FALSE(zero_arrival);
  EXPECT_EQ(zero_arrival.error().code,
            MBarrierErrorCode::invalid_arrival_count);
}

TEST(MBarrierState, TransactionAccountingSupportsSignedOrderAndBoundaries) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager);
  static_cast<void>(shared);
  ASSERT_TRUE(barriers.init(Address{0}, 1));
  ASSERT_TRUE(barriers.complete_tx(Address{0}, 5));
  EXPECT_EQ(barriers.snapshot(Address{0})->transaction_count, -5);
  ASSERT_TRUE(barriers.arrive(Address{0}));
  ASSERT_TRUE(barriers.expect_tx(Address{0}, 5));
  EXPECT_EQ(*barriers.snapshot(Address{0}), (MBarrierSnapshot{1, 1, 0, 1}));

  ASSERT_TRUE(barriers.init(Address{8}, 1));
  ASSERT_TRUE(barriers.expect_tx(Address{8}, kMaxCount));
  EXPECT_EQ(barriers.snapshot(Address{8})->transaction_count, kMaxCount);
  const auto positive_overflow = barriers.expect_tx(Address{8}, 1);
  ASSERT_FALSE(positive_overflow);
  EXPECT_EQ(positive_overflow.error().code,
            MBarrierErrorCode::transaction_overflow);
  EXPECT_EQ(barriers.snapshot(Address{8})->transaction_count, kMaxCount);

  ASSERT_TRUE(barriers.init(Address{16}, 1));
  ASSERT_TRUE(barriers.complete_tx(Address{16}, kMaxCount));
  EXPECT_EQ(barriers.snapshot(Address{16})->transaction_count,
            -static_cast<std::int64_t>(kMaxCount));
  const auto negative_overflow = barriers.complete_tx(Address{16}, 1);
  ASSERT_FALSE(negative_overflow);
  EXPECT_EQ(negative_overflow.error().code,
            MBarrierErrorCode::transaction_overflow);
  EXPECT_EQ(barriers.snapshot(Address{16})->transaction_count,
            -static_cast<std::int64_t>(kMaxCount));

  ASSERT_TRUE(barriers.init(Address{24}, 1));
  ASSERT_TRUE(barriers.complete_tx(Address{24}, kMaxCount));
  ASSERT_TRUE(barriers.expect_tx(Address{24}, 2 * kMaxCount));
  EXPECT_EQ(barriers.snapshot(Address{24})->transaction_count, kMaxCount);

  ASSERT_TRUE(barriers.init(Address{32}, 1));
  ASSERT_TRUE(barriers.expect_tx(Address{32}, kMaxCount));
  ASSERT_TRUE(barriers.complete_tx(Address{32}, 2 * kMaxCount));
  EXPECT_EQ(barriers.snapshot(Address{32})->transaction_count,
            -static_cast<std::int64_t>(kMaxCount));
}

TEST(MBarrierState, SidecarsAreIsolatedByAddressAndSharedResource) {
  AddressSpaceManager manager;
  const auto first_shared = manager.create_shared({32});
  const auto second_shared = manager.create_shared({32});
  auto first = *manager.mbarriers(first_shared);
  auto second = *manager.mbarriers(second_shared);
  ASSERT_TRUE(first.init(Address{0}, 1));
  ASSERT_TRUE(first.init(Address{8}, 2));
  ASSERT_TRUE(second.init(Address{0}, 3));
  ASSERT_TRUE(first.arrive(Address{0}));

  EXPECT_EQ(*first.snapshot(Address{0}), (MBarrierSnapshot{1, 1, 0, 1}));
  EXPECT_EQ(*first.snapshot(Address{8}), (MBarrierSnapshot{2, 2, 0, 0}));
  EXPECT_EQ(*second.snapshot(Address{0}), (MBarrierSnapshot{3, 3, 0, 0}));
}

TEST(MBarrierState, ValidatesAddressAndDestroyedSharedResources) {
  AddressSpaceManager manager;
  auto [shared, barriers] = shared_barriers(manager, 16);
  const auto misaligned = barriers.init(Address{1}, 1);
  ASSERT_FALSE(misaligned);
  EXPECT_EQ(misaligned.error().code, MBarrierErrorCode::invalid_address);
  ASSERT_TRUE(misaligned.error().memory_error);
  EXPECT_EQ(misaligned.error().memory_error->code, MemoryErrorCode::Misaligned);
  const auto out_of_bounds = barriers.init(Address{16}, 1);
  ASSERT_FALSE(out_of_bounds);
  EXPECT_EQ(out_of_bounds.error().code, MBarrierErrorCode::invalid_address);
  ASSERT_TRUE(out_of_bounds.error().memory_error);
  EXPECT_EQ(out_of_bounds.error().memory_error->code,
            MemoryErrorCode::OutOfBounds);

  ASSERT_TRUE(barriers.init(Address{8}, 1));
  ASSERT_TRUE(manager.destroy(shared));
  EXPECT_EQ(barriers.snapshot(Address{8}).error().code,
            MBarrierErrorCode::stale_shared_space);
}

}  // namespace ptxsim::memory::test
