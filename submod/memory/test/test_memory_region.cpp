#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <ptxsim/memory/core/memory_region.hpp>

namespace ptxsim::memory::test {

TEST(MemoryRegion, ConstructionAndZeroSizeRegion) {
  MemoryRegion region(8);
  EXPECT_EQ(region.size(), 8U);
  EXPECT_FALSE(region.empty());
  EXPECT_EQ(region.access(), RegionAccess::ReadWrite);
  EXPECT_TRUE(region.contains(Address{0}, 8));

  MemoryRegion empty(0, RegionAccess::ReadOnly);
  EXPECT_EQ(empty.size(), 0U);
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.access(), RegionAccess::ReadOnly);
  EXPECT_TRUE(empty.contains(Address{0}, 0));
  EXPECT_FALSE(empty.contains(Address{1}, 0));
}

TEST(MemoryRegion, AlignedWriteReadAndSnapshotRoundTrip) {
  MemoryRegion region(8);
  const std::array payload{std::byte{1}, std::byte{2}, std::byte{3},
                           std::byte{4}};
  ASSERT_TRUE(region.write(Address{4}, payload, 4));
  EXPECT_TRUE(region.is_initialized(Address{4}, payload.size()));

  std::array<std::byte, 4> destination{};
  ASSERT_TRUE(region.read(Address{4}, destination, 4));
  EXPECT_EQ(destination, payload);
  EXPECT_EQ(*region.snapshot(Address{4}, payload.size()),
            (std::vector<std::byte>{payload.begin(), payload.end()}));
}

TEST(MemoryRegion, RejectsUninitializedAndPartiallyInitializedReads) {
  MemoryRegion region(4);
  std::array<std::byte, 4> destination{};
  const auto uninitialized = region.read(Address{0}, destination);
  ASSERT_FALSE(uninitialized);
  EXPECT_EQ(uninitialized.error().code, MemoryErrorCode::UninitializedRead);

  ASSERT_TRUE(region.initialize(
      Address{1}, std::array{std::byte{7}, std::byte{8}}));
  EXPECT_FALSE(region.is_initialized(Address{0}, 3));
  EXPECT_TRUE(region.is_initialized(Address{1}, 2));
  EXPECT_EQ(region.read(Address{0}, destination).error().code,
            MemoryErrorCode::UninitializedRead);

  ASSERT_TRUE(region.read(Address{0}, destination, 1,
                          ReadRequirement::IgnoreInitialization));
  EXPECT_EQ(destination,
            (std::array{std::byte{0}, std::byte{7}, std::byte{8},
                        std::byte{0}}));
}

TEST(MemoryRegion, RejectsOutOfBoundsAndOverflowingRanges) {
  MemoryRegion region(8);
  std::array<std::byte, 1> byte{};

  const auto start_out_of_bounds = region.read(Address{9}, byte);
  ASSERT_FALSE(start_out_of_bounds);
  EXPECT_EQ(start_out_of_bounds.error().code, MemoryErrorCode::OutOfBounds);

  const auto range_out_of_bounds =
      region.validate(Address{7}, 2, 1);
  ASSERT_FALSE(range_out_of_bounds);
  EXPECT_EQ(range_out_of_bounds.error().code, MemoryErrorCode::OutOfBounds);

  EXPECT_FALSE(region.contains(Address{1},
                               std::numeric_limits<std::size_t>::max()));
  EXPECT_FALSE(region.contains(
      Address{std::numeric_limits<std::uint64_t>::max()}, 1));
}

TEST(MemoryRegion, ValidatesPowerOfTwoAlignmentAndAddressAlignment) {
  MemoryRegion region(8);
  EXPECT_TRUE(region.validate(Address{4}, 4, 4));

  for (const std::size_t alignment : {0U, 3U}) {
    const auto invalid = region.validate(Address{0}, 1, alignment);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, MemoryErrorCode::InvalidAlignment);
    EXPECT_EQ(invalid.error().required_alignment, alignment);
  }

  const auto misaligned = region.validate(Address{2}, 1, 4);
  ASSERT_FALSE(misaligned);
  EXPECT_EQ(misaligned.error().code, MemoryErrorCode::Misaligned);
  EXPECT_EQ(misaligned.error().required_alignment, 4U);
}

TEST(MemoryRegion, ReadOnlyRegionAllowsLoaderInitializationOnly) {
  MemoryRegion region(4, RegionAccess::ReadOnly);
  const std::array payload{std::byte{1}, std::byte{2}, std::byte{3},
                           std::byte{4}};

  const auto write = region.write(Address{0}, payload);
  ASSERT_FALSE(write);
  EXPECT_EQ(write.error().code, MemoryErrorCode::WriteToReadOnlyRegion);

  ASSERT_TRUE(region.initialize(Address{0}, payload));
  std::array<std::byte, 4> destination{};
  ASSERT_TRUE(region.read(Address{0}, destination));
  EXPECT_EQ(destination, payload);
}

TEST(MemoryRegion, ResetFillAndZeroInitializationUpdateStateAndBytes) {
  MemoryRegion region(3);
  region.fill_initialized(std::byte{0x5a});
  EXPECT_TRUE(region.is_initialized(Address{0}, region.size()));
  EXPECT_EQ(*region.snapshot(Address{0}, region.size()),
            (std::vector(3, std::byte{0x5a})));

  region.reset_uninitialized(std::byte{0xa5});
  EXPECT_FALSE(region.is_initialized(Address{0}, region.size()));
  EXPECT_EQ(*region.snapshot(Address{0}, region.size()),
            (std::vector(3, std::byte{0xa5})));

  region.zero_initialize();
  EXPECT_TRUE(region.is_initialized(Address{0}, region.size()));
  EXPECT_EQ(*region.snapshot(Address{0}, region.size()),
            (std::vector(3, std::byte{0})));
}

TEST(MemoryRegion, ZeroLengthOperationsAreValidAtOnePastEnd) {
  MemoryRegion region(2);
  const std::span<const std::byte> source;
  const std::span<std::byte> destination;

  EXPECT_TRUE(region.initialize(Address{2}, source));
  EXPECT_TRUE(region.write(Address{2}, source));
  EXPECT_TRUE(region.read(Address{2}, destination));
  EXPECT_TRUE(region.is_initialized(Address{2}, 0));
  EXPECT_EQ(*region.snapshot(Address{2}, 0), std::vector<std::byte>{});

  EXPECT_FALSE(region.initialize(Address{3}, source));
  EXPECT_FALSE(region.write(Address{3}, source));
  EXPECT_FALSE(region.read(Address{3}, destination));
}

TEST(MemoryRegion, MoveConstructionAndAssignmentPreserveRegionState) {
  MemoryRegion source(2, RegionAccess::ReadOnly);
  const std::array payload{std::byte{4}, std::byte{2}};
  ASSERT_TRUE(source.initialize(Address{0}, payload));

  MemoryRegion constructed(std::move(source));
  EXPECT_EQ(constructed.size(), 2U);
  EXPECT_EQ(constructed.access(), RegionAccess::ReadOnly);
  EXPECT_EQ(*constructed.snapshot(Address{0}, 2),
            (std::vector<std::byte>{payload.begin(), payload.end()}));

  MemoryRegion assigned(1);
  assigned = std::move(constructed);
  EXPECT_EQ(assigned.size(), 2U);
  EXPECT_EQ(assigned.access(), RegionAccess::ReadOnly);
  EXPECT_EQ(*assigned.snapshot(Address{0}, 2),
            (std::vector<std::byte>{payload.begin(), payload.end()}));
}

}  // namespace ptxsim::memory::test
