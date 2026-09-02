#pragma once

#include <expected>
#include <optional>
#include <variant>
#include <vector>

#include <ptxsim/arith/context.hpp>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/common/ids.hpp>
#include <ptxsim/common/raw_value.hpp>
#include <ptxsim/execution_model/warp.hpp>
#include <ptxsim/memory/register/register_error.hpp>
#include <ptxsim/runtime/runtime.hpp>

namespace ptxsim::inst_execute_engine {

enum class Op {
  mov,
  add,
  bra,
  exit,
};

enum class DataType {
  b32,
  u32,
};

struct MoveProbe {
  DataType type;
  common::RegisterSlot source;
  common::RegisterSlot destination;
};

using B32ProbeOperand = std::variant<common::RegisterSlot, common::RawValue>;

struct AddProbe {
  DataType type;
  common::RegisterSlot destination;
  B32ProbeOperand lhs;
  B32ProbeOperand rhs;
};

struct BranchProbe {
  common::ProgramCounter target;
};

struct ExitProbe {};

struct PredicateProbe {
  common::RegisterSlot source;
  bool negated = false;
};

using ProbeOperation =
    std::variant<MoveProbe, AddProbe, BranchProbe, ExitProbe>;

namespace detail {

struct OperationOpVisitor final {
  constexpr auto operator()(const MoveProbe&) const noexcept -> Op {
    return Op::mov;
  }

  constexpr auto operator()(const AddProbe&) const noexcept -> Op {
    return Op::add;
  }

  constexpr auto operator()(const BranchProbe&) const noexcept -> Op {
    return Op::bra;
  }

  constexpr auto operator()(const ExitProbe&) const noexcept -> Op {
    return Op::exit;
  }
};

}  // namespace detail

[[nodiscard]] constexpr auto op(const ProbeOperation& operation) noexcept
    -> Op {
  return std::visit(detail::OperationOpVisitor{}, operation);
}

struct ProbeInstruction {
  std::optional<PredicateProbe> predicate;
  ProbeOperation operation;
};

enum class StepErrorCode {
  foreign_warp,
  lane_mask_width,
  empty_issue,
  invalid_lane,
  lane_not_ready,
  pc_mismatch,
  unsupported_instruction,
};

struct StepError {
  StepErrorCode code;
  std::optional<execution_model::LaneId> lane;

  constexpr bool operator==(const StepError&) const noexcept = default;
};

using LaneFaultCause =
    std::variant<runtime::RuntimeBindingError, memory::RegisterError,
                 common::RawValueError, arith::arithmetic_error>;

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
                             const ProbeInstruction& instruction,
                             common::ProgramCounter fallthrough)
      -> std::expected<StepReport, StepError>;

 private:
  runtime::LaunchRuntime& runtime_;
  common::FunctionId function_;
  const arith::context& arithmetic_;
};

}  // namespace ptxsim::inst_execute_engine
