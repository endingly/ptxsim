#pragma once

#include <expected>
#include <optional>

#include <ptxsim/exec_ir/exec_ir.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Return whether a scalar movement type has a raw register encoding. */
[[nodiscard]] auto valid_movement_type(exec_ir::DataType type) noexcept -> bool;

/** @brief Return whether a scalar source's MOV type is structurally compatible. */
[[nodiscard]] auto valid_scalar_move_source(const exec_ir::MovSource& source,
                                            exec_ir::DataType type) noexcept
    -> bool;

/** @brief Return whether a projected bit-vector pack or unpack is structurally legal. */
[[nodiscard]] auto valid_aggregate_move(const exec_ir::RegisterVector& registers,
                                        exec_ir::DataType type,
                                        bool source_vector) noexcept -> bool;

/** @brief Prepare one raw scalar copy after generated structural validation. */
auto prepare_scalar_move(LaneResourceResolver& resolver, exec_ir::DataType type,
                         common::RegisterSlot destination,
                         const exec_ir::MovSource& source,
                         std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare a low-element-first aggregate register packing operation. */
auto prepare_pack_move(LaneResourceResolver& resolver, exec_ir::DataType type,
                       common::RegisterSlot destination,
                       const exec_ir::RegisterVector& source,
                       std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare a low-element-first aggregate register unpacking operation. */
auto prepare_unpack_move(LaneResourceResolver& resolver, exec_ir::DataType type,
                         const exec_ir::RegisterVector& destination,
                         common::RegisterSlot source,
                         std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare one four-component topology-special-register copy. */
auto prepare_vector_special_move(
    LaneResourceResolver& resolver, exec_ir::VectorArity vector,
    exec_ir::DataType type, exec_ir::VectorRegisterRef destination,
    exec_ir::VectorSpecialRegisterRef source,
    std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare one predicate register copy after generated validation. */
auto prepare_predicate_move(LaneResourceResolver& resolver,
                            exec_ir::Predicate destination,
                            const exec_ir::PredicateSource& source,
                            std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

}  // namespace ptxsim::inst_execute_engine::detail
