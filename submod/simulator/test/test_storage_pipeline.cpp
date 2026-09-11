#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {

/** @brief Lower an owned executable from real PTX declarations and instructions. */
auto storage_program(std::string_view source)
    -> std::optional<exec_ir::ExecutableProgram> {
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << "PTX parsing failed";
    for (const auto& diagnostic : ast.diagnostics)
      ADD_FAILURE() << diagnostic.message;
    return std::nullopt;
  }
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!resolved) {
    for (const auto& diagnostic : resolved.error())
      ADD_FAILURE() << diagnostic.message;
    return std::nullopt;
  }
  auto lowered = exec_ir_lowering::lower(*resolved);
  if (!lowered) {
    ADD_FAILURE() << "Lowering error "
                  << static_cast<unsigned>(lowered.error().code);
    return std::nullopt;
  }
  return std::move(*lowered);
}

/** @brief Decode a little-endian output word without host alignment assumptions. */
auto output_word(std::span<const std::byte> bytes, std::size_t offset)
    -> std::uint32_t {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index != 4; ++index)
    value |= std::to_integer<std::uint32_t>(bytes[offset + index])
             << (8 * index);
  return value;
}

/** @brief Encode the public output-buffer pointer at byte zero. */
auto output_arguments(const exec_ir::ExecutableProgram& program)
    -> std::vector<std::byte> {
  const std::array<std::byte, 8> pointer{};
  const std::array arguments{std::span<const std::byte>{pointer}};
  auto packed = pack_entry_arguments(program, common::FunctionId{0}, arguments);
  EXPECT_TRUE(packed);
  return packed ? std::move(*packed) : std::vector<std::byte>{};
}

}  // namespace

TEST(StoragePipeline,
     AutomaticallyAllocatesAlignedSharedAndIsolatedLocalStorage) {
  auto program = storage_program(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.entry storage(.param .u64 output) {
  .shared .align 16 .u32 scratch[2];
  .local .align 8 .u32 private_value;
  .reg .u64 %output, %address, %offset, %shared_ptr, %local_ptr;
  .reg .u32 %thread_id, %cta, %value, %neighbor, %shared_value, %local_value, %index;
  ld.param.u64 %output, [output];
  mov.u32 %thread_id, %tid.x;
  mov.u32 %cta, %ctaid.x;
  mul.lo.u32 %value, %cta, 100;
  add.u32 %value, %value, %thread_id;
  add.u32 %value, %value, 10;
  mov.u64 %shared_ptr, scratch;
  mul.wide.u32 %offset, %thread_id, 4;
  add.u64 %shared_ptr, %shared_ptr, %offset;
  st.shared.u32 [%shared_ptr], %value;
  mov.u64 %local_ptr, private_value;
  st.local.u32 [%local_ptr], %value;
  bar.sync 0;
  sub.u32 %neighbor, 1, %thread_id;
  mul.wide.u32 %offset, %neighbor, 4;
  mov.u64 %shared_ptr, scratch;
  add.u64 %shared_ptr, %shared_ptr, %offset;
  ld.shared.u32 %shared_value, [%shared_ptr];
  ld.local.u32 %local_value, [private_value];
  mul.lo.u32 %index, %cta, 2;
  add.u32 %index, %index, %thread_id;
  mul.wide.u32 %offset, %index, 8;
  add.u64 %address, %output, %offset;
  st.global.u32 [%address], %shared_value;
  st.global.u32 [%address+4], %local_value;
  exit;
}
)ptx");
  ASSERT_TRUE(program);
  runtime::LaunchRuntime runtime{
      execution_model::GridId{81},
      {.cta_dim = {2, 1, 1}, .thread_dim = {2, 1, 1}, .warp_size = 2}};
  const auto global = runtime.address_spaces().create_global({.capacity = 48});
  ASSERT_TRUE(runtime.bind_global(global));
  auto view = runtime.address_spaces().view(global);
  ASSERT_TRUE(view);
  const std::vector<std::byte> sentinel(48, std::byte{0xa5});
  ASSERT_TRUE(view->initialize(memory::Address{0}, sentinel));
  auto arguments = output_arguments(*program);
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic, std::move(arguments)};
  const auto result = simulator.run(256);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->termination, RunTermination::completed);
  std::vector<memory::SharedSpaceHandle> shared_handles;
  for (const auto& cta : runtime.grid()) {
    const auto shared = runtime.shared(cta.id());
    ASSERT_TRUE(shared);
    shared_handles.push_back(*shared);
  }
  ASSERT_EQ(shared_handles.size(), 2U);
  EXPECT_NE(shared_handles[0], shared_handles[1]);
  const auto bytes = view->snapshot(memory::Address{0}, 48);
  ASSERT_TRUE(bytes);
  for (std::size_t cta = 0; cta != 2; ++cta) {
    for (std::size_t thread = 0; thread != 2; ++thread) {
      const auto offset = (cta * 2 + thread) * 8;
      EXPECT_EQ(output_word(*bytes, offset), cta * 100 + (1 - thread) + 10);
      EXPECT_EQ(output_word(*bytes, offset + 4), cta * 100 + thread + 10);
    }
  }
  for (std::size_t index = 32; index != 48; ++index)
    EXPECT_EQ((*bytes)[index], std::byte{0xa5});
}

TEST(StoragePipeline,
     InitializesModuleDataAndPreservesCallerBufferAndConstantPermissions) {
  auto program = storage_program(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .align 16 .u32 initialized[2] = {17, 23};
.const .align 8 .u32 constants[2] = {31, 47};
.entry module_data(.param .u64 output) {
  .reg .u64 %output, %pointer;
  .reg .u32 %a, %b, %c;
  ld.param.u64 %output, [output];
  ld.global.u32 %a, [initialized];
  mov.u64 %pointer, initialized+4;
  ld.global.u32 %b, [%pointer];
  ld.const.u32 %c, [constants+4];
  st.global.u32 [%output], %a;
  st.global.u32 [%output+4], %b;
  st.global.u32 [%output+8], %c;
  exit;
}
)ptx");
  ASSERT_TRUE(program);
  runtime::LaunchRuntime runtime{
      execution_model::GridId{82},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({.capacity = 32});
  ASSERT_TRUE(runtime.bind_global(global));
  auto view = runtime.address_spaces().view(global);
  ASSERT_TRUE(view);
  const std::vector<std::byte> sentinel(32, std::byte{0xa5});
  ASSERT_TRUE(view->initialize(memory::Address{0}, sentinel));
  auto arguments = output_arguments(*program);
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic, std::move(arguments)};
  const auto result = simulator.run(32);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->termination, RunTermination::completed);
  const auto bound = runtime.global();
  ASSERT_TRUE(bound);
  EXPECT_EQ(*bound, global);
  const auto bytes = view->snapshot(memory::Address{0}, 32);
  ASSERT_TRUE(bytes);
  EXPECT_EQ(output_word(*bytes, 0), 17U);
  EXPECT_EQ(output_word(*bytes, 4), 23U);
  EXPECT_EQ(output_word(*bytes, 8), 47U);
  for (std::size_t index = 12; index != 32; ++index)
    EXPECT_EQ((*bytes)[index], std::byte{0xa5});
  const auto constant = runtime.constant();
  ASSERT_TRUE(constant);
  auto constant_view = runtime.address_spaces().view(*constant);
  ASSERT_TRUE(constant_view);
  const std::array<std::byte, 4> replacement{};
  EXPECT_FALSE(constant_view->write(memory::Address{0}, replacement));
  const auto constant_bytes = constant_view->snapshot(memory::Address{0}, 8);
  ASSERT_TRUE(constant_bytes);
  EXPECT_EQ(output_word(*constant_bytes, 0), 31U);
  EXPECT_EQ(output_word(*constant_bytes, 4), 47U);
}

TEST(StoragePipeline, BindsExternalStorageWithoutInitializingCallerBytes) {
  auto program = storage_program(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.extern .global .align 8 .u32 external_data[2];
.entry external_storage() {
  .reg .u32 %value;
  ld.global.u32 %value, [external_data];
  add.u32 %value, %value, 1;
  st.global.u32 [external_data+4], %value;
  exit;
}
)ptx");
  ASSERT_TRUE(program);
  runtime::LaunchRuntime runtime{
      execution_model::GridId{83},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({.capacity = 32});
  ASSERT_TRUE(runtime.bind_global(global));
  auto view = runtime.address_spaces().view(global);
  ASSERT_TRUE(view);
  const std::array<std::byte, 8> initial{std::byte{41}};
  ASSERT_TRUE(view->initialize(memory::Address{16}, initial));
  runtime::StorageLaunchOptions options;
  options.externals.push_back(
      {.name = "external_data", .offset = 16, .extent = 8});
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic,          {},      std::move(options)};
  const auto result = simulator.run(16);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->termination, RunTermination::completed);
  const auto bytes = view->snapshot(memory::Address{16}, 8);
  ASSERT_TRUE(bytes);
  EXPECT_EQ(output_word(*bytes, 0), 41U);
  EXPECT_EQ(output_word(*bytes, 4), 42U);
  EXPECT_EQ(*view->size(), 32U);
}

TEST(StoragePipeline, AllocatesDynamicSharedFromLaunchSizeForEveryCta) {
  auto program = storage_program(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.extern .shared .align 16 .b8 dynamic_data[];
.entry dynamic_storage(.param .u64 output) {
  .reg .u64 %output, %address, %offset;
  .reg .u32 %cta, %value;
  ld.param.u64 %output, [output];
  mov.u32 %cta, %ctaid.x;
  add.u32 %value, %cta, 91;
  st.shared.u32 [dynamic_data+12], %value;
  ld.shared.u32 %value, [dynamic_data+12];
  mul.wide.u32 %offset, %cta, 4;
  add.u64 %address, %output, %offset;
  st.global.u32 [%address], %value;
  exit;
}
)ptx");
  ASSERT_TRUE(program);
  runtime::LaunchRuntime runtime{
      execution_model::GridId{84},
      {.cta_dim = {2, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({.capacity = 8});
  ASSERT_TRUE(runtime.bind_global(global));
  auto arguments = output_arguments(*program);
  runtime::StorageLaunchOptions options;
  options.dynamic_shared_bytes = 16;
  const arith::context arithmetic;
  Simulator simulator{std::move(*program),   runtime,
                      common::FunctionId{0}, arithmetic,
                      std::move(arguments),  std::move(options)};
  const auto result = simulator.run(32);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->termination, RunTermination::completed);
  const auto view = runtime.address_spaces().view(global);
  ASSERT_TRUE(view);
  const auto bytes = view->snapshot(memory::Address{0}, 8);
  ASSERT_TRUE(bytes);
  EXPECT_EQ(output_word(*bytes, 0), 91U);
  EXPECT_EQ(output_word(*bytes, 4), 92U);
}

TEST(StoragePipeline,
     RejectsMissingOrMisalignedExternalBeforeChangingCallerData) {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .u32 initialized = 99;
.extern .global .align 8 .u32 external_data[2];
.entry external_storage() { exit; }
)ptx";
  for (const bool misaligned : {false, true}) {
    SCOPED_TRACE(misaligned);
    auto program = storage_program(source);
    ASSERT_TRUE(program);
    runtime::LaunchRuntime runtime{
        execution_model::GridId{85},
        {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
    const auto global =
        runtime.address_spaces().create_global({.capacity = 32});
    ASSERT_TRUE(runtime.bind_global(global));
    auto view = runtime.address_spaces().view(global);
    ASSERT_TRUE(view);
    const std::vector<std::byte> original(32, std::byte{0xa5});
    ASSERT_TRUE(view->initialize(memory::Address{0}, original));
    runtime::StorageLaunchOptions options;
    if (misaligned)
      options.externals.push_back(
          {.name = "external_data", .offset = 1, .extent = 8});
    const arith::context arithmetic;
    Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                        arithmetic,          {},      std::move(options)};
    const auto result = simulator.run(16);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, RunErrorCode::storage_error);
    EXPECT_TRUE(result.error().storage_error.has_value());
    EXPECT_EQ(*view->size(), original.size());
    const auto bytes = view->snapshot(memory::Address{0}, original.size());
    ASSERT_TRUE(bytes);
    EXPECT_EQ(*bytes, original);
    const auto again = simulator.run(16);
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error(), result.error());
  }
}

TEST(StoragePipeline, RejectsStaleBoundGlobalInsteadOfReplacingIt) {
  auto program = storage_program(R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .u32 initialized = 99;
.entry stale_storage() { exit; }
)ptx");
  ASSERT_TRUE(program);
  runtime::LaunchRuntime runtime{
      execution_model::GridId{86},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({.capacity = 32});
  ASSERT_TRUE(runtime.bind_global(global));
  ASSERT_TRUE(runtime.address_spaces().destroy(global));
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  const auto result = simulator.run(16);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, RunErrorCode::storage_error);
  EXPECT_TRUE(result.error().storage_error.has_value());
}

}  // namespace ptxsim::simulator::test
