#pragma once

#include <expected>
#include <vector>

#include <ptxsim/execution_model/warp.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>

#include "prepared_effect.hpp"

namespace ptxsim::inst_execute_engine::detail {

/**
 * @brief Commit a prepared all-or-nothing warp rendezvous arrival.
 *
 * @p prepared must be nonempty and contain one successful warp-sync effect per
 * issued lane.
 */
auto commit_warp_sync(execution_model::Warp& warp,
                      const execution_model::WarpIssueGroup& issue,
                      const std::vector<PreparedLane>& prepared)
    -> std::expected<void, StepError>;

/** @brief Commit scalar effects and trap the report's preparation and commit faults. */
void commit_scalar(execution_model::Warp& warp,
                   std::vector<PreparedLane>& prepared, StepReport& report);

/** @brief Trap every lane named in a completed execution report. */
void trap_faulted_lanes(execution_model::Warp& warp, const StepReport& report);

}  // namespace ptxsim::inst_execute_engine::detail
