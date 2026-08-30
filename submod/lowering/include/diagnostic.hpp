#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ptxsim::lowering {

enum class LoweringDiagnosticCode {
  frontend_invalid_input,
  unsupported_ptx_feature,
  unsupported_ptx_variant,
  unsupported_type_combination,
  lowering_invariant_violation,
  malformed_resolved_ir,
  internal_lowering_error,
};

struct LoweringSourceLocation {
  std::string file;
  std::uint32_t line;
  std::uint32_t column;

  constexpr bool operator==(const LoweringSourceLocation&) const noexcept =
      default;
};

struct LoweringDiagnostic {
  LoweringDiagnosticCode code;
  std::optional<LoweringSourceLocation> source_location;
  std::optional<std::string> function_context;
  std::optional<std::string> instruction_context;
  std::optional<std::string> unsupported_feature;
  std::optional<std::string> operand_or_control_detail;

  constexpr bool operator==(const LoweringDiagnostic&) const noexcept = default;
};

[[nodiscard]] auto to_string(LoweringDiagnosticCode code) -> std::string;
[[nodiscard]] auto to_string(const LoweringDiagnostic& diagnostic)
    -> std::string;

}  // namespace ptxsim::lowering
