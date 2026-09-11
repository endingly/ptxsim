#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>

#include <ptxsim/memory/memory.hpp>

namespace ptxsim::memory::test {

TEST(StorageGrowth, PreservesBytesInitializationAndReadOnlyPermission) {
  MemoryRegion region{8, RegionAccess::ReadOnly};
  const std::array bytes{std::byte{0x12}, std::byte{0x34}};
  ASSERT_TRUE(region.initialize(Address{2}, bytes));
  ASSERT_TRUE(region.grow(16));
  EXPECT_EQ(region.size(), 16U);
  EXPECT_EQ(region.access(), RegionAccess::ReadOnly);
  EXPECT_TRUE(region.is_initialized(Address{2}, 2));
  EXPECT_FALSE(region.is_initialized(Address{0}, 1));
  EXPECT_FALSE(region.is_initialized(Address{8}, 8));
  std::array<std::byte, 2> observed{};
  ASSERT_TRUE(region.read(Address{2}, observed));
  EXPECT_EQ(observed, bytes);
  const auto denied = region.write(Address{2}, bytes);
  ASSERT_FALSE(denied);
  EXPECT_EQ(denied.error().code, MemoryErrorCode::WriteToReadOnlyRegion);
  const auto shrinking = region.grow(4);
  ASSERT_FALSE(shrinking);
  EXPECT_EQ(region.size(), 16U);
  ASSERT_TRUE(region.read(Address{2}, observed));
  EXPECT_EQ(observed, bytes);
}

TEST(StorageGrowth, RetainsExistingViewAndReservesAppendAgainstAllocatorReuse) {
  AddressSpaceManager manager;
  const auto global = manager.create_global({.capacity = 8});
  auto view = manager.view(global);
  ASSERT_TRUE(view);
  const std::array bytes{std::byte{0x56}, std::byte{0x78}};
  ASSERT_TRUE(view->initialize(Address{6}, bytes));
  ASSERT_TRUE(manager.grow(global, 24));
  ASSERT_TRUE(view->size());
  EXPECT_EQ(*view->size(), 24U);
  std::array<std::byte, 2> observed{};
  ASSERT_TRUE(view->read(Address{6}, observed));
  EXPECT_EQ(observed, bytes);
  const auto initialization = view->is_initialized(Address{8}, 16);
  ASSERT_TRUE(initialization);
  EXPECT_FALSE(*initialization);
  // Appended declaration storage and the entire caller prefix are reserved.
  EXPECT_FALSE(manager.allocate(global, 1));
  ASSERT_TRUE(manager.destroy(global));
  EXPECT_FALSE(manager.grow(global, 32));
  EXPECT_FALSE(view->size());
}

TEST(StorageGrowth, GrowsConstantStorageWithoutGrantingRuntimeWriteAccess) {
  AddressSpaceManager manager;
  const auto constant = manager.create_constant({.capacity = 4});
  auto view = manager.view(constant);
  ASSERT_TRUE(view);
  ASSERT_TRUE(manager.grow(constant, 8));
  const std::array bytes{std::byte{0xab}, std::byte{0xcd}};
  ASSERT_TRUE(view->initialize(Address{4}, bytes));
  EXPECT_FALSE(view->write(Address{4}, bytes));
  std::array<std::byte, 2> observed{};
  ASSERT_TRUE(view->read(Address{4}, observed));
  EXPECT_EQ(observed, bytes);
  EXPECT_FALSE(manager.allocate(constant, 1));
}

TEST(StorageGrowth, RejectsUnrepresentableExtentWithoutChangingExistingRegion) {
  MemoryRegion region{4};
  const std::array bytes{std::byte{0xef}};
  ASSERT_TRUE(region.initialize(Address{0}, bytes));
  const auto result = region.grow(std::numeric_limits<std::size_t>::max());
  ASSERT_FALSE(result);
  EXPECT_EQ(region.size(), 4U);
  std::array<std::byte, 1> observed{};
  ASSERT_TRUE(region.read(Address{0}, observed));
  EXPECT_EQ(observed, bytes);
}

}  // namespace ptxsim::memory::test
