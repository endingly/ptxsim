#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptx_frontend::resolved_ir {
struct ResolvedModule;
}

namespace ptxsim::exec_ir_lowering {

enum class LoweringErrorCode : std::uint8_t {
  malformed_resolved_ir,
  unsupported_instruction,
  unsupported_form,
  unsupported_type,
  unsupported_operand,
  invalid_branch_target,
  program_validation_failed,
};

struct LoweringError {
  LoweringErrorCode code;
  std::optional<std::uint32_t> function;
  std::optional<std::uint32_t> instruction;
  std::optional<std::uint32_t> symbol;
  std::optional<exec_ir::ProgramError> program_error;

  constexpr bool operator==(const LoweringError&) const noexcept = default;
};

[[nodiscard]] auto lower(
    const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<exec_ir::ExecutableProgram, LoweringError>;

}  // namespace ptxsim::exec_ir_lowering
