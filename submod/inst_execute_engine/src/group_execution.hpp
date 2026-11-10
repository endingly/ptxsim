#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include <ptxsim/exec_ir/instruction_catalogue.gen.hpp>

#include "instruction_preparation.hpp"
#include "issue_validation.hpp"

namespace ptxsim::inst_execute_engine::detail {

/** @brief Static typed lane preparation operation, without a runtime dispatch pointer. */
template <typename Prepare>
concept TypedLanePreparation = requires(Prepare prepare, LaneResourceResolver& resolver,
                                       const arith::context& arithmetic,
                                       std::optional<common::ProgramCounter> successor) {
  { prepare(resolver, arithmetic, successor) } -> std::same_as<std::expected<PreparedEffect, LaneFaultCause>>;
};

/** @brief Prepare all lanes inside one selected typed group, preserving fault/commit order. */
template <exec_ir::GeneratedForm Form, TypedLanePreparation Prepare>
[[nodiscard]] auto prepare_group(runtime::LaunchRuntime& runtime,
                                 common::FunctionId function,
                                 const arith::context& arithmetic,
                                 execution_model::Warp& warp,
                                 const execution_model::WarpIssueGroup& issue,
                                 const Form& instruction,
                                 std::optional<common::ProgramCounter> successor,
                                 Prepare prepare, PrepareKind kind)
    -> std::expected<PreparedIssue, StepError> {
  constexpr auto opcode = Form::opcode;
  const bool needs_successor = (opcode != exec_ir::Opcode::Bra && opcode != exec_ir::Opcode::Exit) ||
                                instruction.execution_predicate.has_value();
  if (needs_successor && !successor) return step_error(StepErrorCode::missing_fallthrough);
  PreparedIssue result{kind, {}, {}};
  result.lanes.reserve(issue.size());
  for (std::uint32_t index = 0; index < warp.architectural_warp_size(); ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) continue;
    auto& thread = warp.thread(lane);
    LaneResourceResolver resolver(runtime, thread, function);
    const auto effect = [&]() -> std::expected<PreparedEffect, LaneFaultCause> {
      if (instruction.execution_predicate) {
        const auto view = resolver.resolve();
        if (!view) return std::unexpected(view.error());
        const auto raw = view->get().read(instruction.execution_predicate->register_ref.slot);
        if (!raw) return std::unexpected(LaneFaultCause{raw.error()});
        const auto predicate = raw->as_pred();
        if (!predicate) return std::unexpected(LaneFaultCause{predicate.error()});
        if (*predicate == instruction.execution_predicate->negated)
          return PreparedEffect{.memory_write = std::nullopt, .control = *successor};
      }
      return prepare(resolver, arithmetic, successor);
    }();
    if (!effect) result.report.faults.push_back({lane, effect.error()});
    else result.lanes.push_back({&thread, *effect});
  }
  return result;
}
}  // namespace ptxsim::inst_execute_engine::detail
