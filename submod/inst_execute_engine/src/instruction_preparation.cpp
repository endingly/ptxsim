#include "instruction_preparation.hpp"
#include "instruction_preparation.gen.hpp"
#include "mov_preparation.hpp"

#include <type_traits>

namespace ptxsim::inst_execute_engine::detail {
namespace {

/** @brief Return the standard unsupported result for declaration-only forms. */
auto unsupported_instruction() -> std::unexpected<StepErrorCode> {
  return std::unexpected(StepErrorCode::unsupported_instruction);
}

/** @brief Select every generated and structurally valid movement form. */
auto select_move(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!generated::validate_mov(operation))
    return std::unexpected(StepErrorCode::invalid_instruction);
  return SelectedPreparer{generated::prepare_mov, PrepareKind::scalar};
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
