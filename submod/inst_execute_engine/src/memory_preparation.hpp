#pragma once

#include <expected>
#include <optional>

#include <ptxsim/exec_ir/exec_ir.hpp>

#include "prepared_effect.hpp"
#include "resource_resolution.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Check type, arity, and statically selected state-space legality. */
[[nodiscard]] auto valid_memory_shape(
    exec_ir::DataType type, std::optional<exec_ir::VectorArity> vector,
    exec_ir::AddressSpace space) noexcept -> bool;

/** @brief Check vector element count and sink legality without reading registers. */
[[nodiscard]] auto valid_memory_vector_shape(
    exec_ir::DataType type, exec_ir::VectorArity vector,
    exec_ir::AddressSpace space, const exec_ir::RegisterVector& registers,
    bool load) noexcept -> bool;

/** @brief Prepare one ordinary scalar load without changing registers. */
auto prepare_memory_load(LaneResourceResolver& resolver,
                         const exec_ir::Address& address,
                         common::RegisterSlot destination,
                         exec_ir::DataType type, exec_ir::AddressSpace space,
                         std::optional<exec_ir::VectorArity> vector,
                         common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare one ordinary vector load without changing registers. */
auto prepare_memory_load(LaneResourceResolver& resolver,
                         const exec_ir::Address& address,
                         const exec_ir::RegisterVector& destination,
                         exec_ir::DataType type, exec_ir::AddressSpace space,
                         std::optional<exec_ir::VectorArity> vector,
                         common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare one ordinary scalar store without changing memory. */
auto prepare_memory_store(LaneResourceResolver& resolver,
                          const exec_ir::Address& address,
                          common::RegisterSlot source, exec_ir::DataType type,
                          exec_ir::AddressSpace space,
                          std::optional<exec_ir::VectorArity> vector,
                          common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

/** @brief Prepare one ordinary vector store without changing memory. */
auto prepare_memory_store(LaneResourceResolver& resolver,
                          const exec_ir::Address& address,
                          const exec_ir::RegisterVector& source,
                          exec_ir::DataType type, exec_ir::AddressSpace space,
                          std::optional<exec_ir::VectorArity> vector,
                          common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause>;

}  // namespace ptxsim::inst_execute_engine::detail
