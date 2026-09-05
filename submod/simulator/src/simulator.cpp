#include <ptxsim/simulator/simulator.hpp>

#include <utility>

#include <ptxsim/execution_model/cta.hpp>
#include <ptxsim/execution_model/lane_mask.hpp>
#include <ptxsim/execution_model/thread.hpp>
#include <ptxsim/execution_model/warp.hpp>

namespace ptxsim::simulator {
namespace {

/** @brief Package a validated-program failure as a runner error. */
auto program_error(exec_ir::ProgramError error) -> RunError {
  return {.code = RunErrorCode::program_error, .program_error = error};
}

/** @brief Package a register-frame failure as a runner error. */
auto register_error(memory::RegisterError error) -> RunError {
  return {.code = RunErrorCode::register_error, .register_error = error};
}

/** @brief Package an entry-parameter byte-count mismatch as a runner error. */
auto entry_parameter_size_error(std::size_t expected, std::size_t actual)
    -> RunError {
  RunError result{.code = RunErrorCode::entry_parameter_size_mismatch};
  result.entry_parameter_size_error =
      EntryParameterSizeError{.expected = expected, .actual = actual};
  return result;
}

/** @brief Package an address-space operation failure as a runner error. */
auto address_space_error(memory::AddressSpaceError error) -> RunError {
  return {.code = RunErrorCode::address_space_error,
          .address_space_error = error};
}

/** @brief Package a runtime binding failure as a runner error. */
auto runtime_binding_error(runtime::RuntimeBindingError error) -> RunError {
  return {.code = RunErrorCode::runtime_binding_error,
          .runtime_binding_error = error};
}

/** @brief Package an instruction-engine rejection as a runner error. */
auto execution_error(inst_execute_engine::StepError error) -> RunError {
  return {.code = RunErrorCode::execution_error, .execution_error = error};
}

/**
 * @brief Select the PC of the lowest-ID ready lane and all ready lanes at it.
 *
 * The explicit minimum preserves this policy even if Warp changes its thread
 * storage order.
 */
auto ready_group(const execution_model::Warp& warp)
    -> std::optional<execution_model::WarpIssueGroup> {
  std::optional<common::ProgramCounter> selected_pc;
  std::optional<execution_model::LaneId> selected_lane;
  for (const auto& thread : warp) {
    if (thread.ready() &&
        (!selected_lane || thread.lane_id() < *selected_lane)) {
      selected_lane = thread.lane_id();
      selected_pc = thread.pc();
    }
  }
  if (!selected_pc) {
    return std::nullopt;
  }

  execution_model::LaneMask lanes{warp.architectural_warp_size()};
  for (const auto& thread : warp) {
    if (thread.ready() && thread.pc() == *selected_pc) {
      lanes.set(thread.lane_id());
    }
  }
  return execution_model::WarpIssueGroup{.pc = *selected_pc,
                                         .lanes = std::move(lanes)};
}

/** @brief A topology-selected warp and the ready lanes it will issue. */
struct SelectedWarpGroup {
  /** @brief Non-owning warp selected from the runtime's stable topology. */
  execution_model::Warp* warp;
  /** @brief Same-PC ready lanes selected within @ref warp. */
  execution_model::WarpIssueGroup group;
};

/**
 * @brief Return the first topology-order warp containing a ready issue group.
 *
 * CTAs and their warps retain construction order, which defines this
 * deterministic cross-warp policy.
 */
auto ready_warp_group(runtime::LaunchRuntime& runtime)
    -> std::optional<SelectedWarpGroup> {
  for (auto& cta : runtime.grid()) {
    for (auto& warp : cta) {
      const auto group = ready_group(warp);
      if (group) {
        return SelectedWarpGroup{.warp = &warp, .group = *group};
      }
    }
  }
  return std::nullopt;
}

/**
 * @brief Create entry-function frames only for threads lacking a binding.
 *
 * Existing bindings must still resolve to a live register frame; stale or
 * otherwise invalid bindings are reported instead of being replaced.
 */
auto provision_register_frames(runtime::LaunchRuntime& runtime,
                               const execution_model::Warp& warp,
                               const exec_ir::FunctionLayout& layout,
                               common::FunctionId function)
    -> std::expected<void, RunError> {
  for (const auto& thread : warp) {
    const auto binding = runtime.register_frame(thread.id(), function);
    if (binding) {
      const auto frame = runtime.registers().view(*binding);
      if (!frame) {
        return std::unexpected(register_error(frame.error()));
      }
      continue;
    }
    if (binding.error().code !=
            runtime::RuntimeBindingErrorCode::missing_binding ||
        binding.error().resource !=
            runtime::RuntimeResourceKind::register_frame) {
      return std::unexpected(runtime_binding_error(binding.error()));
    }

    const auto frame = runtime.registers().create_frame(
        {.slot_widths = layout.register_widths});
    if (!frame) {
      return std::unexpected(register_error(frame.error()));
    }
    const auto bound =
        runtime.bind_register_frame(thread.id(), function, *frame);
    if (!bound) {
      return std::unexpected(runtime_binding_error(bound.error()));
    }
  }
  return {};
}

}  // namespace

Simulator::Simulator(exec_ir::ExecutableProgram program,
                     runtime::LaunchRuntime& runtime,
                     common::FunctionId entry_function,
                     const arith::context& arithmetic,
                     std::vector<std::byte> entry_parameters) noexcept
    : program_(std::move(program)),
      runtime_(runtime),
      entry_function_(entry_function),
      arithmetic_(arithmetic),
      entry_parameters_(std::move(entry_parameters)) {}

auto Simulator::initialize() -> std::expected<void, RunError> {
  if (initialization_error_) {
    return std::unexpected(*initialization_error_);
  }
  if (initialized_) {
    return {};
  }
  const auto layout = program_.function_layout(entry_function_);
  if (!layout) {
    initialization_error_ = program_error(layout.error());
    return std::unexpected(*initialization_error_);
  }
  if (entry_parameters_.size() != layout->get().entry_parameter_size) {
    initialization_error_ = entry_parameter_size_error(
        layout->get().entry_parameter_size, entry_parameters_.size());
    return std::unexpected(*initialization_error_);
  }
  if (layout->get().entry_parameter_size != 0U) {
    const auto parameter = runtime_.address_spaces().create_entry_parameter(
        {.size = layout->get().entry_parameter_size});
    auto parameter_view = runtime_.address_spaces().view(parameter);
    if (!parameter_view) {
      initialization_error_ = address_space_error(parameter_view.error());
      return std::unexpected(*initialization_error_);
    }
    const auto initialized =
        parameter_view->initialize(memory::Address{0}, entry_parameters_);
    if (!initialized) {
      initialization_error_ = address_space_error(initialized.error());
      return std::unexpected(*initialization_error_);
    }
    const auto bound = runtime_.bind_entry_parameter(parameter);
    if (!bound) {
      initialization_error_ = runtime_binding_error(bound.error());
      return std::unexpected(*initialization_error_);
    }
  }
  for (const auto& cta : runtime_.grid()) {
    for (const auto& warp : cta) {
      const auto provisioned = provision_register_frames(
          runtime_, warp, layout->get(), entry_function_);
      if (!provisioned) {
        initialization_error_ = provisioned.error();
        return std::unexpected(*initialization_error_);
      }
    }
  }
  initialized_ = true;
  return {};
}

auto Simulator::step() -> std::expected<StepReport, RunError> {
  const auto initialized = initialize();
  if (!initialized) {
    return std::unexpected(initialized.error());
  }
  if (runtime_.grid().completed()) {
    return StepReport{.termination = StepTermination::completed};
  }
  if (runtime_.grid().trapped()) {
    return StepReport{.termination = StepTermination::trapped};
  }

  const auto selected = ready_warp_group(runtime_);
  if (!selected) {
    return StepReport{.termination = StepTermination::deadlocked};
  }
  const common::CodeLocation location{entry_function_, selected->group.pc};
  const auto instruction = program_.fetch(location);
  if (!instruction) {
    return std::unexpected(program_error(instruction.error()));
  }

  std::optional<common::ProgramCounter> successor;
  if (exec_ir::may_fallthrough(instruction->get())) {
    const auto fallthrough = program_.fallthrough(location);
    if (!fallthrough) {
      return std::unexpected(program_error(fallthrough.error()));
    }
    successor = fallthrough->pc;
  }

  inst_execute_engine::InstExecuteEngine engine{runtime_, entry_function_,
                                                arithmetic_};
  const auto result = engine.execute(*selected->warp, selected->group,
                                     instruction->get(), successor);
  if (!result) {
    return std::unexpected(execution_error(result.error()));
  }
  if (!result->faults.empty() || runtime_.grid().trapped()) {
    return StepReport{.termination = StepTermination::trapped,
                      .issue = IssuedWarpGroup{.warp = selected->warp->id(),
                                               .group = selected->group},
                      .faults = result->faults};
  }
  if (runtime_.grid().completed()) {
    return StepReport{.termination = StepTermination::completed,
                      .issue = IssuedWarpGroup{.warp = selected->warp->id(),
                                               .group = selected->group}};
  }
  return StepReport{.termination = StepTermination::issued,
                    .issue = IssuedWarpGroup{.warp = selected->warp->id(),
                                             .group = selected->group}};
}

auto Simulator::run(std::size_t maximum_issued_groups)
    -> std::expected<RunReport, RunError> {
  const auto initialized = initialize();
  if (!initialized) {
    return std::unexpected(initialized.error());
  }

  std::size_t issued_groups = 0;
  while (issued_groups < maximum_issued_groups) {
    const auto step_report = step();
    if (!step_report) {
      return std::unexpected(step_report.error());
    }
    if (step_report->issue) {
      ++issued_groups;
    }
    switch (step_report->termination) {
      case StepTermination::issued:
        continue;
      case StepTermination::completed:
        return RunReport{RunTermination::completed, issued_groups};
      case StepTermination::trapped:
        return RunReport{
            RunTermination::trapped, issued_groups, step_report->faults,
            step_report->issue ? std::optional{step_report->issue->warp}
                               : std::nullopt};
      case StepTermination::deadlocked:
        return RunReport{RunTermination::deadlocked, issued_groups};
    }
  }

  if (runtime_.grid().completed()) {
    return RunReport{RunTermination::completed, issued_groups};
  }
  if (runtime_.grid().trapped()) {
    return RunReport{RunTermination::trapped, issued_groups};
  }
  if (!ready_warp_group(runtime_)) {
    return RunReport{RunTermination::deadlocked, issued_groups};
  }
  return RunReport{RunTermination::step_limit_exhausted, issued_groups};
}

}  // namespace ptxsim::simulator
