#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {

/** @brief Parse, resolve, and lower a complete PTX module for ABI tests. */
auto lower_program(std::string_view source)
    -> std::optional<exec_ir::ExecutableProgram> {
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast) {
    ADD_FAILURE() << "PTX parse failed";
    return std::nullopt;
  }
  const auto module = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!module) {
    ADD_FAILURE() << "PTX resolve failed";
    return std::nullopt;
  }
  auto program = exec_ir_lowering::lower(*module);
  if (!program) {
    ADD_FAILURE() << "PTX lowering failed: "
                  << static_cast<unsigned>(program.error().code);
    return std::nullopt;
  }
  return std::move(*program);
}

/** @brief Encode one 32-bit scalar as PTX little-endian parameter bytes. */
auto little_endian(std::uint32_t value) -> std::array<std::byte, 4> {
  std::array<std::byte, 4> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(value >> (index * 8U)));
  }
  return result;
}

/** @brief Encode one 64-bit scalar as PTX little-endian parameter bytes. */
auto little_endian(std::uint64_t value) -> std::array<std::byte, 8> {
  std::array<std::byte, 8> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(value >> (index * 8U)));
  }
  return result;
}

/** @brief Write a 32-bit expected value at a known PTX byte offset. */
void write_little_endian(std::vector<std::byte>& destination,
                         std::size_t offset, std::uint32_t value) {
  const auto bytes = little_endian(value);
  std::ranges::copy(bytes,
                    destination.begin() + static_cast<std::ptrdiff_t>(offset));
}

/** @brief Write a 64-bit expected value at a known PTX byte offset. */
void write_little_endian(std::vector<std::byte>& destination,
                         std::size_t offset, std::uint64_t value) {
  const auto bytes = little_endian(value);
  std::ranges::copy(bytes,
                    destination.begin() + static_cast<std::ptrdiff_t>(offset));
}

/** @brief Return a one-thread runtime shape for deterministic ABI launches. */
auto one_thread_shape() -> execution_model::GridShape {
  return {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1};
}

/** @brief Return the sole thread in a one-thread test launch. */
auto only_thread(runtime::LaunchRuntime& runtime) -> execution_model::Thread& {
  return runtime.grid()
      .cta(execution_model::CtaId{runtime.grid().id(), 0})
      .warp(0)
      .thread(execution_model::LaneId{0});
}

/** @brief Consume a full-width launch address before accessing global storage. */
constexpr std::string_view kPointerLoad = R"ptx(
.entry load_address(.param .u64 address) {
  .reg .u64 %address;
  .reg .u32 %value;
  ld.param.u64 %address, [address];
  ld.global.u32 %value, [%address];
  exit;
}
)ptx";

}  // namespace

TEST(EntryArguments, PacksMixedWidthGemmSignatureAtIndependentOffsets) {
  const auto program = lower_program(R"ptx(
.entry gemm(
    .param .u32 M,
    .param .align 16 .u64 .ptr .global .align 4 A,
    .param .u32 N,
    .param .u64 .ptr .global .align 8 B,
    .param .u32 K,
    .param .u64 .ptr .global .align 4 C,
    .param .u32 lda,
    .param .u32 ldb,
    .param .u32 ldc) {
  exit;
}
)ptx");
  ASSERT_TRUE(program);

  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(layout->get().entry_parameters,
            (std::vector<exec_ir::EntryParameterLayout>{
                {0, 4, 4},
                {16, 8, 16},
                {24, 4, 4},
                {32, 8, 8},
                {40, 4, 4},
                {48, 8, 8},
                {56, 4, 4},
                {60, 4, 4},
                {64, 4, 4},
            }));
  EXPECT_EQ(layout->get().entry_parameter_size, 68U);

  const auto m = little_endian(std::uint32_t{2});
  const auto a = little_endian(std::uint64_t{0x0102030405060708ULL});
  const auto n = little_endian(std::uint32_t{3});
  const auto b = little_endian(std::uint64_t{0x1112131415161718ULL});
  const auto k = little_endian(std::uint32_t{5});
  const auto c = little_endian(std::uint64_t{0x2122232425262728ULL});
  const auto lda = little_endian(std::uint32_t{7});
  const auto ldb = little_endian(std::uint32_t{11});
  const auto ldc = little_endian(std::uint32_t{17});
  const std::array arguments{
      std::span<const std::byte>{m},   std::span<const std::byte>{a},
      std::span<const std::byte>{n},   std::span<const std::byte>{b},
      std::span<const std::byte>{k},   std::span<const std::byte>{c},
      std::span<const std::byte>{lda}, std::span<const std::byte>{ldb},
      std::span<const std::byte>{ldc}};

  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  std::vector<std::byte> expected(68, std::byte{0});
  write_little_endian(expected, 0, std::uint32_t{2});
  write_little_endian(expected, 16, std::uint64_t{0x0102030405060708ULL});
  write_little_endian(expected, 24, std::uint32_t{3});
  write_little_endian(expected, 32, std::uint64_t{0x1112131415161718ULL});
  write_little_endian(expected, 40, std::uint32_t{5});
  write_little_endian(expected, 48, std::uint64_t{0x2122232425262728ULL});
  write_little_endian(expected, 56, std::uint32_t{7});
  write_little_endian(expected, 60, std::uint32_t{11});
  write_little_endian(expected, 64, std::uint32_t{17});
  EXPECT_EQ(*packed, expected);
}

TEST(EntryArguments, LaunchesGlobalBuffersThroughPackedGemmArguments) {
  const auto program = lower_program(R"ptx(
.entry sum_inputs(
    .param .u32 M,
    .param .align 16 .u64 .ptr .global .align 4 A,
    .param .u32 N,
    .param .u64 .ptr .global .align 8 B,
    .param .u32 K,
    .param .u64 .ptr .global .align 4 C,
    .param .u32 lda,
    .param .u32 ldb,
    .param .u32 ldc) {
  .reg .b64 %rd<3>;
  .reg .u32 %r<8>;
  ld.param.u32 %r0, [M];
  ld.param.u64 %rd0, [A];
  ld.param.u32 %r1, [N];
  ld.param.u64 %rd1, [B];
  ld.param.u32 %r2, [K];
  ld.param.u64 %rd2, [C];
  ld.param.u32 %r3, [lda];
  ld.param.u32 %r4, [ldb];
  ld.param.u32 %r5, [ldc];
  ld.global.u32 %r6, [%rd0];
  ld.global.u32 %r7, [%rd1];
  add.u32 %r6, %r6, %r7;
  add.u32 %r6, %r6, %r0;
  add.u32 %r6, %r6, %r1;
  add.u32 %r6, %r6, %r2;
  add.u32 %r6, %r6, %r3;
  add.u32 %r6, %r6, %r4;
  add.u32 %r6, %r6, %r5;
  st.global.u32 [%rd2], %r6;
  exit;
}
)ptx");
  ASSERT_TRUE(program);

  constexpr std::uint64_t a_address = 0;
  constexpr std::uint64_t b_address = 16;
  constexpr std::uint64_t c_address = 32;
  const auto m = little_endian(std::uint32_t{2});
  const auto a = little_endian(a_address);
  const auto n = little_endian(std::uint32_t{3});
  const auto b = little_endian(b_address);
  const auto k = little_endian(std::uint32_t{5});
  const auto c = little_endian(c_address);
  const auto lda = little_endian(std::uint32_t{7});
  const auto ldb = little_endian(std::uint32_t{11});
  const auto ldc = little_endian(std::uint32_t{17});
  const std::array arguments{
      std::span<const std::byte>{m},   std::span<const std::byte>{a},
      std::span<const std::byte>{n},   std::span<const std::byte>{b},
      std::span<const std::byte>{k},   std::span<const std::byte>{c},
      std::span<const std::byte>{lda}, std::span<const std::byte>{ldb},
      std::span<const std::byte>{ldc}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);

  runtime::LaunchRuntime runtime{execution_model::GridId{91},
                                 one_thread_shape()};
  const auto global = runtime.address_spaces().create_global({64});
  ASSERT_TRUE(runtime.bind_global(global));
  auto global_view = runtime.address_spaces().view(global);
  ASSERT_TRUE(global_view);
  std::vector<std::byte> sentinel(64, std::byte{0xa5});
  ASSERT_TRUE(global_view->initialize(memory::Address{0}, sentinel));
  ASSERT_TRUE(global_view->initialize(memory::Address{a_address},
                                      little_endian(std::uint32_t{11})));
  ASSERT_TRUE(global_view->initialize(memory::Address{b_address},
                                      little_endian(std::uint32_t{13})));

  const arith::context arithmetic;
  Simulator simulator{*program, runtime, common::FunctionId{0}, arithmetic,
                      *packed};
  const auto run = simulator.run(32);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_EQ(run->issued_groups, 20U);

  const auto result = global_view->snapshot(memory::Address{0}, 64);
  ASSERT_TRUE(result);
  auto expected = sentinel;
  write_little_endian(expected, a_address, std::uint32_t{11});
  write_little_endian(expected, b_address, std::uint32_t{13});
  write_little_endian(expected, c_address, std::uint32_t{69});
  EXPECT_EQ(*result, expected);
}

TEST(EntryArguments, PacksAlignedByteArraysAndLoadsTheirBoundaryBytes) {
  const auto program = lower_program(R"ptx(
.entry array_bytes(.param .align 16 .b8 scalar,
                   .param .align 16 .b8 bytes[8]) {
  .reg .u32 %r<5>;
  ld.param.u8 %r0, [scalar];
  ld.param.u8 %r1, [bytes];
  ld.param.u8 %r2, [bytes+7];
  add.u32 %r3, %r0, %r1;
  add.u32 %r4, %r3, %r2;
  exit;
}
)ptx");
  ASSERT_TRUE(program);
  const auto layout = program->function_layout(common::FunctionId{0});
  ASSERT_TRUE(layout);
  EXPECT_EQ(
      layout->get().entry_parameters,
      (std::vector<exec_ir::EntryParameterLayout>{{0, 1, 16}, {16, 8, 16}}));
  EXPECT_EQ(layout->get().entry_parameter_size, 24U);

  const std::array scalar{std::byte{0x01}};
  const std::array bytes{std::byte{0x12}, std::byte{0xa5}, std::byte{0xa5},
                         std::byte{0xa5}, std::byte{0xa5}, std::byte{0xa5},
                         std::byte{0xa5}, std::byte{0x34}};
  const std::array arguments{std::span<const std::byte>{scalar},
                             std::span<const std::byte>{bytes}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  std::vector<std::byte> expected(24, std::byte{0});
  expected[0] = scalar[0];
  std::ranges::copy(bytes, expected.begin() + 16);
  EXPECT_EQ(*packed, expected);

  runtime::LaunchRuntime runtime{execution_model::GridId{92},
                                 one_thread_shape()};
  const arith::context arithmetic;
  Simulator simulator{*program, runtime, common::FunctionId{0}, arithmetic,
                      *packed};
  const auto run = simulator.run(6);
  ASSERT_TRUE(run);
  EXPECT_EQ(*run, (RunReport{RunTermination::completed, 6}));
  const auto frame =
      runtime.register_frame(only_thread(runtime).id(), common::FunctionId{0});
  ASSERT_TRUE(frame);
  const auto registers = runtime.registers().view(*frame);
  ASSERT_TRUE(registers);
  for (const auto [slot, expected_value] :
       std::array{std::pair{0U, 0x01U}, std::pair{1U, 0x12U},
                  std::pair{2U, 0x34U}, std::pair{4U, 0x47U}}) {
    const auto value = registers->read(common::RegisterSlot{slot});
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, common::RawValue::b32(expected_value));
  }
}

TEST(EntryArguments, ReportsInputErrorsBeforeRuntimeInitialization) {
  const auto program = lower_program(R"ptx(
.entry no_arguments() { exit; }
.entry arguments(.param .u32 count, .param .align 8 .u64 address) {
  .reg .u32 %value;
  ld.param.u32 %value, [count];
  exit;
}
)ptx");
  ASSERT_TRUE(program);

  const std::array<std::span<const std::byte>, 0> no_arguments{};
  const auto empty =
      pack_entry_arguments(*program, common::FunctionId{0}, no_arguments);
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());

  const auto invalid =
      pack_entry_arguments(*program, common::FunctionId{9}, no_arguments);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, EntryArgumentErrorCode::program_error);
  ASSERT_TRUE(invalid.error().program_error);
  EXPECT_EQ(invalid.error().program_error->code,
            exec_ir::ProgramErrorCode::function_not_found);

  const auto count = little_endian(std::uint32_t{4});
  const auto address = little_endian(std::uint64_t{16});
  const std::array one_argument{std::span<const std::byte>{count}};
  const auto wrong_count =
      pack_entry_arguments(*program, common::FunctionId{1}, one_argument);
  ASSERT_FALSE(wrong_count);
  EXPECT_EQ(wrong_count.error(),
            (EntryArgumentError{
                .code = EntryArgumentErrorCode::argument_count_mismatch,
                .expected = 2,
                .actual = 1}));

  const std::array wrong_size_arguments{std::span<const std::byte>{count},
                                        std::span<const std::byte>{count}};
  const auto wrong_size = pack_entry_arguments(*program, common::FunctionId{1},
                                               wrong_size_arguments);
  ASSERT_FALSE(wrong_size);
  EXPECT_EQ(wrong_size.error(),
            (EntryArgumentError{
                .code = EntryArgumentErrorCode::argument_size_mismatch,
                .argument_index = 1,
                .expected = 8,
                .actual = 4}));

  const std::array valid_arguments{std::span<const std::byte>{count},
                                   std::span<const std::byte>{address}};
  auto packed =
      pack_entry_arguments(*program, common::FunctionId{1}, valid_arguments);
  ASSERT_TRUE(packed);
  ASSERT_EQ(packed->size(), 16U);
  packed->pop_back();

  runtime::LaunchRuntime runtime{execution_model::GridId{93},
                                 one_thread_shape()};
  const arith::context arithmetic;
  Simulator simulator{*program, runtime, common::FunctionId{1}, arithmetic,
                      std::move(*packed)};
  const auto run = simulator.run(2);
  ASSERT_FALSE(run);
  EXPECT_EQ(run.error().code, RunErrorCode::entry_parameter_size_mismatch);
  EXPECT_EQ(run.error().entry_parameter_size_error,
            (EntryParameterSizeError{.expected = 16, .actual = 15}));
  EXPECT_FALSE(runtime.entry_parameter());
  EXPECT_FALSE(
      runtime.register_frame(only_thread(runtime).id(), common::FunctionId{1}));
}

TEST(EntryArguments, PreservesMissingGlobalBindingAfterLoadingAnArgument) {
  const auto program = lower_program(kPointerLoad);
  ASSERT_TRUE(program);
  const auto address = little_endian(std::uint64_t{8});
  const std::array arguments{std::span<const std::byte>{address}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  runtime::LaunchRuntime runtime{execution_model::GridId{94},
                                 one_thread_shape()};
  const arith::context arithmetic;
  Simulator simulator{*program, runtime, common::FunctionId{0}, arithmetic,
                      *packed};
  const auto run = simulator.run(3);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::trapped);
  EXPECT_EQ(run->issued_groups, 2U);
  ASSERT_EQ(run->faults.size(), 1U);
  ASSERT_TRUE(std::holds_alternative<runtime::RuntimeBindingError>(
      run->faults.front().cause));
  EXPECT_EQ(std::get<runtime::RuntimeBindingError>(run->faults.front().cause),
            (runtime::RuntimeBindingError{
                runtime::RuntimeBindingErrorCode::missing_binding,
                runtime::RuntimeResourceKind::global}));
  EXPECT_TRUE(runtime.entry_parameter());
}

TEST(EntryArguments, PreservesAlignmentFaultsAndHighAddressBits) {
  const auto program = lower_program(kPointerLoad);
  ASSERT_TRUE(program);
  // A truncated high address would incorrectly read the initialized bytes at 8.
  const std::array cases{
      std::pair{std::uint64_t{1}, memory::MemoryErrorCode::Misaligned},
      std::pair{std::uint64_t{0x100000008ULL},
                memory::MemoryErrorCode::OutOfBounds}};
  for (const auto [address_value, expected_error] : cases) {
    SCOPED_TRACE(address_value);
    const auto address = little_endian(address_value);
    const std::array arguments{std::span<const std::byte>{address}};
    const auto packed =
        pack_entry_arguments(*program, common::FunctionId{0}, arguments);
    ASSERT_TRUE(packed);
    runtime::LaunchRuntime runtime{execution_model::GridId{95},
                                   one_thread_shape()};
    const auto global = runtime.address_spaces().create_global({16});
    ASSERT_TRUE(runtime.bind_global(global));
    auto global_view = runtime.address_spaces().view(global);
    ASSERT_TRUE(global_view);
    const std::vector<std::byte> initialized(16, std::byte{0xa5});
    ASSERT_TRUE(global_view->initialize(memory::Address{0}, initialized));
    const arith::context arithmetic;
    Simulator simulator{*program, runtime, common::FunctionId{0}, arithmetic,
                        *packed};
    const auto run = simulator.run(3);
    ASSERT_TRUE(run);
    EXPECT_EQ(run->termination, RunTermination::trapped);
    EXPECT_EQ(run->issued_groups, 2U);
    ASSERT_EQ(run->faults.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<memory::AddressSpaceError>(
        run->faults.front().cause));
    const auto& error =
        std::get<memory::AddressSpaceError>(run->faults.front().cause);
    ASSERT_TRUE(error.memory_error);
    EXPECT_EQ(error.memory_error->code, expected_error);
    EXPECT_EQ(error.memory_error->address, memory::Address{address_value});
  }
}

}  // namespace ptxsim::simulator::test
