#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>
#include <ptxsim/simulator/simulator.hpp>

namespace ptxsim::simulator::test {
namespace {

/** @brief One PTX memory transfer with independently specified register bits. */
struct MemoryPipelineCase {
  /** PTX instruction type, separate from the register storage width. */
  std::string_view type;
  /** Number of vector elements; one selects the scalar form. */
  unsigned lanes;
  /** Register width in bits; wider integer storage tests extension/truncation. */
  unsigned register_bits;
  /** Stored low-bit encoding and expected extended load result. */
  std::uint64_t source, expected;
  /** Bytes per memory element, excluding register extension. */
  unsigned element_bytes;
  /** Empty for generic addressing or an explicit state-space suffix. */
  std::string_view space = ".global";
  /** PTX pointer register width, independent of transferred data. */
  unsigned address_bits = 64;
};

/** @brief Identify the data width and vector shape of a failed transfer. */
void PrintTo(const MemoryPipelineCase& item, std::ostream* output) {
  *output << item.space << '.' << item.type << " lanes=" << item.lanes
          << " register_bits=" << item.register_bits;
}

/** @brief Encode test register bits without floating-point conversions. */
auto register_value(unsigned bits, std::uint64_t value) -> common::RawValue {
  return bits == 32 ? common::RawValue::b32(static_cast<std::uint32_t>(value))
                    : common::RawValue::b64(value);
}

/** @brief Test memory through the real parse/resolve/lower/simulate pipeline. */
class MemoryPipeline : public ::testing::TestWithParam<MemoryPipelineCase> {};

TEST_P(MemoryPipeline, StoresLowBitsAndLoadsExtendedValues) {
  const auto& item = GetParam();
  std::string declarations =
      ".reg .b" + std::to_string(item.address_bits) + " %addr;\n";
  for (const auto prefix : {"s", "d"}) {
    for (unsigned index = 0; index < item.lanes; ++index)
      declarations += ".reg .b" + std::to_string(item.register_bits) + " %" +
                      prefix + std::to_string(index) + ";\n";
  }
  std::string sources, destinations;
  for (unsigned index = 0; index < item.lanes; ++index) {
    if (index != 0) {
      sources += ", ";
      destinations += ", ";
    }
    sources += "%s" + std::to_string(index);
    destinations += "%d" + std::to_string(index);
  }
  if (item.lanes != 1) {
    sources = "{" + sources + "}";
    destinations = "{" + destinations + "}";
  }
  const auto suffix =
      std::string(item.space) +
      (item.lanes == 1 ? "" : ".v" + std::to_string(item.lanes)) + "." +
      std::string(item.type);
  const std::string text = ".entry kernel() {\n" + declarations + "st" +
                           suffix + " [%addr-16], " + sources + ";\n" + "ld" +
                           suffix + " " + destinations + ", [32];\nexit;\n}";
  SCOPED_TRACE(text);
  ptx_frontend::PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved);
  auto program = exec_ir_lowering::lower(*resolved);
  ASSERT_TRUE(program) << static_cast<unsigned>(program.error().code);

  runtime::LaunchRuntime runtime{
      execution_model::GridId{51},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({64});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const arith::context arithmetic;
  Simulator simulator{std::move(*program), runtime, common::FunctionId{0},
                      arithmetic};
  ASSERT_TRUE(simulator.run(0));
  const auto& thread = runtime.grid()
                           .cta(execution_model::CtaId{runtime.grid().id(), 0})
                           .warp(0)
                           .thread(execution_model::LaneId{0});
  if (item.space == ".shared") {
    const auto shared = runtime.address_spaces().create_shared({64});
    ASSERT_TRUE(runtime.bind_shared(
        execution_model::CtaId{runtime.grid().id(), 0}, shared));
    memory = runtime.address_spaces().view(shared);
  } else if (item.space == ".local") {
    const auto local = runtime.address_spaces().create_local_frame({64});
    ASSERT_TRUE(
        runtime.bind_local_frame(thread.id(), common::FunctionId{0}, local));
    memory = runtime.address_spaces().view(local);
  }
  ASSERT_TRUE(memory);
  const auto frame = runtime.register_frame(thread.id(), common::FunctionId{0});
  ASSERT_TRUE(frame);
  auto registers = runtime.registers().view(*frame);
  ASSERT_TRUE(registers);
  ASSERT_TRUE(registers->write(common::RegisterSlot{0},
                               register_value(item.address_bits, 48)));
  for (unsigned index = 0; index < item.lanes; ++index) {
    ASSERT_TRUE(registers->write(
        common::RegisterSlot{1 + index},
        register_value(item.register_bits, item.source + index)));
  }
  const auto run = simulator.run(3);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->termination, RunTermination::completed);
  for (unsigned index = 0; index < item.lanes; ++index) {
    const auto value =
        registers->read(common::RegisterSlot{1 + item.lanes + index});
    ASSERT_TRUE(value);
    EXPECT_EQ(*value,
              register_value(item.register_bits, item.expected + index));
  }
  const auto stored =
      memory->snapshot(memory::Address{32}, item.lanes * item.element_bytes);
  ASSERT_TRUE(stored);
  for (unsigned index = 0; index < item.lanes; ++index) {
    for (unsigned byte = 0; byte < item.element_bytes; ++byte)
      EXPECT_EQ((*stored)[index * item.element_bytes + byte],
                std::byte{static_cast<std::uint8_t>((item.source + index) >>
                                                    (8 * byte))});
  }
}

INSTANTIATE_TEST_SUITE_P(
    OrdinaryMemoryForms, MemoryPipeline,
    ::testing::Values(
        MemoryPipelineCase{"b8", 1, 32, 0xabcdef98, 0x98, 1},
        MemoryPipelineCase{"u8", 1, 32, 0xabcdef98, 0x98, 1},
        MemoryPipelineCase{"s8", 1, 32, 0xabcdef98, 0xffffff98, 1},
        MemoryPipelineCase{"b16", 1, 32, 0xabcdef98, 0xef98, 2},
        MemoryPipelineCase{"u16", 1, 32, 0xabcdef98, 0xef98, 2},
        MemoryPipelineCase{"s16", 1, 32, 0xabcdef98, 0xffffef98, 2},
        MemoryPipelineCase{"b32", 1, 64, 0x11223344abcdef98, 0xabcdef98, 4},
        MemoryPipelineCase{"u32", 1, 64, 0x11223344abcdef98, 0xabcdef98, 4},
        MemoryPipelineCase{"s32", 1, 64, 0x11223344abcdef98, 0xffffffffabcdef98,
                           4},
        MemoryPipelineCase{"b64", 1, 64, 0x11223344abcdef98, 0x11223344abcdef98,
                           8},
        MemoryPipelineCase{"u64", 1, 64, 0x11223344abcdef98, 0x11223344abcdef98,
                           8},
        MemoryPipelineCase{"s64", 1, 64, 0x88776655abcdef98, 0x88776655abcdef98,
                           8},
        MemoryPipelineCase{"f32", 1, 32, 0x7fc00042, 0x7fc00042, 4},
        MemoryPipelineCase{"f64", 1, 64, 0x8000000000000000, 0x8000000000000000,
                           8},
        MemoryPipelineCase{"s8", 2, 32, 0xabcdef98, 0xffffff98, 1},
        MemoryPipelineCase{"u16", 4, 32, 0xabcdef98, 0xef98, 2},
        MemoryPipelineCase{"s32", 4, 32, 0xabcdef98, 0xabcdef98, 4},
        MemoryPipelineCase{"b32", 8, 32, 0xabcdef98, 0xabcdef98, 4},
        MemoryPipelineCase{"u64", 4, 64, 0x11223344abcdef98, 0x11223344abcdef98,
                           8},
        MemoryPipelineCase{"f32", 2, 32, 0x7fc00042, 0x7fc00042, 4},
        MemoryPipelineCase{"f64", 2, 64, 0x8000000000000000, 0x8000000000000000,
                           8},
        MemoryPipelineCase{"u8", 1, 32, 0xabcdef98, 0x98, 1, ""},
        MemoryPipelineCase{"s16", 2, 32, 0xabcdef98, 0xffffef98, 2, ""},
        MemoryPipelineCase{"b32", 8, 32, 0xabcdef98, 0xabcdef98, 4, ""},
        MemoryPipelineCase{"u64", 4, 64, 0x11223344abcdef98, 0x11223344abcdef98,
                           8, ""},
        MemoryPipelineCase{"u32", 1, 32, 0xabcdef98, 0xabcdef98, 4, ".global",
                           32},
        MemoryPipelineCase{"u16", 4, 32, 0xabcdef98, 0xef98, 2, ".shared", 32},
        MemoryPipelineCase{"s8", 1, 32, 0xabcdef98, 0xffffff98, 1, ".local",
                           32}));

}  // namespace
}  // namespace ptxsim::simulator::test
