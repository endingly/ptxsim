#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include <ptxsim/lowering/diagnostic.hpp>

namespace ptxsim::lowering::test {
namespace {

TEST(LoweringDiagnostic, NamesEveryCategory) {
  constexpr std::array cases{
      std::pair{LoweringDiagnosticCode::frontend_invalid_input,
                "frontend_invalid_input"},
      std::pair{LoweringDiagnosticCode::unsupported_ptx_feature,
                "unsupported_ptx_feature"},
      std::pair{LoweringDiagnosticCode::unsupported_ptx_variant,
                "unsupported_ptx_variant"},
      std::pair{LoweringDiagnosticCode::unsupported_type_combination,
                "unsupported_type_combination"},
      std::pair{LoweringDiagnosticCode::lowering_invariant_violation,
                "lowering_invariant_violation"},
      std::pair{LoweringDiagnosticCode::malformed_resolved_ir,
                "malformed_resolved_ir"},
      std::pair{LoweringDiagnosticCode::internal_lowering_error,
                "internal_lowering_error"},
  };

  for (const auto& [code, name] : cases) {
    EXPECT_EQ(to_string(code), name);
  }
  EXPECT_EQ(to_string(static_cast<LoweringDiagnosticCode>(99)),
            "invalid_lowering_diagnostic_code");
}

TEST(LoweringDiagnostic, FormatsOptionalFieldsDeterministically) {
  const LoweringDiagnostic diagnostic{
      .code = LoweringDiagnosticCode::unsupported_ptx_feature,
      .source_location = LoweringSourceLocation{"input.ptx", 7, 3},
      .function_context = "kernel",
      .instruction_context = "ld.global",
      .unsupported_feature = "vector load",
      .operand_or_control_detail = "v4.b32",
  };

  const auto expected =
      "code=unsupported_ptx_feature source=\"input.ptx\":7:3 "
      "function=\"kernel\" instruction=\"ld.global\" "
      "unsupported-feature=\"vector load\" operand-or-control=\"v4.b32\"";
  EXPECT_EQ(to_string(diagnostic), expected);
  EXPECT_EQ(to_string(diagnostic), expected);
}

TEST(LoweringDiagnostic, EscapesTextAndCopiesItsInputs) {
  std::string file = "input\\\n.ptx";
  std::string function = "kernel\tname";
  std::string instruction = "mov\r\"b32";
  std::string feature{static_cast<char>(1)};
  feature += "feature";
  feature.push_back(static_cast<char>(0x7fU));
  std::string detail = "r\\0";
  const LoweringDiagnostic diagnostic{
      .code = LoweringDiagnosticCode::unsupported_ptx_variant,
      .source_location =
          LoweringSourceLocation{file, std::uint32_t{9}, std::uint32_t{4}},
      .function_context = function,
      .instruction_context = instruction,
      .unsupported_feature = feature,
      .operand_or_control_detail = detail,
  };
  file.assign("changed");
  function.assign("changed");
  instruction.assign("changed");
  feature.assign("changed");
  detail.assign("changed");

  EXPECT_EQ(to_string(diagnostic),
            "code=unsupported_ptx_variant source=\"input\\\\\\n.ptx\":9:4 "
            "function=\"kernel\\tname\" instruction=\"mov\\r\\\"b32\" "
            "unsupported-feature=\"\\x01feature\\x7f\" "
            "operand-or-control=\"r\\\\0\"");
}

TEST(LoweringDiagnostic, OmitsAbsentOptionalFields) {
  EXPECT_EQ(to_string({.code = LoweringDiagnosticCode::malformed_resolved_ir}),
            "code=malformed_resolved_ir");
}

}  // namespace
}  // namespace ptxsim::lowering::test
