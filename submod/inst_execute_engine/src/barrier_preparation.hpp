#pragma once

#include <expected>
#include <optional>

#include <ptxsim/exec_ir/exec_ir.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Prepare the membership mask used by one warp rendezvous. */
auto prepare_bar_warp_sync(LaneResourceResolver& resolver,
                           const exec_ir::ScalarOperand& membermask,
                           common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/**
 * @brief Prepare one lane's CTA barrier arrival without mutating CTA state.
 *
 * The generated adapter supplies the form-derived protocol and optional
 * operands. Dynamic barrier IDs and counts remain in the returned effect so
 * collective commit can reject disagreement before recording an arrival.
 */
auto prepare_cta_barrier(
    LaneResourceResolver& resolver,
    execution_model::CtaBarrierProtocol protocol, bool waits,
    const exec_ir::ScalarOperand& barrier,
    std::optional<exec_ir::ScalarOperand> thread_count,
    std::optional<exec_ir::Predicate> reduction_input,
    std::optional<common::RegisterSlot> reduction_destination,
    common::ProgramCounter successor) -> std::expected<PreparedEffect, LaneFaultCause>;

}  // namespace ptxsim::inst_execute_engine::detail
