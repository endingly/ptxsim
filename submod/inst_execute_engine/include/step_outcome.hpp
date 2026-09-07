#pragma once

#include <optional>
#include <variant>
#include <vector>

#include <ptxsim/arith/error.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/execution_model/ids.hpp>
#include <ptxsim/memory/address_space/address_space_error.hpp>
#include <ptxsim/memory/address_space/generic_address.hpp>
#include <ptxsim/memory/register/register_error.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine {

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
    std::variant<runtime::RuntimeBindingError, memory::RegisterError,
                 common::RawValueError, arith::arithmetic_error,
                 memory::AddressResolutionError, memory::AddressSpaceError>;

/** @brief A fault retained after the issue's other eligible lanes execute. */
struct LaneFault {
  /** Lane whose instruction preparation or commit could not complete. */
  execution_model::LaneId lane;
  /** Structured cause retained after other lanes in the issue commit. */
  LaneFaultCause cause;

  /** @brief Compare both the faulting lane and its typed execution cause. */
  constexpr bool operator==(const LaneFault&) const = default;
};

/** @brief The lane-local faults produced while executing one issue group. */
struct StepReport {
  /** Faults in preparation order, followed by faults encountered during commit. */
  std::vector<LaneFault> faults;
};

}  // namespace ptxsim::inst_execute_engine
