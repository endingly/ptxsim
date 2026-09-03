#pragma once

#include <expected>
#include <optional>
#include <variant>
#include <vector>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/exec_ir/exec_ir.hpp>
#include <ptxsim/execution_model/warp.hpp>
#include <ptxsim/memory/address_space/address_space_error.hpp>
#include <ptxsim/memory/address_space/generic_address.hpp>
#include <ptxsim/memory/register/register_error.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine {

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

struct StepError {
  StepErrorCode code;
  std::optional<execution_model::LaneId> lane;

  constexpr bool operator==(const StepError&) const noexcept = default;
};

using LaneFaultCause =
    std::variant<runtime::RuntimeBindingError, memory::RegisterError,
                 common::RawValueError, arith::arithmetic_error,
                 memory::AddressResolutionError, memory::AddressSpaceError>;

struct LaneFault {
  execution_model::LaneId lane;
  LaneFaultCause cause;
};

struct StepReport {
  std::vector<LaneFault> faults;
};

class InstExecuteEngine final {
 public:
  InstExecuteEngine(runtime::LaunchRuntime& runtime,
                    common::FunctionId function,
                    const arith::context& arithmetic) noexcept;

  [[nodiscard]] auto execute(execution_model::Warp& warp,
                             const execution_model::WarpIssueGroup& issue,
                             const exec_ir::Instruction& instruction,
                             std::optional<common::ProgramCounter> successor)
      -> std::expected<StepReport, StepError>;

 private:
  runtime::LaunchRuntime& runtime_;
  common::FunctionId function_;
  const arith::context& arithmetic_;
};

}  // namespace ptxsim::inst_execute_engine
