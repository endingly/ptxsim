#pragma once

#include <optional>
#include <variant>
#include <vector>

#include <ptxsim/arith/error.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/exec_ir_types.hpp>
#include <ptxsim/execution_model/ids.hpp>
#include <ptxsim/memory/address_space/address_space_error.hpp>
#include <ptxsim/memory/address_space/generic_address.hpp>
#include <ptxsim/memory/register/register_error.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine {

/** @brief A special register whose architectural backing is not modeled. */
struct UnsupportedSpecialRegister {
  /** Program-stable special-register identity requested by the instruction. */
  common::SpecialRegisterId id;
  /** Optional component requested from a vector-capable special register. */
  std::optional<std::uint8_t> component;

  /** @brief Compare the unavailable special-register identity and component. */
  constexpr bool operator==(const UnsupportedSpecialRegister&) const noexcept =
      default;
};

/** @brief A MOV source category that needs storage or address-state support. */
struct UnsupportedMovSource {
  /** @brief Compare source-category prerequisite failures. */
  constexpr bool operator==(const UnsupportedMovSource&) const noexcept =
      default;
};

/** @brief A MOV type whose raw movement representation is unavailable. */
struct UnsupportedMovType {
  /** Execution-IR type selector that has no supported raw movement path. */
  exec_ir::DataType type;

  /** @brief Compare the movement type selectors reported by these failures. */
  constexpr bool operator==(const UnsupportedMovType&) const noexcept = default;
};

/** @brief A malformed direct MOV register-vector record. */
struct InvalidMovVector {
  /** @brief Compare malformed movement-vector failures. */
  constexpr bool operator==(const InvalidMovVector&) const noexcept = default;
};

/** @brief A malformed direct predicate MOV destination record. */
struct InvalidMovPredicate {
  /** @brief Compare malformed predicate-movement failures. */
  constexpr bool operator==(const InvalidMovPredicate&) const noexcept =
      default;
};

/** @brief Reason an issue group cannot commit execution. */
enum class StepErrorCode {
  foreign_warp,
  lane_mask_width,
  empty_issue,
  invalid_lane,
  lane_not_ready,
  pc_mismatch,
  missing_fallthrough,
  unsupported_instruction,
  /** The supplied execution IR has an invalid enum or control combination. */
  invalid_instruction,
  /** A membermask is empty, out of range, or excludes an issued lane. */
  collective_invalid_mask,
  /** Issued lanes read different membermask values. */
  collective_mask_mismatch,
  /** An arrival conflicts with the active rendezvous PC or participants. */
  collective_pending_mismatch,
  /** An issue attempts to arrive a lane already recorded by this rendezvous. */
  collective_duplicate_arrival,
  /** A first rendezvous names a lane that cannot reach it. */
  collective_unreachable_participant,
};

/** @brief A non-lane-local rejection that prevents an issue from completing. */
struct StepError {
  /** Machine-readable rejection category. */
  StepErrorCode code;
  /** Issued lane responsible for a lane-specific rejection, when applicable. */
  std::optional<execution_model::LaneId> lane;

  /** @brief Compare the rejection category and its optional responsible lane. */
  constexpr bool operator==(const StepError&) const noexcept = default;
};

/** @brief Typed faults that can occur while preparing or committing one lane. */
using LaneFaultCause =
    std::variant<runtime::RuntimeBindingError, runtime::StorageError,
                 memory::RegisterError, common::RawValueError,
                 arith::arithmetic_error, memory::AddressResolutionError,
                 memory::AddressSpaceError, UnsupportedSpecialRegister,
                 UnsupportedMovSource, UnsupportedMovType, InvalidMovVector,
                 InvalidMovPredicate>;

/** @brief A fault retained after the issue's other eligible lanes execute. */
struct LaneFault {
  /** Lane whose instruction preparation or commit could not complete. */
  execution_model::LaneId lane;
  /** Structured cause retained after other lanes in the issue commit. */
  LaneFaultCause cause;
  /** Owning warp when a deferred collective releases outside the issuing warp. */
  std::optional<execution_model::WarpId> warp;

  /** @brief Compare both the faulting lane and its typed execution cause. */
  constexpr bool operator==(const LaneFault&) const = default;
};

/** @brief The lane-local faults produced while executing one issue group. */
struct StepReport {
  /** Faults in preparation order, followed by faults encountered during commit. */
  std::vector<LaneFault> faults;
};

}  // namespace ptxsim::inst_execute_engine
