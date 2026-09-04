#pragma once

#include <cstddef>
#include <expected>
#include <optional>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::simulator {

/** @brief Categories of failure before the runner reaches a terminal state. */
enum class RunErrorCode {
  requires_one_warp,
  program_error,
  register_error,
  runtime_binding_error,
  execution_error,
};

/** @brief Structured reason the runner could not perform an issue step. */
struct RunError {
  /** @brief Stable category identifying the failed runner precondition or step. */
  RunErrorCode code;
  /** @brief Program lookup or fallthrough failure when @ref code is program_error. */
  std::optional<exec_ir::ProgramError> program_error;
  /** @brief Register-frame creation or validation failure when @ref code is register_error. */
  std::optional<memory::RegisterError> register_error;
  /** @brief Runtime binding failure when @ref code is runtime_binding_error. */
  std::optional<runtime::RuntimeBindingError> runtime_binding_error;
  /** @brief Engine rejection when @ref code is execution_error. */
  std::optional<inst_execute_engine::StepError> execution_error;

  constexpr bool operator==(const RunError&) const noexcept = default;
};

/** @brief Observable terminal state of a bounded simulation run. */
enum class RunTermination {
  completed,
  trapped,
  deadlocked,
  step_limit_exhausted,
};

/** @brief Result of a run that reached an architectural or resource bound. */
struct RunReport {
  /** @brief Terminal condition observed after the last issued group. */
  RunTermination termination;
  /** @brief Number of same-PC lane groups successfully issued by this run. */
  std::size_t issued_groups;

  constexpr bool operator==(const RunReport&) const noexcept = default;
};

/**
 * @brief Execute one entry function on a runtime containing exactly one warp.
 *
 * Ready lanes are grouped by their current program counter before issue, which
 * preserves divergent control flow without introducing a scheduler policy.
 * Missing entry-function register frames are provisioned from the validated
 * program layout. Callers still supply all other runtime resources and may
 * pre-bind register frames when injecting initial architectural state. The
 * finite issue budget bounds loops in the input program.
 */
[[nodiscard]] auto run_to_completion(runtime::LaunchRuntime& runtime,
                                     const exec_ir::ExecutableProgram& program,
                                     common::FunctionId entry_function,
                                     const arith::context& arithmetic,
                                     std::size_t maximum_issued_groups)
    -> std::expected<RunReport, RunError>;

}  // namespace ptxsim::simulator
