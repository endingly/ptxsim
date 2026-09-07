#pragma once

#include <expected>
#include <optional>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/execution_model/warp.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine {

/** @brief Executes one validated PTX instruction issue against a warp. */
class InstExecuteEngine final {
 public:
  /** @brief Bind execution to one launch runtime and function register layout. */
  InstExecuteEngine(runtime::LaunchRuntime& runtime,
                    common::FunctionId function,
                    const arith::context& arithmetic) noexcept;

  /** @brief Prepare and commit an issue, returning rejections or lane faults. */
  [[nodiscard]] auto execute(execution_model::Warp& warp,
                             const execution_model::WarpIssueGroup& issue,
                             const exec_ir::Instruction& instruction,
                             std::optional<common::ProgramCounter> successor)
      -> std::expected<StepReport, StepError>;

 private:
  /** Launch state borrowed for the lifetime of this engine. */
  runtime::LaunchRuntime& runtime_;
  /** Function identity selecting each thread's register frame. */
  common::FunctionId function_;
  /** Arithmetic configuration borrowed for instruction preparation. */
  const arith::context& arithmetic_;
};

}  // namespace ptxsim::inst_execute_engine
