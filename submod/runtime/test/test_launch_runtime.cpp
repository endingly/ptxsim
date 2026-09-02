#include <gtest/gtest.h>

#include <concepts>
#include <variant>

#include <ptxsim/common/ids.hpp>
#include <ptxsim/memory/memory.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::runtime::test {
namespace {

using execution_model::CtaId;
using execution_model::GridId;
using execution_model::ThreadId;

static_assert(std::same_as<execution_model::ProgramCounter,
                           common::ProgramCounter>);

auto shape() -> execution_model::GridShape {
  return {
      .cta_dim = {2, 1, 1},
      .thread_dim = {33, 1, 1},
      .warp_size = 32,
  };
}

auto cta(std::uint64_t index) -> CtaId { return {GridId{9}, index}; }
auto thread(std::uint64_t index) -> ThreadId { return {GridId{9}, index}; }

}  // namespace

TEST(LaunchRuntimeTest, OwnsTopologyAndRoutesLaunchBindings) {
  LaunchRuntime runtime(GridId{9}, shape());
  const auto first_cta = cta(0);
  const auto second_cta = cta(1);
  const auto first_thread = thread(0);
  const auto second_thread = thread(33);
  const common::FunctionId function{3};

  EXPECT_EQ(runtime.grid().cta_count(), 2u);
  EXPECT_EQ(runtime.grid().thread_count(), 66u);
  EXPECT_EQ(runtime.grid().warp_count(), 4u);
  EXPECT_FALSE(runtime.global());

  const auto global = runtime.address_spaces().create_global({.capacity = 16});
  const auto constant = runtime.address_spaces().create_constant({.capacity = 16});
  const auto entry =
      runtime.address_spaces().create_entry_parameter({.size = 16});
  const auto shared_a = runtime.address_spaces().create_shared({.size = 16});
  const auto shared_b = runtime.address_spaces().create_shared({.size = 16});
  const auto local_a = runtime.address_spaces().create_local_frame({.size = 16});
  const auto local_b = runtime.address_spaces().create_local_frame({.size = 16});
  const auto frame_a = runtime.registers().create_frame({.slot_widths = {}});
  const auto frame_b = runtime.registers().create_frame({.slot_widths = {}});
  ASSERT_TRUE(frame_a);
  ASSERT_TRUE(frame_b);
  const auto tmem_a = runtime.tensor_memory().create_space();
  const auto tmem_b = runtime.tensor_memory().create_space();

  ASSERT_TRUE(runtime.bind_global(global));
  ASSERT_TRUE(runtime.bind_constant(constant));
  ASSERT_TRUE(runtime.bind_entry_parameter(entry));
  ASSERT_TRUE(runtime.bind_shared(first_cta, shared_a));
  ASSERT_TRUE(runtime.bind_shared(second_cta, shared_b));
  ASSERT_TRUE(runtime.bind_tensor_memory(first_cta, tmem_a));
  ASSERT_TRUE(runtime.bind_tensor_memory(second_cta, tmem_b));
  ASSERT_TRUE(runtime.bind_register_frame(first_thread, function, *frame_a));
  ASSERT_TRUE(runtime.bind_register_frame(second_thread, function, *frame_b));
  ASSERT_TRUE(runtime.bind_local_frame(first_thread, function, local_a));
  ASSERT_TRUE(runtime.bind_local_frame(second_thread, function, local_b));

  EXPECT_EQ(*runtime.global(), global);
  EXPECT_EQ(*runtime.constant(), constant);
  EXPECT_EQ(*runtime.entry_parameter(), entry);
  EXPECT_EQ(*runtime.shared(first_cta), shared_a);
  EXPECT_EQ(*runtime.shared(second_cta), shared_b);
  EXPECT_EQ(*runtime.tensor_memory_space(first_cta), tmem_a);
  EXPECT_EQ(*runtime.tensor_memory_space(second_cta), tmem_b);
  EXPECT_EQ(*runtime.register_frame(first_thread, function), *frame_a);
  EXPECT_EQ(*runtime.local_frame(second_thread, function), local_b);
  EXPECT_EQ(runtime.grid().thread(first_thread).status(),
            execution_model::ThreadStatus::Ready);

  const auto first_context = runtime.address_context(first_thread, function);
  const auto second_context = runtime.address_context(second_thread, function);
  ASSERT_TRUE(first_context);
  ASSERT_TRUE(second_context);
  EXPECT_EQ(first_context->global, global);
  EXPECT_EQ(first_context->constant, constant);
  EXPECT_EQ(first_context->entry_parameter, entry);
  EXPECT_EQ(first_context->local, local_a);
  EXPECT_EQ(first_context->shared, shared_a);
  EXPECT_EQ(second_context->local, local_b);
  EXPECT_EQ(second_context->shared, shared_b);

  const auto resolved_global = memory::resolve({.value = 7}, *first_context);
  const auto resolved_constant = memory::resolve(
      {.value = memory::GenericAddressLayout::constant_base + 7},
      *first_context);
  const auto resolved_entry = memory::resolve(
      {.value = memory::GenericAddressLayout::entry_parameter_base + 7},
      *first_context);
  const auto resolved_local = memory::resolve(
      {.value = memory::GenericAddressLayout::local_base + 7}, *first_context);
  const auto resolved_shared = memory::resolve(
      {.value = memory::GenericAddressLayout::shared_base + 7}, *first_context);
  ASSERT_TRUE(resolved_global);
  ASSERT_TRUE(resolved_constant);
  ASSERT_TRUE(resolved_entry);
  ASSERT_TRUE(resolved_local);
  ASSERT_TRUE(resolved_shared);
  EXPECT_EQ(std::get<memory::GlobalSpaceHandle>(resolved_global->resource),
            global);
  EXPECT_EQ(std::get<memory::ConstantSpaceHandle>(resolved_constant->resource),
            constant);
  EXPECT_EQ(std::get<memory::EntryParameterHandle>(resolved_entry->resource),
            entry);
  EXPECT_EQ(std::get<memory::LocalFrameHandle>(resolved_local->resource),
            local_a);
  EXPECT_EQ(std::get<memory::SharedSpaceHandle>(resolved_shared->resource),
            shared_a);
}

TEST(LaunchRuntimeTest, RejectsForeignInvalidAndDuplicateBindings) {
  LaunchRuntime runtime(GridId{9}, shape());
  const auto local_global = runtime.address_spaces().create_global({.capacity = 8});
  memory::AddressSpaceManager foreign_spaces;
  memory::RegisterManager foreign_registers;
  memory::TensorMemoryManager foreign_tensor_memory;
  const auto foreign_global = foreign_spaces.create_global({.capacity = 8});
  const auto foreign_frame = foreign_registers.create_frame({.slot_widths = {}});
  ASSERT_TRUE(foreign_frame);
  const auto foreign_tmem = foreign_tensor_memory.create_space();
  const auto stale_shared = runtime.address_spaces().create_shared({.size = 8});
  const auto stale_tmem = runtime.tensor_memory().create_space();
  ASSERT_TRUE(runtime.address_spaces().destroy(stale_shared));
  ASSERT_TRUE(runtime.tensor_memory().destroy(stale_tmem));

  const auto foreign = runtime.bind_shared(
      CtaId{GridId{10}, 0}, runtime.address_spaces().create_shared({.size = 8}));
  ASSERT_FALSE(foreign);
  EXPECT_EQ(foreign.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::shared}));
  const auto invalid_global = runtime.bind_global(foreign_global);
  ASSERT_FALSE(invalid_global);
  EXPECT_EQ(invalid_global.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::global}));
  const auto invalid_shared = runtime.bind_shared(cta(0), stale_shared);
  ASSERT_FALSE(invalid_shared);
  EXPECT_EQ(invalid_shared.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::shared}));
  const auto invalid_frame = runtime.bind_register_frame(
      thread(0), common::FunctionId{1}, *foreign_frame);
  ASSERT_FALSE(invalid_frame);
  EXPECT_EQ(invalid_frame.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::register_frame}));
  const auto invalid_tmem = runtime.bind_tensor_memory(cta(0), foreign_tmem);
  ASSERT_FALSE(invalid_tmem);
  EXPECT_EQ(invalid_tmem.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::tensor_memory}));
  const auto stale_tensor = runtime.bind_tensor_memory(cta(0), stale_tmem);
  ASSERT_FALSE(stale_tensor);
  EXPECT_EQ(stale_tensor.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::invalid_resource,
                                 RuntimeResourceKind::tensor_memory}));

  ASSERT_TRUE(runtime.bind_global(local_global));
  const auto duplicate = runtime.bind_global(local_global);
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::duplicate_binding,
                                 RuntimeResourceKind::global}));
  const auto missing = runtime.local_frame(thread(0), common::FunctionId{7});
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::missing_binding,
                                 RuntimeResourceKind::local_frame}));
}

TEST(LaunchRuntimeTest, ContextLeavesAbsentBindingsForMemoryResolution) {
  LaunchRuntime runtime(GridId{9}, shape());
  const auto context = runtime.address_context(thread(0), common::FunctionId{1});
  ASSERT_TRUE(context);
  EXPECT_FALSE(context->global);
  EXPECT_FALSE(context->local);
  EXPECT_FALSE(context->shared);

  const auto missing_local = memory::resolve(
      {.value = memory::GenericAddressLayout::local_base}, *context);
  ASSERT_FALSE(missing_local);
  EXPECT_EQ(missing_local.error().code,
            memory::AddressResolutionErrorCode::missing_binding);
  EXPECT_EQ(missing_local.error().space, memory::StateSpace::Local);

  const auto foreign_thread =
      runtime.address_context(ThreadId{GridId{10}, 0}, common::FunctionId{1});
  ASSERT_FALSE(foreign_thread);
  EXPECT_EQ(foreign_thread.error().code,
            RuntimeBindingErrorCode::foreign_topology);
}

TEST(LaunchRuntimeTest, ScopedQueriesRejectForeignTopology) {
  LaunchRuntime runtime(GridId{9}, shape());

  const auto foreign_cta = runtime.shared(CtaId{GridId{10}, 0});
  ASSERT_FALSE(foreign_cta);
  EXPECT_EQ(foreign_cta.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::shared}));
  const auto missing_cta = runtime.tensor_memory_space(cta(2));
  ASSERT_FALSE(missing_cta);
  EXPECT_EQ(missing_cta.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::tensor_memory}));

  const auto foreign_thread = runtime.register_frame(
      ThreadId{GridId{10}, 0}, common::FunctionId{1});
  ASSERT_FALSE(foreign_thread);
  EXPECT_EQ(foreign_thread.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::register_frame}));
  const auto missing_thread =
      runtime.local_frame(thread(66), common::FunctionId{1});
  ASSERT_FALSE(missing_thread);
  EXPECT_EQ(missing_thread.error(),
            (RuntimeBindingError{RuntimeBindingErrorCode::foreign_topology,
                                 RuntimeResourceKind::local_frame}));
}

TEST(LaunchRuntimeTest, DestroyedBoundResourceStaysManagerValidated) {
  LaunchRuntime runtime(GridId{9}, shape());
  const auto shared = runtime.address_spaces().create_shared({.size = 8});
  ASSERT_TRUE(runtime.bind_shared(cta(0), shared));
  ASSERT_TRUE(runtime.address_spaces().destroy(shared));

  const auto context = runtime.address_context(thread(0), common::FunctionId{1});
  ASSERT_TRUE(context);
  ASSERT_TRUE(context->shared);
  const auto view = runtime.address_spaces().view(*context->shared);
  ASSERT_FALSE(view);
  EXPECT_EQ(view.error().code, memory::AddressSpaceErrorCode::stale_resource);
}

}  // namespace ptxsim::runtime::test
