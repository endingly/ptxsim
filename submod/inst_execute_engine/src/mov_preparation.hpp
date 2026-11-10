#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include <ptxsim/exec_ir/exec_ir.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Return whether a movement type has an implemented raw copy codec. */
[[nodiscard]] auto valid_movement_type(exec_ir::DataType type) noexcept -> bool;

/** @brief Validate a scalar intrinsic's type against owned metadata and layout-local compatibility facts. */
[[nodiscard]] auto valid_scalar_move_shape(const exec_ir::MovSource& source,
    exec_ir::DataType type,const exec_ir::FormContract& contract,std::uint16_t layout) noexcept -> bool;

/** @brief Validate aggregate bit-container, byte-lane arity and source-sink structure. */
[[nodiscard]] auto valid_aggregate_move_shape(const exec_ir::RegisterVector& registers,
    exec_ir::DataType type,bool source_vector) noexcept -> bool;

/** @brief Return whether a structurally valid scalar source has implemented MOV semantics. */
[[nodiscard]] auto valid_scalar_move_source(const exec_ir::MovSource& source,
                                            exec_ir::DataType type) noexcept
    -> bool;

/** @brief Return whether a structurally valid aggregate has implemented source/destination support. */
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
    exec_ir::DataType type, const exec_ir::RegisterVector& destination,
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
