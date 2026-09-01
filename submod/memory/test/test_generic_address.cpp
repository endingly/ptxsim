#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>

#include <ptxsim/memory/address_space/address_space_manager.hpp>
#include <ptxsim/memory/address_space/generic_address.hpp>

namespace ptxsim::memory::test {
namespace {

template <typename Context>
concept HasFunctionParameter =
    requires(Context context) { context.function_parameter; };

template <typename Context>
concept HasTopologyId = requires(Context context) {
  context.thread_id;
  context.cta_id;
  context.grid_id;
};

static_assert(!std::same_as<GenericAddress, Address>);
static_assert(!HasFunctionParameter<ExecutionAddressContext>);
static_assert(!HasTopologyId<ExecutionAddressContext>);

auto full_context(AddressSpaceManager& manager) -> ExecutionAddressContext {
  return ExecutionAddressContext{
      .global = manager.create_global({0}),
      .constant = manager.create_constant({0}),
      .entry_parameter = manager.create_entry_parameter({0}),
      .local = manager.create_local_frame({0}),
      .shared = manager.create_shared({0}),
  };
}

void expect_resolved(GenericAddress address,
                     const ExecutionAddressContext& context, StateSpace space,
                     ResolvedResourceHandle resource, Address region_address) {
  const auto result = resolve(address, context);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->space, space);
  EXPECT_EQ(result->resource, resource);
  EXPECT_EQ(result->region_address, region_address);
}

}  // namespace

TEST(GenericAddress, ResolvesEachWindowBaseAndLastAddress) {
  AddressSpaceManager manager;
  const auto context = full_context(manager);
  const auto last = GenericAddressLayout::window_size - 1;

  expect_resolved(GenericAddress{GenericAddressLayout::global_base}, context,
                  StateSpace::Global, *context.global, Address{0});
  expect_resolved(GenericAddress{GenericAddressLayout::global_base + last},
                  context, StateSpace::Global, *context.global, Address{last});
  expect_resolved(GenericAddress{GenericAddressLayout::constant_base}, context,
                  StateSpace::Constant, *context.constant, Address{0});
  expect_resolved(GenericAddress{GenericAddressLayout::constant_base + last},
                  context, StateSpace::Constant, *context.constant,
                  Address{last});
  expect_resolved(GenericAddress{GenericAddressLayout::entry_parameter_base},
                  context, StateSpace::Parameter, *context.entry_parameter,
                  Address{0});
  expect_resolved(
      GenericAddress{GenericAddressLayout::entry_parameter_base + last},
      context, StateSpace::Parameter, *context.entry_parameter, Address{last});
  expect_resolved(GenericAddress{GenericAddressLayout::local_base}, context,
                  StateSpace::Local, *context.local, Address{0});
  expect_resolved(GenericAddress{GenericAddressLayout::local_base + last},
                  context, StateSpace::Local, *context.local, Address{last});
  expect_resolved(GenericAddress{GenericAddressLayout::shared_base}, context,
                  StateSpace::Shared, *context.shared, Address{0});
  expect_resolved(GenericAddress{GenericAddressLayout::shared_base + last},
                  context, StateSpace::Shared, *context.shared, Address{last});
}

TEST(GenericAddress, AdjacentBoundariesSelectTheNextWindow) {
  AddressSpaceManager manager;
  const auto context = full_context(manager);

  EXPECT_EQ(
      resolve(GenericAddress{GenericAddressLayout::constant_base - 1}, context)
          ->space,
      StateSpace::Global);
  EXPECT_EQ(
      resolve(GenericAddress{GenericAddressLayout::constant_base}, context)
          ->space,
      StateSpace::Constant);
  EXPECT_EQ(
      resolve(GenericAddress{GenericAddressLayout::entry_parameter_base - 1},
              context)
          ->space,
      StateSpace::Constant);
  EXPECT_EQ(resolve(GenericAddress{GenericAddressLayout::entry_parameter_base},
                    context)
                ->space,
            StateSpace::Parameter);
  EXPECT_EQ(
      resolve(GenericAddress{GenericAddressLayout::local_base - 1}, context)
          ->space,
      StateSpace::Parameter);
  EXPECT_EQ(
      resolve(GenericAddress{GenericAddressLayout::local_base}, context)->space,
      StateSpace::Local);
  EXPECT_EQ(
      resolve(GenericAddress{GenericAddressLayout::shared_base - 1}, context)
          ->space,
      StateSpace::Local);
  EXPECT_EQ(resolve(GenericAddress{GenericAddressLayout::shared_base}, context)
                ->space,
            StateSpace::Shared);
}

TEST(GenericAddress, MissingBindingDoesNotFallBackToAnotherResource) {
  AddressSpaceManager manager;
  auto context = full_context(manager);
  context.global.reset();
  const GenericAddress address{12};

  const auto result = resolve(address, context);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(),
            (AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                    address, StateSpace::Global}));

  auto local_context = full_context(manager);
  local_context.local.reset();
  const GenericAddress local_address{GenericAddressLayout::local_base + 12};
  const auto local_result = resolve(local_address, local_context);
  ASSERT_FALSE(local_result);
  EXPECT_EQ(local_result.error(),
            (AddressResolutionError{AddressResolutionErrorCode::missing_binding,
                                    local_address, StateSpace::Local}));
}

TEST(GenericAddress, UnmappedAddressesHaveNoStateSpace) {
  const ExecutionAddressContext context;
  const GenericAddress first{GenericAddressLayout::unmapped_base};
  const GenericAddress last{std::numeric_limits<std::uint64_t>::max()};

  const auto first_result = resolve(first, context);
  ASSERT_FALSE(first_result);
  EXPECT_EQ(
      first_result.error(),
      (AddressResolutionError{AddressResolutionErrorCode::unmapped_address,
                              first, std::nullopt}));
  const auto last_result = resolve(last, context);
  ASSERT_FALSE(last_result);
  EXPECT_EQ(
      last_result.error(),
      (AddressResolutionError{AddressResolutionErrorCode::unmapped_address,
                              last, std::nullopt}));
}

TEST(GenericAddress, ResolutionIsDeterministicAndDoesNotCheckCapacity) {
  AddressSpaceManager manager;
  const auto context = full_context(manager);
  const GenericAddress address{GenericAddressLayout::local_base + 42};

  EXPECT_EQ(resolve(address, context), resolve(address, context));
  const auto result = resolve(address, context);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->region_address, (Address{42}));
}

}  // namespace ptxsim::memory::test
