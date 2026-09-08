#pragma once

#include <expected>
#include <optional>

#include <ptxsim/execution_model/warp.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine::detail {

/** @brief Construct a lane-specific or issue-wide execution rejection. */
auto step_error(StepErrorCode code,
                std::optional<execution_model::LaneId> lane = std::nullopt)
    -> std::unexpected<StepError>;

/** @brief Validate ownership and readiness before selecting an instruction. */
auto validate_issue(const runtime::LaunchRuntime& runtime,
                    const execution_model::Warp& warp,
                    const execution_model::WarpIssueGroup& issue)
    -> std::expected<void, StepError>;

}  // namespace ptxsim::inst_execute_engine::detail
