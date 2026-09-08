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
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/common/shape.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {

/** @brief Parse, resolve, and lower a complete public PTX module. */
auto lower_program(std::string_view source)
    -> std::optional<exec_ir::ExecutableProgram> {
  ptx_frontend::PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast) {
    ADD_FAILURE() << "PTX parsing failed";
    return std::nullopt;
  }
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  if (!resolved) {
    ADD_FAILURE() << "PTX resolution failed";
    return std::nullopt;
  }
  auto program = exec_ir_lowering::lower(*resolved);
  if (!program) {
    ADD_FAILURE() << "PTX lowering failed: "
                  << static_cast<unsigned>(program.error().code);
    return std::nullopt;
  }
  return std::move(*program);
}

/** @brief Encode one unsigned ABI scalar as little-endian bytes. */
auto little_endian(std::uint64_t value) -> std::array<std::byte, 8> {
  std::array<std::byte, 8> result{};
  for (std::size_t index = 0; index != result.size(); ++index)
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(value >> (index * 8U)));
  return result;
}

/** @brief Write low-order bytes into a snapshot without host-endian assumptions. */
void write_bytes(std::vector<std::byte>& bytes, std::size_t offset,
                 std::uint64_t value, std::size_t width) {
  for (std::size_t index = 0; index != width; ++index)
    bytes[offset + index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(value >> (index * 8U)));
}

/** @brief Return the multidimensional launch shape used to exercise MOV topology. */
auto topology_shape() -> execution_model::GridShape {
  return {
      .cta_dim = {2, 2, 2},
      .thread_dim = {11, 3, 2},
      .warp_size = 32,
  };
}

/** @brief PTX that writes every scalar topology move into one thread-owned record. */
constexpr std::string_view kTopologyKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry topology(.param .u64 output) {
  .reg .u64 %output, %offset, %address;
  .reg .u32 %tid_x, %tid_y, %tid_z;
  .reg .u32 %ntid_x, %ntid_y, %ntid_z;
  .reg .u32 %ctaid_x, %ctaid_y, %ctaid_z;
  .reg .u32 %nctaid_x, %nctaid_y, %nctaid_z;
  .reg .u32 %lane, %local, %cta, %linear;
  .reg .u16 %tid_x16;

  ld.param.u64 %output, [output];
  mov.u32 %tid_x, %tid.x;
  mov.u16 %tid_x16, %tid.x;
  mov.u32 %tid_y, %tid.y;
  mov.u32 %tid_z, %tid.z;
  mov.u32 %ntid_x, %ntid.x;
  mov.u32 %ntid_y, %ntid.y;
  mov.u32 %ntid_z, %ntid.z;
  mov.u32 %ctaid_x, %ctaid.x;
  mov.u32 %ctaid_y, %ctaid.y;
  mov.u32 %ctaid_z, %ctaid.z;
  mov.u32 %nctaid_x, %nctaid.x;
  mov.u32 %nctaid_y, %nctaid.y;
  mov.u32 %nctaid_z, %nctaid.z;
  mov.u32 %lane, %laneid;

  mul.lo.u32 %local, %tid_z, %ntid_y;
  add.u32 %local, %local, %tid_y;
  mul.lo.u32 %local, %local, %ntid_x;
  add.u32 %local, %local, %tid_x;
  mul.lo.u32 %cta, %ctaid_z, %nctaid_y;
  add.u32 %cta, %cta, %ctaid_y;
  mul.lo.u32 %cta, %cta, %nctaid_x;
  add.u32 %cta, %cta, %ctaid_x;
  mul.lo.u32 %linear, %cta, 66;
  add.u32 %linear, %linear, %local;
  mul.wide.u32 %offset, %linear, 64;
  add.u64 %address, %output, %offset;

  st.global.u32 [%address+0], %tid_x;
  st.global.u32 [%address+4], %tid_y;
  st.global.u32 [%address+8], %tid_z;
  st.global.u32 [%address+12], %ntid_x;
  st.global.u32 [%address+16], %ntid_y;
  st.global.u32 [%address+20], %ntid_z;
  st.global.u32 [%address+24], %ctaid_x;
  st.global.u32 [%address+28], %ctaid_y;
  st.global.u32 [%address+32], %ctaid_z;
  st.global.u32 [%address+36], %nctaid_x;
  st.global.u32 [%address+40], %nctaid_y;
  st.global.u32 [%address+44], %nctaid_z;
  st.global.u32 [%address+48], %lane;
  st.global.u16 [%address+52], %tid_x16;
  exit;
}
)ptx";

/** @brief PTX that carries every implemented scalar MOV type from an immediate through a register. */
constexpr std::string_view kScalarBitCopyKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry scalar_bitcopy(.param .u64 output) {
  .reg .u64 %output;
  .reg .b16 %b16_source, %b16_destination;
  .reg .u16 %u16_source, %u16_destination;
  .reg .s16 %s16_source, %s16_destination;
  .reg .b32 %b32_source, %b32_destination;
  .reg .u32 %u32_source, %u32_destination, %marker;
  .reg .s32 %s32_source, %s32_destination;
  .reg .f32 %f32_source, %f32_destination;
  .reg .b64 %b64_source, %b64_destination;
  .reg .u64 %u64_source, %u64_destination;
  .reg .s64 %s64_source, %s64_destination;
  .reg .f64 %f64_source, %f64_destination;
  .reg .pred %p0, %p1, %p2;

  ld.param.u64 %output, [output];
  mov.b16 %b16_source, 0x1234;
  mov.b16 %b16_destination, %b16_source;
  mov.u16 %u16_source, 0x2345;
  mov.u16 %u16_destination, %u16_source;
  mov.s16 %s16_source, -1234;
  mov.s16 %s16_destination, %s16_source;
  mov.b32 %b32_source, 0x01234567;
  mov.b32 %b32_destination, %b32_source;
  mov.u32 %u32_source, 0x89abcdef;
  mov.u32 %u32_destination, %u32_source;
  mov.s32 %s32_source, -1234567;
  mov.s32 %s32_destination, %s32_source;
  mov.f32 %f32_source, 0f7fa12345;
  mov.f32 %f32_destination, %f32_source;
  mov.b64 %b64_source, 0x0123456789abcdef;
  mov.b64 %b64_destination, %b64_source;
  mov.u64 %u64_source, 0xfedcba9876543210;
  mov.u64 %u64_destination, %u64_source;
  mov.s64 %s64_source, -123456789;
  mov.s64 %s64_destination, %s64_source;
  mov.f64 %f64_source, 0d8000000000000000;
  mov.f64 %f64_destination, %f64_source;
  setp.lt.u32 %p0, 0, 1;
  mov.pred %p1, %p0;
  mov.pred %p2, %p1;
  mov.u32 %marker, 0;
  @%p2 mov.u32 %marker, 0xcafebabe;

  st.global.b16 [%output+0], %b16_destination;
  st.global.u16 [%output+2], %u16_destination;
  st.global.s16 [%output+4], %s16_destination;
  st.global.b32 [%output+8], %b32_destination;
  st.global.u32 [%output+12], %u32_destination;
  st.global.s32 [%output+16], %s32_destination;
  st.global.b32 [%output+20], %f32_destination;
  st.global.b64 [%output+24], %b64_destination;
  st.global.u64 [%output+32], %u64_destination;
  st.global.s64 [%output+40], %s64_destination;
  st.global.b64 [%output+48], %f64_destination;
  st.global.u32 [%output+56], %marker;
  exit;
}
)ptx";

/** @brief PTX that round-trips supported packed MOV containers through aliases and a sink. */
constexpr std::string_view kPackUnpackKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry pack_unpack(.param .u64 output) {
  .reg .u64 %output, %wide64_source, %wide64_destination;
  .reg .b128 %wide128_pair, %wide128_quad;
  .reg .b16 %half_low, %half_high;
  .reg .b32 %word, %word_low, %word_high, %sink_word;
  .reg .b32 %quad0, %quad1, %quad2, %quad3;

  ld.param.u64 %output, [output];
  mov.b16 %half_low, 0x1234;
  mov.b16 %half_high, 0xabcd;
  mov.b32 %word, {%half_low, %half_high};
  mov.b32 {%half_low, %half_high}, %word;

  mov.b32 %word_low, 0x01234567;
  mov.b32 %word_high, 0x89abcdef;
  mov.b64 %wide64_source, {%word_low, %word_high};
  mov.b64 {%word_low, %word_high}, %wide64_source;

  mov.b64 %wide64_source, 0x0123456789abcdef;
  mov.b64 %wide64_destination, 0xfedcba9876543210;
  mov.b128 %wide128_pair, {%wide64_source, %wide64_destination};
  mov.b128 {%wide64_source, %wide64_destination}, %wide128_pair;

  mov.b32 %quad0, 0x11111111;
  mov.b32 %quad1, 0x22222222;
  mov.b32 %quad2, 0x33333333;
  mov.b32 %quad3, 0x44444444;
  mov.b128 %wide128_quad, {%quad0, %quad1, %quad2, %quad3};
  mov.b128 {%quad0, %quad1, %quad2, %quad3}, %wide128_quad;
  mov.b64 {%sink_word, _}, %wide64_source;

  st.global.b16 [%output+0], %half_low;
  st.global.b16 [%output+2], %half_high;
  st.global.b32 [%output+4], %word_low;
  st.global.b32 [%output+8], %word_high;
  st.global.b64 [%output+16], %wide64_source;
  st.global.b64 [%output+24], %wide64_destination;
  st.global.b32 [%output+32], %quad0;
  st.global.b32 [%output+36], %quad1;
  st.global.b32 [%output+40], %quad2;
  st.global.b32 [%output+44], %quad3;
  st.global.b32 [%output+48], %sink_word;
  exit;
}
)ptx";

/** @brief PTX that packs remaining byte and halfword aggregate layouts from ABI input. */
constexpr std::string_view kRemainingPackKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry remaining_packs(.param .u64 output, .param .align 8 .b8 input[8]) {
  .reg .u64 %output, %packed64;
  .reg .u32 %packed32;
  .reg .u16 %packed16, %half0, %half1, %half2, %half3;
  .reg .u16 %unpacked_half0, %unpacked_half1, %unpacked_half2, %unpacked_half3;
  .reg .u8 %byte0, %byte1, %byte2, %byte3;
  .reg .u8 %unpacked16_0, %unpacked16_1;
  .reg .u8 %unpacked32_0, %unpacked32_1, %unpacked32_2, %unpacked32_3;

  ld.param.u64 %output, [output];
  ld.param.u8 %byte0, [input];
  ld.param.u8 %byte1, [input+1];
  ld.param.u8 %byte2, [input+2];
  ld.param.u8 %byte3, [input+3];
  mov.b16 %packed16, {%byte0, %byte1};
  mov.b16 {%unpacked16_0, %unpacked16_1}, %packed16;
  mov.b32 %packed32, {%byte0, %byte1, %byte2, %byte3};
  mov.b32 {%unpacked32_0, %unpacked32_1, %unpacked32_2, %unpacked32_3}, %packed32;
  ld.param.u16 %half0, [input];
  ld.param.u16 %half1, [input+2];
  ld.param.u16 %half2, [input+4];
  ld.param.u16 %half3, [input+6];
  mov.b64 %packed64, {%half0, %half1, %half2, %half3};
  mov.b64 {%unpacked_half0, %unpacked_half1, %unpacked_half2, %unpacked_half3}, %packed64;

  st.global.b16 [%output+0], %packed16;
  st.global.b32 [%output+4], %packed32;
  st.global.b64 [%output+8], %packed64;
  st.global.u8 [%output+16], %unpacked16_0;
  st.global.u8 [%output+17], %unpacked16_1;
  st.global.u8 [%output+18], %unpacked32_0;
  st.global.u8 [%output+19], %unpacked32_1;
  st.global.u8 [%output+20], %unpacked32_2;
  st.global.u8 [%output+21], %unpacked32_3;
  st.global.u16 [%output+24], %unpacked_half0;
  st.global.u16 [%output+26], %unpacked_half1;
  st.global.u16 [%output+28], %unpacked_half2;
  st.global.u16 [%output+30], %unpacked_half3;
  exit;
}
)ptx";

/** @brief PTX that reads launch identity and all fixed-width lane masks. */
constexpr std::string_view kGridAndLaneMaskKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry grid_and_lane_masks(.param .u64 output) {
  .reg .u64 %output, %address, %offset, %grid64;
  .reg .u32 %thread, %grid32, %warp, %eq, %le, %lt, %ge, %gt;
  .reg .u16 %grid16;

  ld.param.u64 %output, [output];
  mov.u32 %thread, %tid.x;
  mul.wide.u32 %offset, %thread, 40;
  add.u64 %address, %output, %offset;
  mov.b64 %grid64, %gridid;
  mov.u32 %grid32, %gridid;
  mov.u16 %grid16, %gridid;
  mov.b32 %warp, %warpid;
  mov.b32 %eq, %lanemask_eq;
  mov.b32 %le, %lanemask_le;
  mov.b32 %lt, %lanemask_lt;
  mov.b32 %ge, %lanemask_ge;
  mov.b32 %gt, %lanemask_gt;

  st.global.b64 [%address+0], %grid64;
  st.global.u32 [%address+8], %grid32;
  st.global.u16 [%address+12], %grid16;
  st.global.b32 [%address+16], %eq;
  st.global.b32 [%address+20], %le;
  st.global.b32 [%address+24], %lt;
  st.global.b32 [%address+28], %ge;
  st.global.b32 [%address+32], %gt;
  st.global.b32 [%address+36], %warp;
  exit;
}
)ptx";

/** @brief PTX that converts one entry parameter symbol into an address for offset loads. */
constexpr std::string_view kEntryParameterAddressKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry parameter_address(.param .u64 output, .param .align 8 .b8 input[8]) {
  .reg .u64 %output, %wide_address, %offset_address;
  .reg .u32 %narrow_address, %narrow_value, %wide_value, %offset_value;

  ld.param.u64 %output, [output];
  mov.u32 %narrow_address, input;
  ld.param.u32 %narrow_value, [%narrow_address];
  mov.u64 %wide_address, input;
  ld.param.u32 %wide_value, [%wide_address];
  mov.u64 %offset_address, input+4;
  ld.param.u32 %offset_value, [%offset_address];
  st.global.u32 [%output+0], %narrow_value;
  st.global.u32 [%output+4], %wide_value;
  st.global.u32 [%output+8], %offset_value;
  exit;
}
)ptx";

/** @brief PTX whose vector destinations expose topology's x/y/z/zero layout. */
constexpr std::string_view kVectorTopologyKernel = R"ptx(
.version 9.3
.target sm_100
.address_size 64

.entry vector_topology() {
  .reg .v4 .u32 %tid_values;
  .reg .v4 .u32 %ntid_values;
  .reg .v4 .u32 %ctaid_values;
  .reg .v4 .u32 %nctaid_values;

  mov.v4.u32 %tid_values, %tid;
  mov.v4.u32 %ntid_values, %ntid;
  mov.v4.u32 %ctaid_values, %ctaid;
  mov.v4.u32 %nctaid_values, %nctaid;
  exit;
}
)ptx";

}  // namespace

TEST(MovPipeline, WritesScalarTopologyForEveryThreadThroughPublicLaunch) {
  auto program = lower_program(kTopologyKernel);
  ASSERT_TRUE(program);

  const auto shape = topology_shape();
  constexpr std::size_t guard_bytes = 32;
  constexpr std::size_t record_bytes = 64;
  constexpr std::size_t field_count = 13;
  const auto output_bytes =
      static_cast<std::size_t>(shape.thread_count()) * record_bytes;
  runtime::LaunchRuntime runtime{execution_model::GridId{71}, shape};
  const auto global = runtime.address_spaces().create_global(
      {.capacity = guard_bytes + output_bytes + guard_bytes});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::vector<std::byte> sentinel(
      guard_bytes + output_bytes + guard_bytes, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));

  const auto output_argument = little_endian(guard_bytes);
  const std::array arguments{std::span<const std::byte>{output_argument}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);

  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(1024);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 1024U);

  auto expected = sentinel;
  const auto threads_per_cta = static_cast<std::size_t>(shape.thread_dim.x) *
                               shape.thread_dim.y * shape.thread_dim.z;
  for (std::uint32_t cta_z = 0; cta_z != shape.cta_dim.z; ++cta_z) {
    for (std::uint32_t cta_y = 0; cta_y != shape.cta_dim.y; ++cta_y) {
      for (std::uint32_t cta_x = 0; cta_x != shape.cta_dim.x; ++cta_x) {
        const auto cta_linear =
            static_cast<std::size_t>(cta_x) +
            static_cast<std::size_t>(shape.cta_dim.x) *
                (static_cast<std::size_t>(cta_y) +
                 static_cast<std::size_t>(shape.cta_dim.y) * cta_z);
        for (std::uint32_t tid_z = 0; tid_z != shape.thread_dim.z; ++tid_z) {
          for (std::uint32_t tid_y = 0; tid_y != shape.thread_dim.y; ++tid_y) {
            for (std::uint32_t tid_x = 0; tid_x != shape.thread_dim.x;
                 ++tid_x) {
              const auto local_linear =
                  static_cast<std::size_t>(tid_x) +
                  static_cast<std::size_t>(shape.thread_dim.x) *
                      (static_cast<std::size_t>(tid_y) +
                       static_cast<std::size_t>(shape.thread_dim.y) * tid_z);
              const auto record =
                  guard_bytes +
                  (cta_linear * threads_per_cta + local_linear) * record_bytes;
              const std::array<std::uint32_t, field_count> values{
                  tid_x,
                  tid_y,
                  tid_z,
                  shape.thread_dim.x,
                  shape.thread_dim.y,
                  shape.thread_dim.z,
                  cta_x,
                  cta_y,
                  cta_z,
                  shape.cta_dim.x,
                  shape.cta_dim.y,
                  shape.cta_dim.z,
                  static_cast<std::uint32_t>(local_linear % shape.warp_size),
              };
              for (std::size_t field = 0; field != values.size(); ++field)
                write_bytes(expected, record + field * sizeof(std::uint32_t),
                            values[field], sizeof(std::uint32_t));
              write_bytes(expected, record + 52, tid_x, sizeof(std::uint16_t));
            }
          }
        }
      }
    }
  }
  const auto snapshot = memory->snapshot(
      memory::Address{0}, guard_bytes + output_bytes + guard_bytes);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(*snapshot, expected);
}

TEST(MovPipeline, BitCopiesEveryScalarTypeAndPredicateWithoutRegisterSeeding) {
  auto program = lower_program(kScalarBitCopyKernel);
  ASSERT_TRUE(program);

  constexpr std::size_t guard_bytes = 16;
  constexpr std::size_t result_bytes = 64;
  runtime::LaunchRuntime runtime{
      execution_model::GridId{72},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global(
      {.capacity = guard_bytes + result_bytes + guard_bytes});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::vector<std::byte> sentinel(
      guard_bytes + result_bytes + guard_bytes, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));

  const auto output_argument = little_endian(guard_bytes);
  const std::array arguments{std::span<const std::byte>{output_argument}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(64);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 64U);

  auto expected = sentinel;
  const auto output = guard_bytes;
  write_bytes(expected, output + 0, 0x1234U, 2);
  write_bytes(expected, output + 2, 0x2345U, 2);
  write_bytes(expected, output + 4, 0xfb2eU, 2);
  write_bytes(expected, output + 8, 0x01234567U, 4);
  write_bytes(expected, output + 12, 0x89abcdefU, 4);
  write_bytes(expected, output + 16, 0xffed2979U, 4);
  write_bytes(expected, output + 20, 0x7fa12345U, 4);
  write_bytes(expected, output + 24, 0x0123456789abcdefULL, 8);
  write_bytes(expected, output + 32, 0xfedcba9876543210ULL, 8);
  write_bytes(expected, output + 40, 0xfffffffff8a432ebULL, 8);
  write_bytes(expected, output + 48, 0x8000000000000000ULL, 8);
  write_bytes(expected, output + 56, 0xcafebabeU, 4);
  const auto snapshot = memory->snapshot(
      memory::Address{0}, guard_bytes + result_bytes + guard_bytes);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(*snapshot, expected);
}

TEST(MovPipeline, PacksAndUnpacksBitContainersInLowElementFirstOrder) {
  auto program = lower_program(kPackUnpackKernel);
  ASSERT_TRUE(program);

  constexpr std::size_t guard_bytes = 16;
  constexpr std::size_t result_bytes = 64;
  runtime::LaunchRuntime runtime{
      execution_model::GridId{73},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global(
      {.capacity = guard_bytes + result_bytes + guard_bytes});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::vector<std::byte> sentinel(
      guard_bytes + result_bytes + guard_bytes, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));

  const auto output_argument = little_endian(guard_bytes);
  const std::array arguments{std::span<const std::byte>{output_argument}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(64);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 64U);

  auto expected = sentinel;
  const auto output = guard_bytes;
  write_bytes(expected, output + 0, 0x1234U, 2);
  write_bytes(expected, output + 2, 0xabcdU, 2);
  write_bytes(expected, output + 4, 0x01234567U, 4);
  write_bytes(expected, output + 8, 0x89abcdefU, 4);
  write_bytes(expected, output + 16, 0x0123456789abcdefULL, 8);
  write_bytes(expected, output + 24, 0xfedcba9876543210ULL, 8);
  write_bytes(expected, output + 32, 0x11111111U, 4);
  write_bytes(expected, output + 36, 0x22222222U, 4);
  write_bytes(expected, output + 40, 0x33333333U, 4);
  write_bytes(expected, output + 44, 0x44444444U, 4);
  write_bytes(expected, output + 48, 0x89abcdefU, 4);
  const auto snapshot = memory->snapshot(
      memory::Address{0}, guard_bytes + result_bytes + guard_bytes);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(*snapshot, expected);
}

TEST(MovPipeline, PacksRemainingByteAndHalfwordLayoutsFromPublicAbiInput) {
  auto program = lower_program(kRemainingPackKernel);
  ASSERT_TRUE(program);

  constexpr std::size_t guard_bytes = 16;
  constexpr std::size_t result_bytes = 32;
  runtime::LaunchRuntime runtime{
      execution_model::GridId{76},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global(
      {.capacity = guard_bytes + result_bytes + guard_bytes});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::vector<std::byte> sentinel(
      guard_bytes + result_bytes + guard_bytes, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));

  const auto output_argument = little_endian(guard_bytes);
  const std::array<std::byte, 8> input_argument{
      std::byte{0xf1}, std::byte{0x80}, std::byte{0x34}, std::byte{0xfe},
      std::byte{0x67}, std::byte{0xc5}, std::byte{0xab}, std::byte{0xfe}};
  const std::array arguments{std::span<const std::byte>{output_argument},
                             std::span<const std::byte>{input_argument}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(128);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 128U);

  auto expected = sentinel;
  write_bytes(expected, guard_bytes + 0, 0x80f1U, 2);
  write_bytes(expected, guard_bytes + 4, 0xfe3480f1U, 4);
  write_bytes(expected, guard_bytes + 8, 0xfeabc567fe3480f1ULL, 8);
  write_bytes(expected, guard_bytes + 16, 0xf1U, 1);
  write_bytes(expected, guard_bytes + 17, 0x80U, 1);
  write_bytes(expected, guard_bytes + 18, 0xf1U, 1);
  write_bytes(expected, guard_bytes + 19, 0x80U, 1);
  write_bytes(expected, guard_bytes + 20, 0x34U, 1);
  write_bytes(expected, guard_bytes + 21, 0xfeU, 1);
  write_bytes(expected, guard_bytes + 24, 0x80f1U, 2);
  write_bytes(expected, guard_bytes + 26, 0xfe34U, 2);
  write_bytes(expected, guard_bytes + 28, 0xc567U, 2);
  write_bytes(expected, guard_bytes + 30, 0xfeabU, 2);
  const auto snapshot = memory->snapshot(
      memory::Address{0}, guard_bytes + result_bytes + guard_bytes);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(*snapshot, expected);
}

TEST(MovPipeline, ReadsGridIdentityAndFixedWidthLaneMasksThroughPublicLaunch) {
  auto program = lower_program(kGridAndLaneMaskKernel);
  ASSERT_TRUE(program);

  constexpr std::uint64_t grid_id = 0x123456789abcdef0ULL;
  constexpr std::size_t guard_bytes = 16;
  constexpr std::size_t record_bytes = 40;
  constexpr std::uint32_t thread_count = 64;
  runtime::LaunchRuntime runtime{execution_model::GridId{grid_id},
                                 {.cta_dim = {1, 1, 1},
                                  .thread_dim = {thread_count, 1, 1},
                                  .warp_size = 32}};
  const auto global = runtime.address_spaces().create_global(
      {.capacity = guard_bytes + record_bytes * thread_count + guard_bytes});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::vector<std::byte> sentinel(
      guard_bytes + record_bytes * thread_count + guard_bytes, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));

  const auto output_argument = little_endian(guard_bytes);
  const std::array arguments{std::span<const std::byte>{output_argument}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(256);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 256U);

  auto expected = sentinel;
  for (std::uint32_t thread = 0; thread != thread_count; ++thread) {
    const auto output =
        guard_bytes + static_cast<std::size_t>(thread) * record_bytes;
    const auto lane = thread % 32U;
    const auto lane_bit = std::uint32_t{1} << lane;
    const auto lower_bits = lane_bit - 1U;
    write_bytes(expected, output + 0, grid_id, 8);
    write_bytes(expected, output + 8, grid_id, 4);
    write_bytes(expected, output + 12, grid_id, 2);
    write_bytes(expected, output + 16, lane_bit, 4);
    write_bytes(expected, output + 20, lane_bit | lower_bits, 4);
    write_bytes(expected, output + 24, lower_bits, 4);
    write_bytes(expected, output + 28, ~lower_bits, 4);
    write_bytes(expected, output + 32, ~(lane_bit | lower_bits), 4);
    write_bytes(expected, output + 36, thread / 32U, 4);
  }
  const auto snapshot =
      memory->snapshot(memory::Address{0},
                       guard_bytes + record_bytes * thread_count + guard_bytes);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(*snapshot, expected);
}

TEST(MovPipeline,
     MovesEntryParameterAddressAndLoadsOffsetThroughPublicArguments) {
  auto program = lower_program(kEntryParameterAddressKernel);
  ASSERT_TRUE(program);

  constexpr std::size_t guard_bytes = 16;
  constexpr std::size_t result_bytes = 12;
  runtime::LaunchRuntime runtime{
      execution_model::GridId{75},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global(
      {.capacity = guard_bytes + result_bytes + guard_bytes});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const std::vector<std::byte> sentinel(
      guard_bytes + result_bytes + guard_bytes, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));

  const auto output_argument = little_endian(guard_bytes);
  const auto input_argument = little_endian(0x1122334455667788ULL);
  const std::array arguments{std::span<const std::byte>{output_argument},
                             std::span<const std::byte>{input_argument}};
  const auto packed =
      pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(64);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 64U);

  auto expected = sentinel;
  write_bytes(expected, guard_bytes + 0, 0x55667788U, 4);
  write_bytes(expected, guard_bytes + 4, 0x55667788U, 4);
  write_bytes(expected, guard_bytes + 8, 0x11223344U, 4);
  const auto snapshot = memory->snapshot(
      memory::Address{0}, guard_bytes + result_bytes + guard_bytes);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(*snapshot, expected);
}

TEST(MovPipeline, WritesVectorTopologyRegistersInXyzZeroOrder) {
  auto program = lower_program(kVectorTopologyKernel);
  ASSERT_TRUE(program);

  runtime::LaunchRuntime runtime{execution_model::GridId{74}, topology_shape()};
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic};
  const auto run = runner.run(160);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  EXPECT_LE(run->issued_groups, 160U);

  const auto& thread = runtime.grid()
                           .cta(execution_model::CtaId{runtime.grid().id(), 7})
                           .warp(2)
                           .thread(execution_model::LaneId{1});
  const auto frame = runtime.register_frame(thread.id(), common::FunctionId{0});
  ASSERT_TRUE(frame);
  const auto registers = runtime.registers().view(*frame);
  ASSERT_TRUE(registers);
  const std::array<std::array<std::uint32_t, 4>, 4> expected{
      std::array{10U, 2U, 1U, 0U},
      std::array{11U, 3U, 2U, 0U},
      std::array{1U, 1U, 1U, 0U},
      std::array{2U, 2U, 2U, 0U},
  };
  for (std::size_t vector = 0; vector != expected.size(); ++vector) {
    for (std::size_t component = 0; component != expected[vector].size();
         ++component) {
      const auto value =
          registers->read(common::RegisterSlot{static_cast<std::uint32_t>(
              vector * expected[vector].size() + component)});
      ASSERT_TRUE(value);
      EXPECT_EQ(*value, common::RawValue::b32(expected[vector][component]));
    }
  }
}

}  // namespace ptxsim::simulator::test
