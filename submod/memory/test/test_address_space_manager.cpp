#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <utility>

#include <ptxsim/memory/address_space/address_space_manager.hpp>

namespace ptxsim::memory::test {
namespace {

template <typename View>
concept WritableAddressSpaceView =
    requires(View view, Address address, std::span<const std::byte> bytes) {
      view.write(address, bytes);
      view.initialize(address, bytes);
    };

using ConstViewResult =
    decltype(std::declval<const AddressSpaceManager&>().view(
        std::declval<GlobalSpaceHandle>()));
static_assert(
    std::same_as<typename ConstViewResult::value_type, ConstAddressSpaceView>);
static_assert(!WritableAddressSpaceView<ConstAddressSpaceView>);
static_assert(!std::same_as<GlobalSpaceHandle, ConstantSpaceHandle>);
static_assert(!std::same_as<LocalFrameHandle, EntryParameterHandle>);
static_assert(!std::same_as<FunctionParameterHandle, SharedSpaceHandle>);
}  // namespace

TEST(AddressSpaceManager, GlobalAllocatesAlignedStableRangesAndStoresBytes) {
  AddressSpaceManager manager;
  const auto global = manager.create_global({16});
  const auto first = manager.allocate(global, 4, 4);
  const auto zero = manager.allocate(global, 0, 8);
  const auto second = manager.allocate(global, 4, 8);
  ASSERT_TRUE(first);
  ASSERT_TRUE(zero);
  ASSERT_TRUE(second);
  EXPECT_EQ(*first, (AddressRange{Address{0}, 4}));
  EXPECT_EQ(*zero, (AddressRange{Address{8}, 0}));
  EXPECT_EQ(*second, (AddressRange{Address{8}, 4}));
  EXPECT_LT(first->begin, second->begin);

  auto view = *manager.view(global);
  const std::array payload{std::byte{1}, std::byte{2}, std::byte{3},
                           std::byte{4}};
  ASSERT_TRUE(view.initialize(first->begin, payload));
  std::array<std::byte, 4> read{};
  ASSERT_TRUE(view.read(first->begin, read, 4));
  EXPECT_EQ(read, payload);
  ASSERT_TRUE(view.write(
      first->begin,
      std::array{std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}}, 4));
  EXPECT_EQ(*view.snapshot(first->begin, first->size),
            (std::vector<std::byte>{std::byte{9}, std::byte{8}, std::byte{7},
                                    std::byte{6}}));
}

TEST(AddressSpaceManager, GlobalReportsExhaustionBoundsAndUninitializedReads) {
  AddressSpaceManager manager;
  const auto global = manager.create_global({8});
  ASSERT_TRUE(manager.allocate(global, 6, 1));
  const auto invalid_alignment = manager.allocate(global, 1, 3);
  ASSERT_FALSE(invalid_alignment);
  EXPECT_EQ(invalid_alignment.error().code,
            AddressSpaceErrorCode::allocation_failure);
  ASSERT_TRUE(invalid_alignment.error().memory_error);
  EXPECT_EQ(invalid_alignment.error().memory_error->code,
            MemoryErrorCode::InvalidAlignment);
  const auto exhausted = manager.allocate(global, 3, 1);
  ASSERT_FALSE(exhausted);
  EXPECT_EQ(exhausted.error().code, AddressSpaceErrorCode::allocation_failure);
  ASSERT_TRUE(exhausted.error().memory_error);
  EXPECT_EQ(exhausted.error().memory_error->code, MemoryErrorCode::OutOfBounds);

  auto view = *manager.view(global);
  std::array<std::byte, 1> byte{};
  const auto uninitialized = view.read(Address{0}, byte);
  ASSERT_FALSE(uninitialized);
  EXPECT_EQ(uninitialized.error().code, AddressSpaceErrorCode::storage_failure);
  EXPECT_EQ(uninitialized.error().memory_error->code,
            MemoryErrorCode::UninitializedRead);
  const auto out_of_bounds = view.read(Address{8}, byte);
  ASSERT_FALSE(out_of_bounds);
  EXPECT_EQ(out_of_bounds.error().memory_error->code,
            MemoryErrorCode::OutOfBounds);
}

TEST(AddressSpaceManager, ConstantAllowsInitializationButRejectsRuntimeWrites) {
  AddressSpaceManager manager;
  const auto constant = manager.create_constant({4});
  ASSERT_TRUE(manager.allocate(constant, 4, 1));
  auto view = *manager.view(constant);
  const std::array payload{std::byte{4}, std::byte{3}, std::byte{2},
                           std::byte{1}};
  ASSERT_TRUE(view.initialize(Address{0}, payload));
  std::array<std::byte, 4> read{};
  ASSERT_TRUE(view.read(Address{0}, read));
  EXPECT_EQ(read, payload);
  const auto write = view.write(Address{0}, payload);
  ASSERT_FALSE(write);
  EXPECT_EQ(write.error().memory_error->code,
            MemoryErrorCode::WriteToReadOnlyRegion);
}

TEST(AddressSpaceManager, LocalFramesAreIndependentAndSupportMultipleFrames) {
  AddressSpaceManager manager;
  const auto first = manager.create_local_frame({2});
  const auto second = manager.create_local_frame({2});
  const auto third = manager.create_local_frame({2});
  EXPECT_NE(first, second);
  EXPECT_NE(second, third);

  auto first_view = *manager.view(first);
  auto second_view = *manager.view(second);
  ASSERT_TRUE(first_view.write(Address{0}, std::array{std::byte{7}}));
  EXPECT_FALSE(*second_view.is_initialized(Address{0}, 1));
  EXPECT_FALSE(*manager.view(third)->is_initialized(Address{0}, 1));
}

TEST(AddressSpaceManager, DestroyedResourcesAreStaleAndReusedSlotsRemainSafe) {
  AddressSpaceManager manager;
  const auto old = manager.create_local_frame({1});
  auto old_view = *manager.view(old);
  ASSERT_TRUE(manager.destroy(old));
  EXPECT_EQ(manager.view(old).error().code,
            AddressSpaceErrorCode::stale_resource);
  EXPECT_EQ(old_view.size().error().code,
            AddressSpaceErrorCode::stale_resource);

  const auto replacement = manager.create_local_frame({1});
  EXPECT_NE(old, replacement);
  EXPECT_EQ(old_view.write(Address{0}, std::array{std::byte{1}}).error().code,
            AddressSpaceErrorCode::stale_resource);
  AddressSpaceManager other_manager;
  EXPECT_EQ(other_manager.view(replacement).error().code,
            AddressSpaceErrorCode::stale_resource);
}

TEST(AddressSpaceManager, EntryParametersAreReadOnlyAndIndependent) {
  AddressSpaceManager manager;
  const auto first = manager.create_entry_parameter({1});
  const auto second = manager.create_entry_parameter({1});
  auto first_view = *manager.view(first);
  auto second_view = *manager.view(second);
  ASSERT_TRUE(first_view.initialize(Address{0}, std::array{std::byte{5}}));
  EXPECT_FALSE(*second_view.is_initialized(Address{0}, 1));
  const auto write = first_view.write(Address{0}, std::array{std::byte{9}});
  ASSERT_FALSE(write);
  EXPECT_EQ(write.error().memory_error->code,
            MemoryErrorCode::WriteToReadOnlyRegion);
}

TEST(AddressSpaceManager,
     FunctionParametersAndSharedSpacesAreWritableAndIsolated) {
  AddressSpaceManager manager;
  const auto parameter = manager.create_function_parameter({1});
  const auto other_parameter = manager.create_function_parameter({1});
  const auto first_shared = manager.create_shared({1});
  const auto second_shared = manager.create_shared({1});
  ASSERT_TRUE(
      manager.view(parameter)->write(Address{0}, std::array{std::byte{1}}));
  EXPECT_FALSE(*manager.view(other_parameter)->is_initialized(Address{0}, 1));
  ASSERT_TRUE(
      manager.view(first_shared)->write(Address{0}, std::array{std::byte{2}}));
  EXPECT_FALSE(*manager.view(second_shared)->is_initialized(Address{0}, 1));
}

TEST(AddressSpaceManager, ZeroSizeAndManagerDestroyedViewsAreSafe) {
  AddressSpaceManager manager;
  const auto shared = manager.create_shared({0});
  auto view = *manager.view(shared);
  EXPECT_EQ(*view.size(), 0U);
  EXPECT_TRUE(*view.is_initialized(Address{0}, 0));
  EXPECT_TRUE(view.snapshot(Address{0}, 0));

  AddressSpaceView escaped = [] {
    auto owner = std::make_unique<AddressSpaceManager>();
    const auto local = owner->create_local_frame({1});
    auto result = *owner->view(local);
    owner.reset();
    return result;
  }();
  EXPECT_EQ(escaped.access().error().code,
            AddressSpaceErrorCode::stale_resource);
}

}  // namespace ptxsim::memory::test
