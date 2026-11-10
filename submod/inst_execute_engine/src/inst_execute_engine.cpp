#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include "commit.hpp"
#include "instruction_preparation.hpp"
#include "issue_validation.hpp"
#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine {

InstExecuteEngine::InstExecuteEngine(runtime::LaunchRuntime& runtime,
                                     common::FunctionId function,
                                     const arith::context& arithmetic) noexcept
    : runtime_(runtime), function_(function), arithmetic_(arithmetic) {}

auto InstExecuteEngine::execute(execution_model::Warp& warp,
                                const execution_model::WarpIssueGroup& issue,
                                const exec_ir::Instruction& instruction,
                                std::optional<common::ProgramCounter> successor)
    -> std::expected<StepReport, StepError> {
  auto result = execute_with_context(warp, issue, instruction, SuccessorContext{successor});
  if (!result) return std::unexpected(std::get<StepError>(result.error()));
  return std::move(*result);
}

auto InstExecuteEngine::execute_with_context(execution_model::Warp& warp,
                                            const execution_model::WarpIssueGroup& issue,
                                            const exec_ir::Instruction& instruction,
                                            SuccessorContext successor)
    -> std::expected<StepReport, ExecutionError> {
  if (const auto valid = detail::validate_issue(runtime_, warp, issue);
      !valid) {
    return std::unexpected(valid.error());
  }

  auto group = detail::prepare_issue(runtime_, function_, arithmetic_, warp, issue,
                                      instruction, successor);
  if (!group) return std::unexpected(group.error());
  auto& report = group->report;
  auto& prepared = group->lanes;

  if (group->kind == detail::PrepareKind::warp_sync ||
      group->kind == detail::PrepareKind::cta_barrier) {
    if (!report.faults.empty()) {
      detail::trap_faulted_lanes(warp, report);
      return report;
    }
    const auto committed = group->kind == detail::PrepareKind::warp_sync
                               ? detail::commit_warp_sync(warp, issue, prepared)
                               : commit_cta_barrier(warp, issue, prepared, report);
    if (!committed) {
      return std::unexpected(committed.error());
    }
    return report;
  }

  detail::commit_scalar(warp, prepared, report);
  reconcile_exited_barriers(warp, report);
  return report;
}

}  // namespace ptxsim::inst_execute_engine
