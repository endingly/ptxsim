#include "issue_validation.hpp"

#include <cstdint>

namespace ptxsim::inst_execute_engine::detail {

auto step_error(StepErrorCode code, std::optional<execution_model::LaneId> lane)
    -> std::unexpected<StepError> {
  return std::unexpected(StepError{code, lane});
}

auto validate_issue(const runtime::LaunchRuntime& runtime,
                    const execution_model::Warp& warp,
                    const execution_model::WarpIssueGroup& issue)
    -> std::expected<void, StepError> {
  const auto* runtime_warp = runtime.grid().find_warp(warp.id());
  if (runtime_warp != &warp) {
    return step_error(StepErrorCode::foreign_warp);
  }
  if (issue.lanes.size() != warp.architectural_warp_size()) {
    return step_error(StepErrorCode::lane_mask_width);
  }
  if (issue.empty()) {
    return step_error(StepErrorCode::empty_issue);
  }
  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) {
      continue;
    }
    if (!warp.valid_mask().test(lane)) {
      return step_error(StepErrorCode::invalid_lane, lane);
    }
    const auto& thread = warp.thread(lane);
    if (!thread.ready()) {
      return step_error(StepErrorCode::lane_not_ready, lane);
    }
    if (thread.pc() != issue.pc) {
      return step_error(StepErrorCode::pc_mismatch, lane);
    }
  }
  return {};
}

}  // namespace ptxsim::inst_execute_engine::detail
