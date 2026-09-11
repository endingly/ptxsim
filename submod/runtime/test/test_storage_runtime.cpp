#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::runtime::test {
namespace {

/** @brief Build an instruction-free program for runtime allocation tests. */
auto storage_layout(std::vector<exec_ir::StorageDeclaration> declarations)
    -> std::optional<exec_ir::ExecutableProgram> {
  auto program = exec_ir::ExecutableProgram::create(
      {.functions = {{common::FunctionId{0}, 0, 0, {}}},
       .storage_declarations = std::move(declarations)});
  EXPECT_TRUE(program);
  return program
             ? std::optional<exec_ir::ExecutableProgram>{std::move(*program)}
             : std::nullopt;
}

}  // namespace

TEST(StorageRuntime,
     AppendsToDifferentCallerSharedAndLocalSizesWithoutReplacingHandles) {
  auto program = storage_layout(
      {{.symbol = common::SymbolId{0},
        .name = "shared",
        .space = exec_ir::StorageSpace::shared,
        .extent = 8,
        .alignment = 16,
        .initialization = exec_ir::StorageInitialization::uninitialized},
       {.symbol = common::SymbolId{1},
        .name = "local",
        .space = exec_ir::StorageSpace::local,
        .owner_function = common::FunctionId{0},
        .extent = 4,
        .alignment = 8,
        .initialization = exec_ir::StorageInitialization::uninitialized}});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{91},
      {.cta_dim = {2, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  std::vector<memory::SharedSpaceHandle> shared_handles;
  std::vector<memory::LocalFrameHandle> local_handles;
  std::vector<execution_model::ThreadId> threads;
  for (const auto& cta : runtime.grid()) {
    const auto prefix = 8 + shared_handles.size() * 16;
    const auto shared =
        runtime.address_spaces().create_shared({.size = prefix});
    shared_handles.push_back(shared);
    ASSERT_TRUE(runtime.bind_shared(cta.id(), shared));
    auto view = runtime.address_spaces().view(shared);
    ASSERT_TRUE(view);
    const std::vector<std::byte> bytes(prefix, std::byte{0xa5});
    ASSERT_TRUE(view->initialize(memory::Address{0}, bytes));
    for (const auto& warp : cta)
      for (const auto& thread : warp) {
        threads.push_back(thread.id());
        const auto local =
            runtime.address_spaces().create_local_frame({.size = prefix});
        local_handles.push_back(local);
        ASSERT_TRUE(runtime.bind_local_frame(thread.id(), common::FunctionId{0},
                                             local));
        auto local_view = runtime.address_spaces().view(local);
        ASSERT_TRUE(local_view);
        ASSERT_TRUE(local_view->initialize(memory::Address{0}, bytes));
      }
  }
  ASSERT_TRUE(runtime.prepare_storage(*program));
  ASSERT_EQ(threads.size(), 2U);
  for (std::size_t index = 0; index != threads.size(); ++index) {
    const auto prefix = 8 + index * 16;
    const auto shared_offset = runtime.resolve_symbol_offset(
        {common::SymbolId{0}}, threads[index], common::FunctionId{0});
    const auto local_offset = runtime.resolve_symbol_offset(
        {common::SymbolId{1}}, threads[index], common::FunctionId{0});
    ASSERT_TRUE(shared_offset);
    ASSERT_TRUE(local_offset);
    EXPECT_GE(*shared_offset, prefix);
    EXPECT_EQ(*shared_offset % 16, 0U);
    EXPECT_GE(*local_offset, prefix);
    EXPECT_EQ(*local_offset % 8, 0U);
    const auto shared_view =
        runtime.address_spaces().view(shared_handles[index]);
    const auto local_view = runtime.address_spaces().view(local_handles[index]);
    ASSERT_TRUE(shared_view);
    ASSERT_TRUE(local_view);
    const auto shared_bytes = shared_view->snapshot(memory::Address{0}, prefix);
    const auto local_bytes = local_view->snapshot(memory::Address{0}, prefix);
    ASSERT_TRUE(shared_bytes);
    ASSERT_TRUE(local_bytes);
    EXPECT_EQ(*shared_bytes, std::vector<std::byte>(prefix, std::byte{0xa5}));
    EXPECT_EQ(*local_bytes, std::vector<std::byte>(prefix, std::byte{0xa5}));
    const auto bound_local =
        runtime.local_frame(threads[index], common::FunctionId{0});
    ASSERT_TRUE(bound_local);
    EXPECT_EQ(*bound_local, local_handles[index]);
    const auto initialized =
        local_view->is_initialized(memory::Address{*local_offset}, 4);
    ASSERT_TRUE(initialized);
    EXPECT_FALSE(*initialized);
  }
  const auto repeated = runtime.prepare_storage(*program);
  ASSERT_FALSE(repeated);
  EXPECT_EQ(repeated.error().code, StorageErrorCode::already_prepared);
}

TEST(StorageRuntime, EmptyMetadataRetainsLegacyRuntimeReuse) {
  auto program = storage_layout({});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{92},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  EXPECT_TRUE(runtime.prepare_storage(*program));
  EXPECT_TRUE(runtime.prepare_storage(*program));
  EXPECT_FALSE(runtime.global());
  EXPECT_FALSE(runtime.constant());
}

TEST(StorageRuntime, ZeroDynamicSharedSizeIsValidWhenUnused) {
  auto program = storage_layout(
      {{.symbol = common::SymbolId{0},
        .name = "dynamic",
        .space = exec_ir::StorageSpace::shared,
        .extent = 0,
        .alignment = 16,
        .initialization = exec_ir::StorageInitialization::external,
        .dynamic_shared = true}});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{93},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  EXPECT_TRUE(runtime.prepare_storage(*program));
}

TEST(StorageRuntime, RejectsUnrepresentableGlobalSharedAndLocalLayouts) {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  const std::array declarations{
      exec_ir::StorageDeclaration{
          .symbol = common::SymbolId{0},
          .name = "g",
          .space = exec_ir::StorageSpace::global,
          .extent = maximum,
          .alignment = 1,
          .initialization = exec_ir::StorageInitialization::zero},
      exec_ir::StorageDeclaration{
          .symbol = common::SymbolId{1},
          .name = "s",
          .space = exec_ir::StorageSpace::shared,
          .extent = maximum,
          .alignment = 1,
          .initialization = exec_ir::StorageInitialization::uninitialized},
      exec_ir::StorageDeclaration{
          .symbol = common::SymbolId{2},
          .name = "l",
          .space = exec_ir::StorageSpace::local,
          .owner_function = common::FunctionId{0},
          .extent = maximum,
          .alignment = 1,
          .initialization = exec_ir::StorageInitialization::uninitialized}};
  for (const auto& declaration : declarations) {
    auto program = storage_layout({declaration});
    ASSERT_TRUE(program);
    LaunchRuntime runtime{
        execution_model::GridId{94},
        {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
    const auto prepared = runtime.prepare_storage(*program);
    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error().code, StorageErrorCode::invalid_symbol);
  }
}

TEST(StorageRuntime, RejectsUnrepresentableDynamicSharedBytes) {
  auto program = storage_layout(
      {{.symbol = common::SymbolId{0},
        .name = "dynamic",
        .space = exec_ir::StorageSpace::shared,
        .extent = 0,
        .alignment = 1,
        .initialization = exec_ir::StorageInitialization::external,
        .dynamic_shared = true}});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{95},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  StorageLaunchOptions options{.dynamic_shared_bytes =
                                   std::numeric_limits<std::size_t>::max()};
  const auto prepared = runtime.prepare_storage(*program, options);
  ASSERT_FALSE(prepared);
  EXPECT_EQ(prepared.error().code, StorageErrorCode::invalid_symbol);
}

TEST(StorageRuntime, DynamicSharedDeclarationsAliasOneAlignedSegment) {
  auto program = storage_layout(
      {{.symbol = common::SymbolId{0},
        .name = "a",
        .space = exec_ir::StorageSpace::shared,
        .extent = 0,
        .alignment = 8,
        .initialization = exec_ir::StorageInitialization::external,
        .dynamic_shared = true},
       {.symbol = common::SymbolId{1},
        .name = "static",
        .space = exec_ir::StorageSpace::shared,
        .extent = 8,
        .alignment = 4,
        .initialization = exec_ir::StorageInitialization::uninitialized},
       {.symbol = common::SymbolId{2},
        .name = "b",
        .space = exec_ir::StorageSpace::shared,
        .extent = 0,
        .alignment = 16,
        .initialization = exec_ir::StorageInitialization::external,
        .dynamic_shared = true}});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{96},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  ASSERT_TRUE(runtime.prepare_storage(*program, {.dynamic_shared_bytes = 32}));
  for (const auto& cta : runtime.grid())
    for (const auto& warp : cta)
      for (const auto& thread : warp) {
        const auto first = runtime.resolve_symbol_offset(
            {common::SymbolId{0}}, thread.id(), common::FunctionId{0});
        const auto second = runtime.resolve_symbol_offset(
            {common::SymbolId{2}}, thread.id(), common::FunctionId{0});
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        EXPECT_EQ(*first, 16U);
        EXPECT_EQ(*second, *first);
        const auto shared = runtime.shared(cta.id());
        ASSERT_TRUE(shared);
        const auto view = runtime.address_spaces().view(*shared);
        ASSERT_TRUE(view);
        EXPECT_EQ(*view->size(), 48U);
      }
}

TEST(StorageRuntime, LocalLayoutsAppendIndependentlyForEachFunction) {
  const auto program = exec_ir::ExecutableProgram::create(
      {.functions = {{common::FunctionId{0}, 0, 0, {}},
                     {common::FunctionId{1}, 0, 0, {}}},
       .storage_declarations = {
           {.symbol = common::SymbolId{0},
            .name = "first",
            .space = exec_ir::StorageSpace::local,
            .owner_function = common::FunctionId{0},
            .extent = 4,
            .alignment = 16,
            .initialization = exec_ir::StorageInitialization::uninitialized},
           {.symbol = common::SymbolId{1},
            .name = "second",
            .space = exec_ir::StorageSpace::local,
            .owner_function = common::FunctionId{1},
            .extent = 4,
            .alignment = 16,
            .initialization = exec_ir::StorageInitialization::uninitialized}}});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{97},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  for (const auto& cta : runtime.grid())
    for (const auto& warp : cta)
      for (const auto& thread : warp) {
        const auto first =
            runtime.address_spaces().create_local_frame({.size = 8});
        const auto second =
            runtime.address_spaces().create_local_frame({.size = 32});
        ASSERT_TRUE(runtime.bind_local_frame(thread.id(), common::FunctionId{0},
                                             first));
        ASSERT_TRUE(runtime.bind_local_frame(thread.id(), common::FunctionId{1},
                                             second));
        ASSERT_TRUE(runtime.prepare_storage(*program));
        const auto first_offset = runtime.resolve_symbol_offset(
            {common::SymbolId{0}}, thread.id(), common::FunctionId{0});
        const auto second_offset = runtime.resolve_symbol_offset(
            {common::SymbolId{1}}, thread.id(), common::FunctionId{1});
        ASSERT_TRUE(first_offset);
        ASSERT_TRUE(second_offset);
        EXPECT_EQ(*first_offset, 16U);
        EXPECT_EQ(*second_offset, 32U);
        EXPECT_FALSE(runtime.resolve_symbol_offset(
            {common::SymbolId{0}}, thread.id(), common::FunctionId{1}));
        EXPECT_EQ(*runtime.address_spaces().view(first)->size(), 20U);
        EXPECT_EQ(*runtime.address_spaces().view(second)->size(), 36U);
      }
}

TEST(StorageRuntime, UnsizedExternalRequiresNonemptyCallerRange) {
  auto program = storage_layout(
      {{.symbol = common::SymbolId{0},
        .name = "external",
        .space = exec_ir::StorageSpace::global,
        .extent = 0,
        .alignment = 4,
        .initialization = exec_ir::StorageInitialization::external,
        .external_extent_multiple = 8}});
  ASSERT_TRUE(program);
  LaunchRuntime runtime{
      execution_model::GridId{98},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({.capacity = 16});
  ASSERT_TRUE(runtime.bind_global(global));
  const auto empty =
      runtime.prepare_storage(*program, {.externals = {{"external", 0, 0}}});
  ASSERT_FALSE(empty);
  EXPECT_EQ(empty.error().code, StorageErrorCode::invalid_external_binding);
  for (const auto extent : {1U, 4U, 12U}) {
    const auto invalid = runtime.prepare_storage(
        *program, {.externals = {{"external", 0, extent}}});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, StorageErrorCode::invalid_external_binding);
  }
  EXPECT_TRUE(
      runtime.prepare_storage(*program, {.externals = {{"external", 0, 16}}}));
}

TEST(StorageRuntime, StaleContextResourcesFailBeforeModuleMutation) {
  for (const auto space :
       {exec_ir::StorageSpace::shared, exec_ir::StorageSpace::local}) {
    const auto program = storage_layout(
        {{.symbol = common::SymbolId{0},
          .name = "global",
          .space = exec_ir::StorageSpace::global,
          .extent = 4,
          .alignment = 4,
          .initialization = exec_ir::StorageInitialization::zero},
         {.symbol = common::SymbolId{1},
          .name = "context",
          .space = space,
          .owner_function = common::FunctionId{0},
          .extent = 4,
          .alignment = 4,
          .initialization = exec_ir::StorageInitialization::uninitialized}});
    ASSERT_TRUE(program);
    LaunchRuntime runtime{
        execution_model::GridId{99},
        {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
    auto& manager = runtime.address_spaces();
    const auto global = manager.create_global({.capacity = 4});
    ASSERT_TRUE(runtime.bind_global(global));
    auto view = manager.view(global);
    ASSERT_TRUE(view);
    const std::array<std::byte, 4> marker{std::byte{0x7b}};
    ASSERT_TRUE(view->initialize(memory::Address{0}, marker));
    for (const auto& cta : runtime.grid())
      for (const auto& warp : cta)
        for (const auto& thread : warp) {
          if (space == exec_ir::StorageSpace::shared) {
            const auto handle = manager.create_shared({.size = 4});
            ASSERT_TRUE(runtime.bind_shared(cta.id(), handle));
            ASSERT_TRUE(manager.destroy(handle));
          } else {
            const auto handle = manager.create_local_frame({.size = 4});
            ASSERT_TRUE(runtime.bind_local_frame(
                thread.id(), common::FunctionId{0}, handle));
            ASSERT_TRUE(manager.destroy(handle));
          }
        }
    for (int attempt = 0; attempt != 2; ++attempt) {
      const auto prepared = runtime.prepare_storage(*program);
      ASSERT_FALSE(prepared);
      EXPECT_EQ(prepared.error().code, StorageErrorCode::address_space_failure);
      EXPECT_EQ(*view->size(), 4U);
      std::array<std::byte, 4> bytes{};
      ASSERT_TRUE(view->read(memory::Address{0}, bytes));
      EXPECT_EQ(bytes, marker);
    }
  }
}

}  // namespace ptxsim::runtime::test
