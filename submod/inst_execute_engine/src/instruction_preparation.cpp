#include "instruction_preparation.hpp"
#include "instruction_preparation.gen.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/scalar.hpp>

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Resolve an implemented scalar move source to b32 raw bits. */
auto b32_move_source(const memory::RegisterView& registers,
                     const execution_model::Thread& thread,
                     const exec_ir::MovSource& source)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (const auto* register_slot = std::get_if<common::RegisterSlot>(&source)) {
    return b32_operand(registers, exec_ir::B32Operand{*register_slot});
  }
  if (const auto* immediate = std::get_if<common::RawValue>(&source)) {
    const auto value = immediate->as_b32();
    if (!value) {
      return std::unexpected(LaneFaultCause{value.error()});
    }
    return *value;
  }
  if (const auto* special_register =
          std::get_if<exec_ir::SpecialRegisterRef>(&source);
      special_register != nullptr &&
      special_register->id == exec_ir::kThreadIdSpecialRegister &&
      special_register->component == std::optional<std::uint8_t>{0U}) {
    return thread.position().x;
  }
  return std::unexpected(LaneFaultCause{
      common::RawValueError{common::RawWidth::b32, common::RawWidth::b64}});
}

/** @brief Verify that a scalar destination accepts b32 raw bits. */
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

/** @brief Resolve an implemented scalar move source to b64 raw bits. */
auto b64_move_source(const memory::RegisterView& registers,
                     const exec_ir::MovSource& source)
    -> std::expected<std::uint64_t, LaneFaultCause> {
  if (const auto* register_slot = std::get_if<common::RegisterSlot>(&source)) {
    const auto value = registers.read(*register_slot);
    if (!value) {
      return std::unexpected(LaneFaultCause{value.error()});
    }
    if (const auto b64 = value->as_b64(); b64) {
      return *b64;
    } else {
      return std::unexpected(LaneFaultCause{b64.error()});
    }
  }
  if (const auto* immediate = std::get_if<common::RawValue>(&source)) {
    if (const auto b64 = immediate->as_b64(); b64) {
      return *b64;
    } else {
      return std::unexpected(LaneFaultCause{b64.error()});
    }
  }
  return std::unexpected(LaneFaultCause{
      common::RawValueError{common::RawWidth::b64, common::RawWidth::b32}});
}

/** @brief Verify that a scalar destination accepts b64 raw bits. */
auto b64_destination(const memory::RegisterView& registers,
                     common::RegisterSlot destination)
    -> std::expected<void, LaneFaultCause> {
  const auto width = registers.declared_width(destination);
  if (!width) {
    return std::unexpected(LaneFaultCause{width.error()});
  }
  if (*width != common::RawWidth::b64) {
    return std::unexpected(
        LaneFaultCause{common::RawValueError{common::RawWidth::b64, *width}});
  }
  return {};
}

/** @brief Stage one b32 scalar move without changing its destination frame. */
auto prepare_operation(const memory::RegisterView& registers,
                       const execution_model::Thread& thread,
                       const arith::context&, const exec_ir::Mov& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto& form = std::get<exec_ir::Mov::Scalar>(operation.variant);
  const auto& operands =
      std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
  const auto value = b32_move_source(registers, thread, operands.src);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (const auto destination = b32_destination(registers, operands.dst);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .writes = {PreparedWrite{registers, operands.dst,
                               common::RawValue::b32(*value)}},
      .memory_write = std::nullopt,
      .control = successor};
}

/** @brief Stage one b64 scalar move without changing its destination frame. */
auto prepare_move_b64_operation(const memory::RegisterView& registers,
                                const exec_ir::Mov& operation,
                                common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto& form = std::get<exec_ir::Mov::Scalar>(operation.variant);
  const auto& operands =
      std::get<exec_ir::Mov::Scalar::ScalarOperands>(form.operands);
  const auto value = b64_move_source(registers, operands.src);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (const auto destination = b64_destination(registers, operands.dst);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .writes = {PreparedWrite{registers, operands.dst,
                               common::RawValue::b64(*value)}},
      .memory_write = std::nullopt,
      .control = successor};
}

/** @brief Adapt scalar b32 moves to the common opcode dispatch signature. */
auto prepare_move_b32(LaneResourceResolver& resolver,
                      const arith::context& arithmetic,
                      const exec_ir::Instruction& operation,
                      std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = resolver.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_operation(view->get(), resolver.thread(), arithmetic,
                           std::get<exec_ir::Mov>(operation), *successor);
}

/** @brief Adapt scalar b64 moves to the common opcode dispatch signature. */
auto prepare_move_b64(LaneResourceResolver& resolver, const arith::context&,
                      const exec_ir::Instruction& operation,
                      std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = resolver.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_move_b64_operation(
      view->get(), std::get<exec_ir::Mov>(operation), *successor);
}

/** @brief Return the standard unsupported result for declaration-only forms. */
auto unsupported_instruction() -> std::unexpected<StepErrorCode> {
  return std::unexpected(StepErrorCode::unsupported_instruction);
}

/** @brief Select the implemented scalar move form, if present. */
auto select_move(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!std::holds_alternative<exec_ir::Mov::Scalar>(
          std::get<exec_ir::Mov>(operation).variant)) {
    return unsupported_instruction();
  }
  switch (
      std::get<exec_ir::Mov::Scalar>(std::get<exec_ir::Mov>(operation).variant)
          .type) {
    case exec_ir::DataType::b32:
    case exec_ir::DataType::u32:
      return SelectedPreparer{prepare_move_b32, PrepareKind::scalar};
    case exec_ir::DataType::b64:
      return SelectedPreparer{prepare_move_b64, PrepareKind::scalar};
  }
  return unsupported_instruction();
}

/** @brief Select every generated and structurally valid projected Add form. */
auto select_add(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_add(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_add, PrepareKind::scalar};
}

/** @brief Select every generated and structurally valid projected Sub form. */
auto select_sub(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_sub(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_sub, PrepareKind::scalar};
}

/** @brief Select every generated and structurally valid projected Mul form. */
auto select_mul(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_mul(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_mul, PrepareKind::scalar};
}

/** @brief Select every generated and structurally valid projected FMA form. */
auto select_fma(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_fma(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_fma, PrepareKind::scalar};
}

/** @brief Select every generated and structurally valid projected Setp form. */
auto select_setp(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_setp(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_setp, PrepareKind::scalar};
}

/** @brief Select every generated, ordinary load form. */
auto select_load(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_ld(operation))
    return unsupported_instruction();
  return SelectedPreparer{generated::prepare_ld, PrepareKind::scalar};
}

/** @brief Select every generated, ordinary store form. */
auto select_store(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_st(operation))
    return unsupported_instruction();
  return SelectedPreparer{generated::prepare_st, PrepareKind::scalar};
}

/** @brief Select generated barrier preparation and its collective commit scope. */
auto select_bar(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_bar(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  const auto kind = std::holds_alternative<exec_ir::Bar::WarpSync>(
                        std::get<exec_ir::Bar>(operation).variant)
                        ? PrepareKind::warp_sync
                        : PrepareKind::cta_barrier;
  return SelectedPreparer{generated::prepare_bar, kind};
}

/** @brief Select the implemented direct branch form, if present. */
auto select_branch(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_bra(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_bra, PrepareKind::scalar};
}

/** @brief Select the implemented bare exit form, if present. */
auto select_exit(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_exit(operation)) {
    return std::unexpected(StepErrorCode::invalid_instruction);
  }
  return SelectedPreparer{generated::prepare_exit, PrepareKind::scalar};
}

}  // namespace

auto select_preparer(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  switch (exec_ir::op(operation)) {
    case exec_ir::Op::mov:
      return select_move(operation);
    case exec_ir::Op::add:
      return select_add(operation);
    case exec_ir::Op::sub:
      return select_sub(operation);
    case exec_ir::Op::mul:
      return select_mul(operation);
    case exec_ir::Op::fma:
      return select_fma(operation);
    case exec_ir::Op::setp:
      return select_setp(operation);
    case exec_ir::Op::ld:
      return select_load(operation);
    case exec_ir::Op::st:
      return select_store(operation);
    case exec_ir::Op::bar:
      return select_bar(operation);
    case exec_ir::Op::bra:
      return select_branch(operation);
    case exec_ir::Op::exit:
      return select_exit(operation);
    default:
      return unsupported_instruction();
  }
}

auto prepare_lane(LaneResourceResolver& resolver,
                  const SelectedPreparer& preparer,
                  const arith::context& arithmetic,
                  const exec_ir::Instruction& instruction,
                  std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  if (const auto& instruction_predicate =
          exec_ir::execution_predicate(instruction);
      instruction_predicate) {
    const auto view = resolver.resolve();
    if (!view) {
      return std::unexpected(view.error());
    }
    const auto predicate = view->get().read(instruction_predicate->source);
    if (!predicate) {
      return std::unexpected(LaneFaultCause{predicate.error()});
    }
    const auto value = predicate->as_pred();
    if (!value) {
      return std::unexpected(LaneFaultCause{value.error()});
    }
    if (*value == instruction_predicate->negated) {
      return PreparedEffect{.memory_write = std::nullopt,
                            .control = *successor};
    }
  }
  return preparer.handler(resolver, arithmetic, instruction, successor);
}

}  // namespace ptxsim::inst_execute_engine::detail
