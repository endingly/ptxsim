#pragma once

#include <expected>
#include <optional>
#include <vector>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Selects whether a prepared instruction commits lane-wise or together. */
enum class PrepareKind { scalar, warp_sync, cta_barrier };

/** @brief Transient completed issue preparation, before any architectural commit. */
struct PreparedIssue {
  /** @brief Scalar versus collective commit protocol selected by the typed group. */
  PrepareKind kind;
  /** @brief All lane faults observed while preparing this issue. */
  StepReport report;
  /** @brief Successful lane effects, retained until every issued lane is prepared. */
  std::vector<PreparedLane> lanes;
};

/** @brief Decode one dense Kind and invoke one typed issue-group entry. */
[[nodiscard]] auto prepare_issue(runtime::LaunchRuntime& runtime,
                                 common::FunctionId function,
                                 const arith::context& arithmetic,
                                 execution_model::Warp& warp,
                                 const execution_model::WarpIssueGroup& issue,
                                 const exec_ir::Instruction& instruction,
                                 SuccessorContext successor)
    -> std::expected<PreparedIssue, ExecutionError>;

}  // namespace ptxsim::inst_execute_engine::detail
