#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/execution_model/warp_state.hpp>
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

/** @brief Observable result of one attempted simulator issue step. */
enum class StepTermination {
  /** @brief One issue committed and simulation remains runnable. */
  issued,
  /** @brief All threads have exited, optionally after the reported issue. */
  completed,
  /** @brief A lane trapped, optionally during the reported issue. */
  trapped,
  /** @brief Live threads exist but none is ready to issue. */
  deadlocked,
};

/** @brief Result of one simulator step, including the issue that produced it. */
struct StepReport {
  /** @brief Terminal or progress state observed by the step. */
  StepTermination termination;
  /** @brief Issued same-PC lane group, absent when no instruction was issued. */
  std::optional<execution_model::WarpIssueGroup> issue;
  /** @brief Lane-local causes from a faulting @ref issue, if any. */
  std::vector<inst_execute_engine::LaneFault> faults{};
};

/** @brief Result of a run that reached an architectural or resource bound. */
struct RunReport {
  /** @brief Terminal condition observed after the last issued group. */
  RunTermination termination;
  /** @brief Number of same-PC lane groups successfully issued by this run. */
  std::size_t issued_groups;
  /**
   * @brief Lane-local causes from the faulting issue that ended this run.
   *
   * Empty when no issue fault caused @ref termination, including a runtime
   * that was already trapped when the runner began.
   */
  std::vector<inst_execute_engine::LaneFault> faults{};

  constexpr bool operator==(const RunReport&) const = default;
};

/**
 * @brief Own one executable program and drive it on one borrowed runtime warp.
 *
 * The program is immutable after construction. The runtime and arithmetic
 * context remain borrowed and must outlive this simulator.
 */
class Simulator final {
 public:
  /**
   * @brief Take ownership of a validated program and borrow its execution state.
   */
  Simulator(exec_ir::ExecutableProgram program, runtime::LaunchRuntime& runtime,
            common::FunctionId entry_function,
            const arith::context& arithmetic) noexcept;

  /**
   * @brief Execute at most one deterministic same-PC issue group.
   *
   * Missing entry-function frames are provisioned only on the first call.
   */
  [[nodiscard]] auto step() -> std::expected<StepReport, RunError>;

  /**
   * @brief Execute up to @p maximum_issued_groups issue groups.
   *
   * The returned count covers only groups issued by this call. A faulting
   * issue is counted and its lane-local causes are retained in the report.
   */
  [[nodiscard]] auto run(std::size_t maximum_issued_groups)
      -> std::expected<RunReport, RunError>;

 private:
  /** @brief Initialize the one-warp runtime binding state exactly once. */
  [[nodiscard]] auto initialize() -> std::expected<void, RunError>;

  /** @brief Immutable instruction storage owned for this simulator lifetime. */
  exec_ir::ExecutableProgram program_;
  /** @brief Borrowed launch state mutated by issued instruction effects. */
  runtime::LaunchRuntime& runtime_;
  /** @brief Entry function executed by every thread in the sole warp. */
  common::FunctionId entry_function_;
  /** @brief Borrowed arithmetic semantics used by the instruction engine. */
  const arith::context& arithmetic_;
  /** @brief Whether entry-frame provisioning has completed successfully. */
  bool initialized_ = false;
  /** @brief Cached initialization failure, preventing a later retry/mutation. */
  std::optional<RunError> initialization_error_;
};

}  // namespace ptxsim::simulator
