#include <ptxsim/simulator/simulator.hpp>

#include <map>
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

/** @brief Package a runtime binding failure as a runner error. */
auto runtime_binding_error(runtime::RuntimeBindingError error) -> RunError {
  return {.code = RunErrorCode::runtime_binding_error,
          .runtime_binding_error = error};
}

/** @brief Package an instruction-engine rejection as a runner error. */
auto execution_error(inst_execute_engine::StepError error) -> RunError {
  return {.code = RunErrorCode::execution_error, .execution_error = error};
}

/** @brief Return the sole warp after the caller established the one-warp limit. */
auto only_warp(runtime::LaunchRuntime& runtime) noexcept
    -> execution_model::Warp& {
  for (auto& cta : runtime.grid()) {
    for (auto& warp : cta) {
      return warp;
    }
  }
  std::unreachable();
}

/** @brief Partition ready lanes into full-width issue masks by current PC. */
auto ready_groups(const execution_model::Warp& warp)
    -> std::map<common::ProgramCounter, execution_model::LaneMask> {
  std::map<common::ProgramCounter, execution_model::LaneMask> groups;
  for (const auto& thread : warp) {
    if (!thread.ready()) {
      continue;
    }
    auto [group, inserted] = groups.try_emplace(
        thread.pc(), execution_model::LaneMask{warp.architectural_warp_size()});
    (void)inserted;
    group->second.set(thread.lane_id());
  }
  return groups;
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

auto run_to_completion(runtime::LaunchRuntime& runtime,
                       const exec_ir::ExecutableProgram& program,
                       common::FunctionId entry_function,
                       const arith::context& arithmetic,
                       std::size_t maximum_issued_groups)
    -> std::expected<RunReport, RunError> {
  if (runtime.grid().warp_count() != 1U) {
    return std::unexpected(RunError{.code = RunErrorCode::requires_one_warp});
  }

  auto& warp = only_warp(runtime);
  const auto layout = program.function_layout(entry_function);
  if (!layout) {
    return std::unexpected(program_error(layout.error()));
  }
  const auto provisioned =
      provision_register_frames(runtime, warp, layout->get(), entry_function);
  if (!provisioned) {
    return std::unexpected(provisioned.error());
  }
  inst_execute_engine::InstExecuteEngine engine{runtime, entry_function,
                                                arithmetic};
  std::size_t issued_groups = 0;
  while (issued_groups < maximum_issued_groups) {
    if (runtime.grid().completed()) {
      return RunReport{RunTermination::completed, issued_groups};
    }
    if (runtime.grid().trapped()) {
      return RunReport{RunTermination::trapped, issued_groups};
    }

    auto groups = ready_groups(warp);
    if (groups.empty()) {
      return RunReport{RunTermination::deadlocked, issued_groups};
    }
    auto group = std::move(*groups.begin());
    const common::CodeLocation location{entry_function, group.first};
    const auto instruction = program.fetch(location);
    if (!instruction) {
      return std::unexpected(program_error(instruction.error()));
    }

    std::optional<common::ProgramCounter> successor;
    if (exec_ir::may_fallthrough(instruction->get())) {
      const auto fallthrough = program.fallthrough(location);
      if (!fallthrough) {
        return std::unexpected(program_error(fallthrough.error()));
      }
      successor = fallthrough->pc;
    }

    const auto result = engine.execute(
        warp, {.pc = group.first, .lanes = std::move(group.second)},
        instruction->get(), successor);
    if (!result) {
      return std::unexpected(execution_error(result.error()));
    }
    ++issued_groups;
  }

  if (runtime.grid().completed()) {
    return RunReport{RunTermination::completed, issued_groups};
  }
  if (runtime.grid().trapped()) {
    return RunReport{RunTermination::trapped, issued_groups};
  }
  if (ready_groups(warp).empty()) {
    return RunReport{RunTermination::deadlocked, issued_groups};
  }
  return RunReport{RunTermination::step_limit_exhausted, issued_groups};
}

}  // namespace ptxsim::simulator
