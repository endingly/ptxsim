#include <ptxsim/lowering/diagnostic.hpp>

#include <charconv>
#include <limits>
#include <string_view>

namespace ptxsim::lowering {
namespace {

void append_number(std::string& output, std::uint32_t value) {
  char digits[std::numeric_limits<std::uint32_t>::digits10 + 1];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), value);
  (void)error;
  output.append(digits, end);
}

void append_escaped(std::string& output, std::string_view value) {
  output.push_back('"');
  constexpr char kHex[] = "0123456789abcdef";
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20 || character == 0x7fU) {
          output += "\\x";
          output.push_back(kHex[character >> 4]);
          output.push_back(kHex[character & 0x0f]);
        } else {
          output.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  output.push_back('"');
}

void append_field(std::string& output, std::string_view name,
                  const std::optional<std::string>& value) {
  if (!value) {
    return;
  }
  output.push_back(' ');
  output += name;
  output.push_back('=');
  append_escaped(output, *value);
}

}  // namespace

auto to_string(LoweringDiagnosticCode code) -> std::string {
  switch (code) {
    case LoweringDiagnosticCode::frontend_invalid_input:
      return "frontend_invalid_input";
    case LoweringDiagnosticCode::unsupported_ptx_feature:
      return "unsupported_ptx_feature";
    case LoweringDiagnosticCode::unsupported_ptx_variant:
      return "unsupported_ptx_variant";
    case LoweringDiagnosticCode::unsupported_type_combination:
      return "unsupported_type_combination";
    case LoweringDiagnosticCode::lowering_invariant_violation:
      return "lowering_invariant_violation";
    case LoweringDiagnosticCode::malformed_resolved_ir:
      return "malformed_resolved_ir";
    case LoweringDiagnosticCode::internal_lowering_error:
      return "internal_lowering_error";
  }
  return "invalid_lowering_diagnostic_code";
}

auto to_string(const LoweringDiagnostic& diagnostic) -> std::string {
  std::string output = "code=" + to_string(diagnostic.code);
  if (diagnostic.source_location) {
    output += " source=";
    append_escaped(output, diagnostic.source_location->file);
    output.push_back(':');
    append_number(output, diagnostic.source_location->line);
    output.push_back(':');
    append_number(output, diagnostic.source_location->column);
  }
  append_field(output, "function", diagnostic.function_context);
  append_field(output, "instruction", diagnostic.instruction_context);
  append_field(output, "unsupported-feature", diagnostic.unsupported_feature);
  append_field(output, "operand-or-control",
               diagnostic.operand_or_control_detail);
  return output;
}

}  // namespace ptxsim::lowering
