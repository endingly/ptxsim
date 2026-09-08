#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
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

using common::RawValue;

/** @brief Real PTX spelling and independently encoded inputs and expected output. */
struct FmaCase {
  /** @brief Canonical instruction modifiers, excluding operands and semicolon. */
  std::string instruction;
  /** @brief Multiplicands and accumulator, in instruction operand order. */
  std::array<RawValue, 3> inputs;
  /** @brief Expected result container and bits, independent of arith execution. */
  RawValue expected;
};

/** @brief Show the exact instruction under test in parameterized diagnostics. */
void PrintTo(const FmaCase& value, std::ostream* output) {
  *output << value.instruction;
}

/** @brief Select a bit type usable for both a register and an ABI parameter. */
auto bit_type(const RawValue& value) -> std::string {
  switch (value.width()) {
    case common::RawWidth::b16: return ".b16";
    case common::RawWidth::b32: return ".b32";
    case common::RawWidth::b64: return ".b64";
    default: return ".invalid";
  }
}

/** @brief Encode a raw scalar/packed container as explicit little-endian bytes. */
auto argument_bytes(const RawValue& value) -> std::vector<std::byte> {
  std::uint64_t bits = 0;
  std::size_t size = 0;
  switch (value.width()) {
    case common::RawWidth::b16: bits = *value.as_b16(); size = 2; break;
    case common::RawWidth::b32: bits = *value.as_b32(); size = 4; break;
    case common::RawWidth::b64: bits = *value.as_b64(); size = 8; break;
    default: return {};
  }
  std::vector<std::byte> result(size);
  for (std::size_t index = 0; index < size; ++index)
    result[index] = static_cast<std::byte>((bits >> (8U * index)) & 0xffU);
  return result;
}

/**
 * @brief Run FMA through public argument binding and global output readback.
 *
 * All input registers are initialized by PTX loads. Output sentinels detect
 * writes beyond the result; aliases and immediate substitutions use this same
 * production path without register-frame seeding.
 */
void expect_fma(const FmaCase& item, std::string_view destination = "%d",
                std::array<std::string_view, 3> sources = {"%a", "%b", "%c"}) {
  const std::array names{"a", "b", "c"};
  std::string parameters = ".param .u64 output";
  std::string declarations = ".reg .u64 %output;\n.reg " +
                             bit_type(item.expected) + " %d;\n";
  std::string loads = "ld.param.u64 %output, [output];\n";
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto type = bit_type(item.inputs[index]);
    parameters += ", .param " + type + " " + names[index];
    declarations += ".reg " + type + " %" + names[index] + ";\n";
    loads += "ld.param" + type + " %" + names[index] + ", [" +
             names[index] + "];\n";
  }
  const std::string text = ".version 9.3\n.target sm_100\n.address_size 64\n"
      ".entry kernel(" + parameters + ") {\n" + declarations + loads +
      item.instruction + " " + std::string(destination) + ", " +
      std::string(sources[0]) + ", " + std::string(sources[1]) + ", " +
      std::string(sources[2]) + ";\nst.global" + bit_type(item.expected) +
      " [%output], " + std::string(destination) + ";\nexit;\n}\n";
  SCOPED_TRACE(text);
  ptx_frontend::PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast) << "PTX parsing failed";
  const auto resolved = ptx_frontend::resolved_ir::resolveModule(*ast);
  ASSERT_TRUE(resolved) << "PTX resolution failed";
  auto program = exec_ir_lowering::lower(*resolved);
  ASSERT_TRUE(program) << "PTX lowering failed";
  const auto instruction = program->fetch({common::FunctionId{0},
                                           common::ProgramCounter{4}});
  ASSERT_TRUE(instruction);
  ASSERT_TRUE(std::holds_alternative<exec_ir::Fma>(instruction->get()));
  const std::array bytes{
      argument_bytes(RawValue::b64(std::uint64_t{8})),
      argument_bytes(item.inputs[0]), argument_bytes(item.inputs[1]),
      argument_bytes(item.inputs[2])};
  const std::array arguments{
      std::span<const std::byte>{bytes[0]}, std::span<const std::byte>{bytes[1]},
      std::span<const std::byte>{bytes[2]}, std::span<const std::byte>{bytes[3]}};
  auto packed = pack_entry_arguments(*program, common::FunctionId{0}, arguments);
  ASSERT_TRUE(packed);
  runtime::LaunchRuntime runtime{
      execution_model::GridId{50},
      {.cta_dim = {1, 1, 1}, .thread_dim = {1, 1, 1}, .warp_size = 1}};
  const auto global = runtime.address_spaces().create_global({24});
  ASSERT_TRUE(runtime.bind_global(global));
  auto memory = runtime.address_spaces().view(global);
  ASSERT_TRUE(memory);
  const auto sentinel = std::vector<std::byte>(24, std::byte{0xa5});
  ASSERT_TRUE(memory->initialize(memory::Address{0}, sentinel));
  const arith::context arithmetic;
  Simulator runner{std::move(*program), runtime, common::FunctionId{0},
                   arithmetic, std::move(*packed)};
  const auto run = runner.run(7);
  ASSERT_TRUE(run);
  ASSERT_EQ(run->termination, RunTermination::completed);
  EXPECT_EQ(run->issued_groups, 7);
  const auto snapshot = memory->snapshot(memory::Address{0}, 24);
  ASSERT_TRUE(snapshot);
  auto expected = std::vector<std::byte>(24, std::byte{0xa5});
  const auto output = argument_bytes(item.expected);
  std::ranges::copy(output, expected.begin() + 8);
  EXPECT_EQ(*snapshot, expected);
}

/** @brief Use exact 1/2 * 1/2 + 1/4 = 1/2 for each legal control combination. */
auto ordinary_case(std::string instruction, std::string_view type) -> FmaCase {
  if (type == "f64")
    return {std::move(instruction),
            {RawValue::b64(std::uint64_t{0x3fe0000000000000}),
             RawValue::b64(std::uint64_t{0x3fe0000000000000}),
             RawValue::b64(std::uint64_t{0x3fd0000000000000})},
            RawValue::b64(std::uint64_t{0x3fe0000000000000})};
  if (type == "f32x2")
    return {std::move(instruction),
            {RawValue::b64(std::uint64_t{0x3f0000003f000000}),
             RawValue::b64(std::uint64_t{0x3f0000003f000000}),
             RawValue::b64(std::uint64_t{0x3e8000003e800000})},
            RawValue::b64(std::uint64_t{0x3f0000003f000000})};
  if (type == "f16" || type == "bf16" || type == "f32.f16" ||
      type == "f32.bf16") {
    const bool bfloat = type.ends_with("bf16");
    const auto half = RawValue::b16(std::uint16_t(bfloat ? 0x3f00 : 0x3800));
    const auto quarter = RawValue::b16(std::uint16_t(bfloat ? 0x3e80 : 0x3400));
    const bool mixed = type.starts_with("f32.");
    return {std::move(instruction),
            {half, half, mixed ? RawValue::b32(0x3e800000U) : quarter},
            mixed ? RawValue::b32(0x3f000000U) : half};
  }
  if (type == "f16x2" || type == "bf16x2") {
    const bool bfloat = type == "bf16x2";
    const auto half = RawValue::b32(bfloat ? 0x3f003f00U : 0x38003800U);
    const auto quarter = RawValue::b32(bfloat ? 0x3e803e80U : 0x34003400U);
    return {std::move(instruction), {half, half, quarter}, half};
  }
  return {std::move(instruction),
          {RawValue::b32(0x3f000000U), RawValue::b32(0x3f000000U),
           RawValue::b32(0x3e800000U)}, RawValue::b32(0x3f000000U)};
}

/** @brief Independently enumerate the PTX 9.3 contract's 70 canonical spellings. */
auto canonical_cases() -> std::vector<FmaCase> {
  std::vector<FmaCase> cases;
  const std::array<std::string, 4> rounding{"rn", "rz", "rm", "rp"};
  const std::array<std::string, 2> ftz{"", ".ftz"};
  const std::array<std::string, 2> sat{"", ".sat"};
  const std::array<std::string, 2> relu{"", ".relu"};
  for (const auto& rnd : rounding) {
    for (const auto& flush : ftz) {
      for (const auto& saturate : sat)
        cases.push_back(ordinary_case("fma." + rnd + flush + saturate + ".f32", "f32"));
      cases.push_back(ordinary_case("fma." + rnd + flush + ".f32x2", "f32x2"));
    }
    cases.push_back(ordinary_case("fma." + rnd + ".f64", "f64"));
  }
  for (const std::string type : {"f16", "f16x2"}) {
    for (const auto& flush : ftz) {
      for (const auto& saturate : sat)
        cases.push_back(ordinary_case("fma.rn" + flush + saturate + "." + type, type));
      cases.push_back(ordinary_case("fma.rn" + flush + ".relu." + type, type));
    }
    for (const auto& saturate : sat)
      cases.push_back(ordinary_case("fma.rn.oob" + saturate + "." + type, type));
    cases.push_back(ordinary_case("fma.rn.oob.relu." + type, type));
  }
  for (const std::string type : {"bf16", "bf16x2"}) {
    for (const auto& activation : relu) {
      cases.push_back(ordinary_case("fma.rn" + activation + "." + type, type));
      cases.push_back(ordinary_case("fma.rn.oob" + activation + "." + type, type));
    }
  }
  for (const std::string type : {"f32.f16", "f32.bf16"})
    for (const auto& rnd : rounding)
      for (const auto& saturate : sat)
        cases.push_back(ordinary_case("fma." + rnd + saturate + "." + type, type));
  return cases;
}

/** @brief All legal modifier combinations use the real public launch path. */
class FmaPipeline : public ::testing::TestWithParam<FmaCase> {};

TEST_P(FmaPipeline, ExecutesThroughPublicArgumentBinding) {
  expect_fma(GetParam());
}

INSTANTIATE_TEST_SUITE_P(CompleteFmaContract, FmaPipeline,
                         ::testing::ValuesIn(canonical_cases()));

TEST(FmaPipelineContract, HasEveryCanonicalModifierCombination) {
  EXPECT_EQ(canonical_cases().size(), 70);
  EXPECT_EQ(std::variant_size_v<exec_ir::Fma::Variant>, 16);
}

TEST(FmaPipelineContract, ReadsAllSourcesBeforeAliasedDestinationWriteback) {
  const FmaCase scalar{"fma.rn.f32",
      {RawValue::b32(0x3f800000U), RawValue::b32(0x40000000U),
       RawValue::b32(0x40400000U)}, RawValue::b32(0x40a00000U)};
  for (const auto destination : {"%a", "%b", "%c"})
    expect_fma(scalar, destination);
  expect_fma(ordinary_case("fma.rn.f32.f16", "f32.f16"), "%c");
}

TEST(FmaPipelineContract, PreservesFloatingImmediatesAtEachLegalPosition) {
  const auto scalar = ordinary_case("fma.rn.f32", "f32");
  expect_fma(scalar, "%d", {"0d3fe0000000000000", "%b", "%c"});
  expect_fma(scalar, "%d", {"%a", "0f3f000000", "%c"});
  expect_fma(scalar, "%d", {"%a", "%b", "0.25"});
  expect_fma(ordinary_case("fma.rp.f64", "f64"), "%d",
             {"0f3f000000", "0.5", "0d3fd0000000000000"});
  expect_fma(ordinary_case("fma.rm.f32.bf16", "f32.bf16"), "%d",
             {"%a", "%b", "0.25"});
}

TEST(FmaPipelineContract, PreservesSingleRoundingAcrossCancellation) {
  // (1 + 2^-p) * (1 - 2^-p) - 1 = -2^(-2p), without product rounding.
  expect_fma({"fma.rn.f32",
      {RawValue::b32(0x3f800001U), RawValue::b32(0x3f7ffffeU),
       RawValue::b32(0xbf800000U)}, RawValue::b32(0xa8800000U)});
  expect_fma({"fma.rn.f64",
      {RawValue::b64(std::uint64_t{0x3ff0000000000001}),
       RawValue::b64(std::uint64_t{0x3feffffffffffffe}),
       RawValue::b64(std::uint64_t{0xbff0000000000000})},
       RawValue::b64(std::uint64_t{0xb970000000000000})});
  expect_fma({"fma.rn.f16",
      {RawValue::b16(std::uint16_t{0x3c01}), RawValue::b16(std::uint16_t{0x3bfe}),
       RawValue::b16(std::uint16_t{0xbc00})}, RawValue::b16(std::uint16_t{0x8010})});
  expect_fma({"fma.rn.bf16",
      {RawValue::b16(std::uint16_t{0x3f81}), RawValue::b16(std::uint16_t{0x3f7e}),
       RawValue::b16(std::uint16_t{0xbf80})}, RawValue::b16(std::uint16_t{0xb880})});
  expect_fma({"fma.rn.f32.f16",
      {RawValue::b16(std::uint16_t{0x3c01}), RawValue::b16(std::uint16_t{0x3bfe}),
       RawValue::b32(0xbf800000U)}, RawValue::b32(0xb5800000U)});
  expect_fma({"fma.rn.f32.bf16",
      {RawValue::b16(std::uint16_t{0x3f81}), RawValue::b16(std::uint16_t{0x3f7e}),
       RawValue::b32(0xbf800000U)}, RawValue::b32(0xb8800000U)});
}

TEST(FmaPipelineContract, PreservesDirectedRoundingAndSubnormalControls) {
  for (const std::string rounding : {"rn", "rz", "rm", "rp"}) {
    const auto expected = rounding == "rp" ? 0x3f800001U : 0x3f800000U;
    expect_fma({"fma." + rounding + ".f32",
        {RawValue::b32(0x3f800000U), RawValue::b32(0x3f800000U),
         RawValue::b32(0x33800000U)}, RawValue::b32(expected)});
    expect_fma({"fma." + rounding + ".f32.f16",
        {RawValue::b16(std::uint16_t{0x3c00}), RawValue::b16(std::uint16_t{0x3c00}),
         RawValue::b32(0x33800000U)}, RawValue::b32(expected)});
  }
  const std::array f32_inputs{RawValue::b32(0x00800000U),
      RawValue::b32(0x3f000000U), RawValue::b32(0U)};
  expect_fma({"fma.rn.f32", f32_inputs, RawValue::b32(0x00400000U)});
  expect_fma({"fma.rn.ftz.f32", f32_inputs, RawValue::b32(0U)});
  const std::array half_inputs{RawValue::b16(std::uint16_t{0x0400}),
      RawValue::b16(std::uint16_t{0x3800}), RawValue::b16(std::uint16_t{0})};
  expect_fma({"fma.rn.f16", half_inputs, RawValue::b16(std::uint16_t{0x0200})});
  expect_fma({"fma.rn.ftz.f16", half_inputs, RawValue::b16(std::uint16_t{0})});
}

TEST(FmaPipelineContract, AppliesOobAndActivationIndependentlyPerPackedLane) {
  // The low lane has the OOB marker; the high lane computes 1*2+1=3.
  expect_fma({"fma.rn.oob.f16x2",
      {RawValue::b32(0x3c007ff7U), RawValue::b32(0x40003c00U),
       RawValue::b32(0x3c003c00U)}, RawValue::b32(0x42000000U)});
  expect_fma({"fma.rn.oob.bf16x2",
      {RawValue::b32(0x3f803f80U), RawValue::b32(0x40007ff7U),
       RawValue::b32(0x3f803f80U)}, RawValue::b32(0x40400000U)});
  expect_fma({"fma.rn.oob.relu.f16x2",
      {RawValue::b32(0xbc003c00U), RawValue::b32(0x40003c00U),
       RawValue::b32(0x3c007ff7U)}, RawValue::b32(0x00007fffU)});
  // An ordinary NaN stays NaN while the adjacent OOB lane becomes zero.
  expect_fma({"fma.rn.oob.relu.f16x2",
      {RawValue::b32(0x7ff77e01U), RawValue::b32(0x3c003c00U),
       RawValue::b32(0x3c003c00U)}, RawValue::b32(0x00007fffU)});
  expect_fma({"fma.rn.oob.relu.bf16x2",
      {RawValue::b32(0x3f803f80U), RawValue::b32(0x7ff77fc1U),
       RawValue::b32(0x3f803f80U)}, RawValue::b32(0x00007fffU)});
  // Exact positive-marker matching leaves negative 0xfff7 as an ordinary NaN.
  expect_fma({"fma.rn.oob.relu.bf16x2",
      {RawValue::b32(0xfff73f80U), RawValue::b32(0x3f803f80U),
       RawValue::b32(0x3f807ff7U)}, RawValue::b32(0x7fff7fffU)});
  expect_fma({"fma.rn.relu.bf16x2",
      {RawValue::b32(0xbf807fc1U), RawValue::b32(0x40003f80U),
       RawValue::b32(0x3f803f80U)}, RawValue::b32(0x00007fffU)});
  expect_fma({"fma.rn.sat.f16x2",
      {RawValue::b32(0x3c007e01U), RawValue::b32(0x40003c00U),
       RawValue::b32(0x3c003c00U)}, RawValue::b32(0x3c000000U)});
  // Packed f32 lanes have different signs/results and may not share writeback bits.
  expect_fma({"fma.rn.f32x2",
      {RawValue::b64(std::uint64_t{0xbf8000003f800000}),
       RawValue::b64(std::uint64_t{0x4000000040000000}),
       RawValue::b64(std::uint64_t{0x3f8000003f800000})},
       RawValue::b64(std::uint64_t{0xbf80000040400000})});
}

TEST(FmaPipelineContract, PreservesSignedZerosInfinityAndSaturation) {
  for (const std::string rounding : {"rn", "rz", "rm", "rp"}) {
    // Exact cancellation rounds to -0 only toward negative infinity.
    expect_fma({"fma." + rounding + ".f32",
        {RawValue::b32(0x3f800000U), RawValue::b32(0x3f800000U),
         RawValue::b32(0xbf800000U)},
        RawValue::b32(rounding == "rm" ? 0x80000000U : 0U)});
    // A negative halfway result distinguishes rm from the other modes.
    expect_fma({"fma." + rounding + ".f64",
        {RawValue::b64(std::uint64_t{0xbff0000000000000}),
         RawValue::b64(std::uint64_t{0x3ff0000000000000}),
         RawValue::b64(std::uint64_t{0xbca0000000000000})},
        RawValue::b64(rounding == "rm" ? std::uint64_t{0xbff0000000000001}
                                       : std::uint64_t{0xbff0000000000000})});
  }
  expect_fma({"fma.rn.f32",
      {RawValue::b32(0x80000000U), RawValue::b32(0x3f800000U),
       RawValue::b32(0x80000000U)}, RawValue::b32(0x80000000U)});
  for (const auto type : {"f16", "bf16"}) {
    const auto one = RawValue::b16(std::uint16_t(type == std::string_view{"f16"}
                                                  ? 0x3c00 : 0x3f80));
    expect_fma({"fma.rn.sat.f32." + std::string(type),
        {one, one, RawValue::b32(0x7f800000U)}, RawValue::b32(0x3f800000U)});
    expect_fma({"fma.rn.sat.f32." + std::string(type),
        {one, one, RawValue::b32(0x7fc00001U)}, RawValue::b32(0U)});
  }
  expect_fma({"fma.rn.sat.f32",
      {RawValue::b32(0x7f800000U), RawValue::b32(0U),
       RawValue::b32(0x3f800000U)}, RawValue::b32(0U)});
}

}  // namespace
}  // namespace ptxsim::simulator::test
