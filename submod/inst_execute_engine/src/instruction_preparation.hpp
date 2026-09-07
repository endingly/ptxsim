#pragma once

#include <expected>
#include <optional>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/inst_execute_engine/step_outcome.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Selects whether a prepared instruction commits lane-wise or together. */
enum class PrepareKind { scalar, warp_sync };

/** @brief Result of selecting an implemented instruction form. */
struct SelectedPreparer {
  /** Preparation function for one issued lane. */
  std::expected<PreparedEffect, LaneFaultCause> (*handler)(
      LaneResourceResolver&, const arith::context&, const exec_ir::Instruction&,
      std::optional<common::ProgramCounter>);
  /** Commit path required by the selected instruction form. */
  PrepareKind kind;
};

/** @brief Select the implemented preparation handler for one instruction form. */
auto select_preparer(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode>;

/**
 * @brief Prepare a lane, including its execution predicate, without mutation.
 *
 * The caller supplies a selected, supported form and the successor required by
 * any fallthrough or predicated control effect.
 */
auto prepare_lane(LaneResourceResolver& resolver,
                  const SelectedPreparer& preparer,
                  const arith::context& arithmetic,
                  const exec_ir::Instruction& instruction,
                  std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

}  // namespace ptxsim::inst_execute_engine::detail
