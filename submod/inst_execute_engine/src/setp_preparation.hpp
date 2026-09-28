#pragma once

#include <expected>
#include <optional>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Prepare an integer predicate comparison and its optional sink writes. */
[[nodiscard]] auto prepare_integer_setp(
    LaneResourceResolver& resolver, const arith::context& arithmetic,
    exec_ir::DataType type, exec_ir::ComparisonOperator comparison,
    std::optional<exec_ir::BooleanOperator> boolean,
    const exec_ir::PredicateSource* combine,
    const exec_ir::SetpDestination& destination,
    const exec_ir::ScalarOperand& lhs, const exec_ir::ScalarOperand& rhs,
    std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

}  // namespace ptxsim::inst_execute_engine::detail
