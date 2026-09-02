#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>

#include <array>
#include <functional>
#include <utility>

#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/scalar.hpp>
#include <ptxsim/memory/register/register_view.hpp>

namespace ptxsim::inst_execute_engine {
namespace {

struct PreparedWrite {
  memory::RegisterView registers;
  common::RegisterSlot destination;
  common::RawValue value;
};

struct ExitControl {};

using PreparedControl = std::variant<common::ProgramCounter, ExitControl>;

struct PreparedEffect {
  std::optional<PreparedWrite> write;
  PreparedControl control;
};

struct PreparedLane {
  execution_model::Thread* thread;
  PreparedEffect effect;
};

class LaneRegisterResolver final {
 public:
  LaneRegisterResolver(runtime::LaunchRuntime& runtime,
                       execution_model::Thread& thread,
                       common::FunctionId function) noexcept
      : runtime_(runtime), thread_(thread), function_(function) {}

  auto resolve()
      -> std::expected<std::reference_wrapper<const memory::RegisterView>,
                       LaneFaultCause> {
    if (!registers_) {
      const auto frame = runtime_.register_frame(thread_.id(), function_);
      if (!frame) {
        return std::unexpected(LaneFaultCause{frame.error()});
      }
      const auto view = runtime_.registers().view(*frame);
      if (!view) {
        return std::unexpected(LaneFaultCause{view.error()});
      }
      registers_ = *view;
    }
    return std::cref(*registers_);
  }

 private:
  runtime::LaunchRuntime& runtime_;
  execution_model::Thread& thread_;
  common::FunctionId function_;
  std::optional<memory::RegisterView> registers_;
};

auto step_error(StepErrorCode code,
                std::optional<execution_model::LaneId> lane = std::nullopt)
    -> std::unexpected<StepError> {
  return std::unexpected(StepError{code, lane});
}

auto b32_operand(const memory::RegisterView& registers,
                 const B32ProbeOperand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (const auto* immediate = std::get_if<common::RawValue>(&operand)) {
    if (const auto value = immediate->as_b32(); value) {
      return *value;
    } else {
      return std::unexpected(LaneFaultCause{value.error()});
    }
  }
  const auto value = registers.read(std::get<common::RegisterSlot>(operand));
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  if (const auto b32 = value->as_b32(); b32) {
    return *b32;
  } else {
    return std::unexpected(LaneFaultCause{b32.error()});
  }
}

auto b32_destination(const memory::RegisterView& registers,
                     common::RegisterSlot destination)
    -> std::expected<void, LaneFaultCause> {
  const auto width = registers.declared_width(destination);
  if (!width) {
    return std::unexpected(LaneFaultCause{width.error()});
  }
  if (*width != common::RawWidth::b32) {
    return std::unexpected(
        LaneFaultCause{common::RawValueError{common::RawWidth::b32, *width}});
  }
  return {};
}

auto prepare_operation(const memory::RegisterView& registers,
                       const arith::context&, const MoveProbe& operation,
                       common::ProgramCounter fallthrough)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto value = b32_operand(registers, B32ProbeOperand{operation.source});
  if (!value) {
    return std::unexpected(value.error());
  }
  if (const auto destination =
          b32_destination(registers, operation.destination);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .write = PreparedWrite{registers, operation.destination,
                             common::RawValue::b32(*value)},
      .control = fallthrough,
  };
}

auto prepare_operation(const memory::RegisterView& registers,
                       const arith::context& arithmetic,
                       const AddProbe& operation,
                       common::ProgramCounter fallthrough)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto lhs = b32_operand(registers, operation.lhs);
  if (!lhs) {
    return std::unexpected(lhs.error());
  }
  const auto rhs = b32_operand(registers, operation.rhs);
  if (!rhs) {
    return std::unexpected(rhs.error());
  }
  if (const auto destination =
          b32_destination(registers, operation.destination);
      !destination) {
    return std::unexpected(destination.error());
  }
  const auto sum = arith::add(arithmetic, *lhs, *rhs,
                              {.overflow = arith::integer_overflow_mode::wrap});
  if (!sum) {
    return std::unexpected(LaneFaultCause{sum.error()});
  }
  return PreparedEffect{
      .write = PreparedWrite{registers, operation.destination,
                             common::RawValue::b32(sum->value)},
      .control = fallthrough,
  };
}

auto prepare_operation(const BranchProbe& operation)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return PreparedEffect{
      .write = std::nullopt,
      .control = operation.target,
  };
}

auto prepare_operation(const ExitProbe&)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return PreparedEffect{
      .write = std::nullopt,
      .control = ExitControl{},
  };
}

using LanePrepareHandler = std::expected<PreparedEffect, LaneFaultCause> (*)(
    LaneRegisterResolver&, const arith::context&, const ProbeOperation&,
    common::ProgramCounter);

auto prepare_move_b32(LaneRegisterResolver& registers,
                      const arith::context& arithmetic,
                      const ProbeOperation& operation,
                      common::ProgramCounter fallthrough)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = registers.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_operation(view->get(), arithmetic,
                           std::get<MoveProbe>(operation), fallthrough);
}

auto prepare_add_u32(LaneRegisterResolver& registers,
                     const arith::context& arithmetic,
                     const ProbeOperation& operation,
                     common::ProgramCounter fallthrough)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = registers.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_operation(view->get(), arithmetic,
                           std::get<AddProbe>(operation), fallthrough);
}

auto prepare_branch(LaneRegisterResolver&, const arith::context&,
                    const ProbeOperation& operation, common::ProgramCounter)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(std::get<BranchProbe>(operation));
}

auto prepare_exit(LaneRegisterResolver&, const arith::context&,
                  const ProbeOperation& operation, common::ProgramCounter)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(std::get<ExitProbe>(operation));
}

using OpFamilySelector =
    std::expected<LanePrepareHandler, StepErrorCode> (*)(const ProbeOperation&);

auto unsupported_instruction() -> std::unexpected<StepErrorCode> {
  return std::unexpected(StepErrorCode::unsupported_instruction);
}

auto select_move(const ProbeOperation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  switch (std::get<MoveProbe>(operation).type) {
    case DataType::b32:
      return prepare_move_b32;
    case DataType::u32:
      return unsupported_instruction();
  }
  return unsupported_instruction();
}

auto select_add(const ProbeOperation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  switch (std::get<AddProbe>(operation).type) {
    case DataType::u32:
      return prepare_add_u32;
    case DataType::b32:
      return unsupported_instruction();
  }
  return unsupported_instruction();
}

auto select_branch(const ProbeOperation&)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  return prepare_branch;
}

auto select_exit(const ProbeOperation&)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  return prepare_exit;
}

auto select_preparer(const ProbeOperation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  static_assert(std::variant_size_v<ProbeOperation> ==
                static_cast<std::size_t>(Op::exit) + 1);
  static_assert(static_cast<std::size_t>(Op::mov) == 0);
  static_assert(static_cast<std::size_t>(Op::add) == 1);
  static_assert(static_cast<std::size_t>(Op::bra) == 2);
  static_assert(static_cast<std::size_t>(Op::exit) == 3);
  static constexpr std::array<OpFamilySelector,
                              std::variant_size_v<ProbeOperation>>
      dispatch{
          select_move,
          select_add,
          select_branch,
          select_exit,
      };
  return dispatch[static_cast<std::size_t>(op(operation))](operation);
}

struct ControlCommitter final {
  execution_model::Thread& thread;

  void operator()(common::ProgramCounter pc) const { thread.set_pc(pc); }

  void operator()(ExitControl) const { thread.mark_exited(); }
};

void apply_control(execution_model::Thread& thread,
                   const PreparedControl& control) {
  std::visit(ControlCommitter{thread}, control);
}

}  // namespace

InstExecuteEngine::InstExecuteEngine(runtime::LaunchRuntime& runtime,
                                     common::FunctionId function,
                                     const arith::context& arithmetic) noexcept
    : runtime_(runtime), function_(function), arithmetic_(arithmetic) {}

auto InstExecuteEngine::execute(execution_model::Warp& warp,
                                const execution_model::WarpIssueGroup& issue,
                                const ProbeInstruction& instruction,
                                common::ProgramCounter fallthrough)
    -> std::expected<StepReport, StepError> {
  const auto* runtime_warp = runtime_.grid().find_warp(warp.id());
  if (runtime_warp != &warp) {
    return step_error(StepErrorCode::foreign_warp);
  }
  if (issue.lanes.size() != warp.architectural_warp_size()) {
    return step_error(StepErrorCode::lane_mask_width);
  }
  if (issue.empty()) {
    return step_error(StepErrorCode::empty_issue);
  }

  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) {
      continue;
    }
    if (!warp.valid_mask().test(lane)) {
      return step_error(StepErrorCode::invalid_lane, lane);
    }
    const auto& thread = warp.thread(lane);
    if (!thread.ready()) {
      return step_error(StepErrorCode::lane_not_ready, lane);
    }
    if (thread.pc() != issue.pc) {
      return step_error(StepErrorCode::pc_mismatch, lane);
    }
  }

  const auto prepare = select_preparer(instruction.operation);
  if (!prepare) {
    return step_error(prepare.error());
  }

  StepReport report;
  std::vector<PreparedLane> prepared;
  prepared.reserve(issue.size());

  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) {
      continue;
    }
    auto& thread = warp.thread(lane);
    LaneRegisterResolver registers(runtime_, thread, function_);
    if (instruction.predicate) {
      const auto view = registers.resolve();
      if (!view) {
        report.faults.push_back({lane, view.error()});
        continue;
      }
      const auto predicate = view->get().read(instruction.predicate->source);
      if (!predicate) {
        report.faults.push_back({lane, predicate.error()});
        continue;
      }
      const auto value = predicate->as_pred();
      if (!value) {
        report.faults.push_back({lane, value.error()});
        continue;
      }
      if (*value == instruction.predicate->negated) {
        prepared.push_back(
            {&thread, {.write = std::nullopt, .control = fallthrough}});
        continue;
      }
    }
    const auto effect =
        (*prepare)(registers, arithmetic_, instruction.operation, fallthrough);
    if (!effect) {
      report.faults.push_back({lane, effect.error()});
      continue;
    }
    prepared.push_back({&thread, *effect});
  }

  for (auto& lane : prepared) {
    if (lane.effect.write) {
      if (const auto write = lane.effect.write->registers.write(
              lane.effect.write->destination, lane.effect.write->value);
          !write) {
        report.faults.push_back({lane.thread->lane_id(), write.error()});
        continue;
      }
    }
    apply_control(*lane.thread, lane.effect.control);
  }

  for (const auto& fault : report.faults) {
    warp.thread(fault.lane).mark_trapped();
  }
  return report;
}

}  // namespace ptxsim::inst_execute_engine
