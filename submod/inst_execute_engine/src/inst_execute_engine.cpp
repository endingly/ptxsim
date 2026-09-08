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
  if (const auto valid = detail::validate_issue(runtime_, warp, issue);
      !valid) {
    return std::unexpected(valid.error());
  }

  const auto preparer = detail::select_preparer(instruction);
  if (!preparer) {
    return detail::step_error(preparer.error());
  }
  if (exec_ir::may_fallthrough(instruction) && !successor) {
    return detail::step_error(StepErrorCode::missing_fallthrough);
  }
  StepReport report;
  std::vector<detail::PreparedLane> prepared;
  prepared.reserve(issue.size());
  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) {
      continue;
    }
    auto& thread = warp.thread(lane);
    detail::LaneResourceResolver resolver(runtime_, thread, function_);
    const auto effect = detail::prepare_lane(resolver, *preparer, arithmetic_,
                                             instruction, successor);
    if (!effect) {
      report.faults.push_back({lane, effect.error()});
      continue;
    }
    prepared.push_back({&thread, *effect});
  }

  if (preparer->kind == detail::PrepareKind::warp_sync ||
      preparer->kind == detail::PrepareKind::cta_barrier) {
    if (!report.faults.empty()) {
      detail::trap_faulted_lanes(warp, report);
      return report;
    }
    const auto committed = preparer->kind == detail::PrepareKind::warp_sync
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
