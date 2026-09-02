#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

#include <ptxsim/memory/address_space/address_space_manager.hpp>
#include <ptxsim/memory/async/async_memory_engine.hpp>

namespace ptxsim::memory::test {
namespace {

template <typename T>
concept ExposesThread = requires(T value) { value.thread(); };

template <typename T>
concept ExposesWarp = requires(T value) { value.warp(); };

template <typename T>
concept ExposesCta = requires(T value) { value.cta(); };

static_assert(!ExposesThread<AsyncMemoryEngine>);
static_assert(!ExposesWarp<AsyncMemoryEngine>);
static_assert(!ExposesCta<AsyncMemoryEngine>);
static_assert(std::constructible_from<AsyncMemoryOp, CopyOp>);
static_assert(
    std::same_as<decltype(CopyOp::source), ConstAddressSpaceView>);
static_assert(
    std::same_as<decltype(CopyOp::destination), AddressSpaceView>);

auto copy(AddressSpaceView source, Address source_offset,
          AddressSpaceView destination, Address destination_offset,
          std::size_t size) -> AsyncMemoryOp {
  return CopyOp{static_cast<ConstAddressSpaceView>(source), source_offset,
                std::move(destination), destination_offset, size};
}

void expect_storage_failure(const AsyncMemoryEngine& engine,
                            AsyncMemoryHandle handle,
                            AsyncMemoryErrorCode async_code,
                            AddressSpaceErrorCode address_code) {
  EXPECT_EQ(engine.status(handle), AsyncMemoryStatus::failed);
  const auto result = engine.result(handle);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, async_code);
  ASSERT_TRUE(result.error().address_space_error);
  EXPECT_EQ(result.error().address_space_error->code, address_code);
}

}  // namespace

TEST(AsyncMemoryEngine, HandlesAreUniqueWithinAndAcrossEngines) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_local_frame({1});
  auto source_view = *manager.view(source);
  auto destination_view = *manager.view(destination);

  AsyncMemoryEngine first_engine;
  AsyncMemoryEngine second_engine;
  const auto first = first_engine.issue(
      copy(source_view, Address{0}, destination_view, Address{0}, 1));
  const auto second = first_engine.issue(
      copy(source_view, Address{0}, destination_view, Address{0}, 1));
  const auto other = second_engine.issue(
      copy(source_view, Address{0}, destination_view, Address{0}, 1));

  EXPECT_NE(first, second);
  EXPECT_NE(first, other);
  EXPECT_EQ(first_engine.status(first), AsyncMemoryStatus::pending);
  EXPECT_EQ(first_engine.status(other).error().code,
            AsyncMemoryErrorCode::stale_handle);
}

TEST(AsyncMemoryEngine, ProgressCompletesOneOverlapSafeCopy) {
  AddressSpaceManager manager;
  const auto space = manager.create_local_frame({4});
  auto view = *manager.view(space);
  ASSERT_TRUE(view.initialize(
      Address{0}, std::array{std::byte{1}, std::byte{2}, std::byte{3},
                             std::byte{4}}));

  AsyncMemoryEngine engine;
  const auto handle =
      engine.issue(copy(view, Address{0}, view, Address{1}, 3));
  EXPECT_EQ(engine.status(handle), AsyncMemoryStatus::pending);
  EXPECT_EQ(engine.result(handle).error().code,
            AsyncMemoryErrorCode::pending_operation);

  EXPECT_EQ(engine.progress(), handle);
  EXPECT_EQ(engine.status(handle), AsyncMemoryStatus::completed);
  EXPECT_TRUE(engine.result(handle));
  EXPECT_EQ(*view.snapshot(Address{0}, 4),
            (std::vector<std::byte>{std::byte{1}, std::byte{1}, std::byte{2},
                                    std::byte{3}}));
}

TEST(AsyncMemoryEngine, ProgressIsFifoAndProcessesExactlyOneOperation) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({2});
  const auto destination = manager.create_local_frame({1});
  auto source_view = *manager.view(source);
  auto destination_view = *manager.view(destination);
  ASSERT_TRUE(source_view.initialize(
      Address{0}, std::array{std::byte{7}, std::byte{9}}));

  AsyncMemoryEngine engine;
  const auto first = engine.issue(
      copy(source_view, Address{0}, destination_view, Address{0}, 1));
  const auto second = engine.issue(
      copy(source_view, Address{1}, destination_view, Address{0}, 1));

  EXPECT_EQ(engine.progress(), first);
  EXPECT_EQ(engine.status(first), AsyncMemoryStatus::completed);
  EXPECT_EQ(engine.status(second), AsyncMemoryStatus::pending);
  EXPECT_EQ(*destination_view.snapshot(Address{0}, 1),
            (std::vector<std::byte>{std::byte{7}}));

  EXPECT_EQ(engine.progress(), second);
  EXPECT_EQ(engine.status(second), AsyncMemoryStatus::completed);
  EXPECT_EQ(*destination_view.snapshot(Address{0}, 1),
            (std::vector<std::byte>{std::byte{9}}));
  EXPECT_EQ(engine.progress(), std::nullopt);
}

TEST(AsyncMemoryEngine, FailedOperationDoesNotBlockItsSuccessor) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({2});
  const auto destination = manager.create_local_frame({1});
  auto source_view = *manager.view(source);
  auto destination_view = *manager.view(destination);
  ASSERT_TRUE(source_view.initialize(Address{1},
                                     std::array{std::byte{5}}));

  AsyncMemoryEngine engine;
  const auto failed = engine.issue(
      copy(source_view, Address{0}, destination_view, Address{0}, 1));
  const auto completed = engine.issue(
      copy(source_view, Address{1}, destination_view, Address{0}, 1));

  EXPECT_EQ(engine.progress(), failed);
  expect_storage_failure(engine, failed, AsyncMemoryErrorCode::source_failure,
                         AddressSpaceErrorCode::storage_failure);
  EXPECT_EQ(engine.status(completed), AsyncMemoryStatus::pending);

  EXPECT_EQ(engine.progress(), completed);
  EXPECT_TRUE(engine.result(completed));
  EXPECT_EQ(*destination_view.snapshot(Address{0}, 1),
            (std::vector<std::byte>{std::byte{5}}));
}

TEST(AsyncMemoryEngine, UninitializedSourcePreservesStorageError) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_local_frame({1});

  AsyncMemoryEngine engine;
  const auto handle =
      engine.issue(copy(*manager.view(source), Address{0},
                        *manager.view(destination), Address{0}, 1));
  EXPECT_EQ(engine.progress(), handle);
  expect_storage_failure(engine, handle, AsyncMemoryErrorCode::source_failure,
                         AddressSpaceErrorCode::storage_failure);
  const auto error = engine.result(handle).error().address_space_error;
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->memory_error);
  EXPECT_EQ(error->memory_error->code, MemoryErrorCode::UninitializedRead);
}

TEST(AsyncMemoryEngine, StaleSourcePreservesAddressSpaceError) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_local_frame({1});
  auto source_view = *manager.view(source);
  ASSERT_TRUE(manager.destroy(source));

  AsyncMemoryEngine engine;
  const auto handle = engine.issue(copy(source_view, Address{0},
                                        *manager.view(destination), Address{0},
                                        1));
  EXPECT_EQ(engine.progress(), handle);
  expect_storage_failure(engine, handle, AsyncMemoryErrorCode::source_failure,
                         AddressSpaceErrorCode::stale_resource);
}

TEST(AsyncMemoryEngine, OutOfBoundsSourcePreservesStorageError) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_local_frame({1});

  AsyncMemoryEngine engine;
  const auto handle =
      engine.issue(copy(*manager.view(source), Address{1},
                        *manager.view(destination), Address{0}, 1));
  EXPECT_EQ(engine.progress(), handle);
  expect_storage_failure(engine, handle, AsyncMemoryErrorCode::source_failure,
                         AddressSpaceErrorCode::storage_failure);
  const auto error = engine.result(handle).error().address_space_error;
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->memory_error);
  EXPECT_EQ(error->memory_error->code, MemoryErrorCode::OutOfBounds);
}

TEST(AsyncMemoryEngine, ReadOnlyDestinationPreservesStorageError) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_constant({1});
  auto source_view = *manager.view(source);
  ASSERT_TRUE(source_view.initialize(Address{0},
                                     std::array{std::byte{1}}));

  AsyncMemoryEngine engine;
  const auto handle = engine.issue(copy(source_view, Address{0},
                                        *manager.view(destination), Address{0},
                                        1));
  EXPECT_EQ(engine.progress(), handle);
  expect_storage_failure(engine, handle,
                         AsyncMemoryErrorCode::destination_failure,
                         AddressSpaceErrorCode::storage_failure);
  const auto error = engine.result(handle).error().address_space_error;
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->memory_error);
  EXPECT_EQ(error->memory_error->code,
            MemoryErrorCode::WriteToReadOnlyRegion);
}

TEST(AsyncMemoryEngine, StaleDestinationPreservesAddressSpaceError) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_local_frame({1});
  auto source_view = *manager.view(source);
  auto destination_view = *manager.view(destination);
  ASSERT_TRUE(source_view.initialize(Address{0},
                                     std::array{std::byte{1}}));
  ASSERT_TRUE(manager.destroy(destination));

  AsyncMemoryEngine engine;
  const auto handle = engine.issue(
      copy(source_view, Address{0}, destination_view, Address{0}, 1));
  EXPECT_EQ(engine.progress(), handle);
  expect_storage_failure(engine, handle,
                         AsyncMemoryErrorCode::destination_failure,
                         AddressSpaceErrorCode::stale_resource);
}

TEST(AsyncMemoryEngine, OutOfBoundsDestinationPreservesStorageError) {
  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({1});
  const auto destination = manager.create_local_frame({1});
  auto source_view = *manager.view(source);
  ASSERT_TRUE(source_view.initialize(Address{0},
                                     std::array{std::byte{1}}));

  AsyncMemoryEngine engine;
  const auto handle = engine.issue(copy(source_view, Address{0},
                                        *manager.view(destination), Address{1},
                                        1));
  EXPECT_EQ(engine.progress(), handle);
  expect_storage_failure(engine, handle,
                         AsyncMemoryErrorCode::destination_failure,
                         AddressSpaceErrorCode::storage_failure);
  const auto error = engine.result(handle).error().address_space_error;
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->memory_error);
  EXPECT_EQ(error->memory_error->code, MemoryErrorCode::OutOfBounds);
}

TEST(AsyncMemoryEngine, EmptyProgressAndCrossEngineQueriesAreStale) {
  AsyncMemoryEngine first;
  AsyncMemoryEngine second;
  EXPECT_EQ(first.progress(), std::nullopt);

  AddressSpaceManager manager;
  const auto source = manager.create_local_frame({0});
  const auto destination = manager.create_local_frame({0});
  const auto foreign = second.issue(
      copy(*manager.view(source), Address{0}, *manager.view(destination),
           Address{0}, 0));

  EXPECT_EQ(first.status(foreign).error().code,
            AsyncMemoryErrorCode::stale_handle);
  EXPECT_EQ(first.result(foreign).error().code,
            AsyncMemoryErrorCode::stale_handle);
}

}  // namespace ptxsim::memory::test
