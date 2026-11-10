#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include <ptxsim/exec_ir/exec_ir.hpp>

namespace ptx_frontend::resolved_ir {
struct ResolvedModule;
}

namespace ptxsim::exec_ir_lowering {

/** @brief Cold source/owned-graph failure, independent of execution support. */
enum class LoweringErrorCode : std::uint8_t {
  malformed_resolved_ir,
  program_validation_failed,
};

/** @brief Structured source location or owned-program structural failure. */
struct LoweringError {
  /** @brief Rejected boundary operation. */
  LoweringErrorCode code;
  /** @brief Source declaration index; not an identity retained by runtime IR. */
  std::optional<std::uint32_t> function;
  /** @brief Source function-local instruction index. */
  std::optional<std::uint32_t> instruction;
  /** @brief Source symbol index used only for boundary diagnostics. */
  std::optional<std::uint32_t> symbol;
  /** @brief Structured owned-graph error when freezing rejected the bound graph. */
  std::optional<exec_ir::ProgramError> program_error;

  /** @brief Compare error identity and typed source/owned context. */
  constexpr bool operator==(const LoweringError&) const noexcept = default;
};

/** @brief Fully bind a checked frontend module into an independent immutable snapshot. */
[[nodiscard]] auto lower(
    const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<exec_ir::ExecutableProgram, LoweringError>;

}  // namespace ptxsim::exec_ir_lowering
