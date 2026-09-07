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

/** @brief Verify that a scalar destination is a predicate register. */
auto predicate_destination(const memory::RegisterView& registers,
                           common::RegisterSlot destination)
    -> std::expected<void, LaneFaultCause> {
  const auto width = registers.declared_width(destination);
  if (!width) {
    return std::unexpected(LaneFaultCause{width.error()});
  }
  if (*width != common::RawWidth::pred) {
    return std::unexpected(
        LaneFaultCause{common::RawValueError{common::RawWidth::pred, *width}});
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
  return PreparedEffect{.write = PreparedWrite{registers, operands.dst,
                                               common::RawValue::b32(*value)},
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
  return PreparedEffect{.write = PreparedWrite{registers, operands.dst,
                                               common::RawValue::b64(*value)},
                        .memory_write = std::nullopt,
                        .control = successor};
}

/** @brief Stage one unsigned less-than predicate comparison without mutation. */
auto prepare_operation(const memory::RegisterView& registers,
                       const exec_ir::Setp& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto& form = std::get<exec_ir::Setp::LtU32>(operation.variant);
  const auto lhs = b32_operand(registers, form.src1);
  if (!lhs) {
    return std::unexpected(lhs.error());
  }
  const auto rhs = b32_operand(registers, form.src2);
  if (!rhs) {
    return std::unexpected(rhs.error());
  }
  if (const auto destination =
          predicate_destination(registers, form.dst.source);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .write = PreparedWrite{registers, form.dst.source,
                             common::RawValue::pred(*lhs < *rhs)},
      .memory_write = std::nullopt,
      .control = successor};
}

/**
 * @brief Read one scalar memory value and stage its b32 register writeback.
 *
 * No register mutation occurs until the common commit phase.
 */
template <typename Form>
  requires requires(const Form& form) {
    form.address;
    form.dst;
  }
auto prepare_load(LaneResourceResolver& resolver, const Form& operation,
                  exec_ir::AddressSpace space, common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto memory = [&]()
      -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                       LaneFaultCause> {
    if (space == exec_ir::AddressSpace::param) {
      const auto address = entry_parameter_address(operation.address);
      if (!address) {
        return std::unexpected(address.error());
      }
      return resolver.resolve_entry_parameter(*address);
    }
    const auto address = register_address(operation.address);
    if (!address) {
      return std::unexpected(LaneFaultCause{
          common::RawValueError{common::RawWidth::b64, common::RawWidth::b32}});
    }
    return resolver.resolve_memory(space, *address);
  }();
  if (!memory) {
    return std::unexpected(memory.error());
  }
  std::array<std::byte, 4> bytes;
  if (const auto read = memory->first.read(memory->second, bytes, 4); !read) {
    return std::unexpected(LaneFaultCause{read.error()});
  }
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  if (const auto destination = b32_destination(registers->get(), operation.dst);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .write = PreparedWrite{registers->get(), operation.dst,
                             common::RawValue::b32(bytes_b32(bytes))},
      .memory_write = std::nullopt,
      .control = successor};
}

/** @brief Validate one scalar store and retain its bytes for commit. */
template <typename Form>
  requires requires(const Form& form) {
    form.address;
    form.src;
  }
auto prepare_store(LaneResourceResolver& resolver, const Form& operation,
                   exec_ir::AddressSpace space,
                   common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto address = register_address(operation.address);
  if (!address) {
    return std::unexpected(LaneFaultCause{
        common::RawValueError{common::RawWidth::b64, common::RawWidth::b32}});
  }
  const auto memory = resolver.resolve_memory(space, *address);
  if (!memory) {
    return std::unexpected(memory.error());
  }
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  const auto source = registers->get().read(operation.src);
  if (!source) {
    return std::unexpected(LaneFaultCause{source.error()});
  }
  const auto value = source->as_b32();
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  if (const auto valid = memory->first.validate_write(memory->second, 4, 4);
      !valid) {
    return std::unexpected(LaneFaultCause{valid.error()});
  }
  return PreparedEffect{
      .write = std::nullopt,
      .memory_write =
          PreparedMemoryWrite{memory->first, memory->second, b32_bytes(*value)},
      .control = successor};
}

/** @brief Stage a direct branch target. */
auto prepare_operation(const exec_ir::Bra& operation)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return PreparedEffect{
      .write = std::nullopt,
      .memory_write = std::nullopt,
      .control = std::get<exec_ir::Bra::Direct>(operation.variant).target};
}

/** @brief Stage an architectural thread exit. */
auto prepare_operation(const exec_ir::Exit&)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return PreparedEffect{.write = std::nullopt,
                        .memory_write = std::nullopt,
                        .control = ExitControl{}};
}

/** @brief Read a warp membership bitmap without resolving unused lane state. */
auto warp_sync_membermask(LaneResourceResolver& resolver,
                          const exec_ir::B32Operand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (const auto* immediate = std::get_if<common::RawValue>(&operand)) {
    if (const auto value = immediate->as_b32(); value) {
      return *value;
    } else {
      return std::unexpected(LaneFaultCause{value.error()});
    }
  }
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  return b32_operand(registers->get(), operand);
}

/** @brief Stage a rendezvous membership bitmap without changing thread state. */
auto prepare_operation(LaneResourceResolver& resolver,
                       const exec_ir::Bar& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto membermask = warp_sync_membermask(
      resolver, std::get<exec_ir::Bar::WarpSync>(operation.variant).membermask);
  if (!membermask) {
    return std::unexpected(membermask.error());
  }
  return PreparedEffect{.write = std::nullopt,
                        .memory_write = std::nullopt,
                        .control = successor,
                        .warp_sync = PreparedWarpSync{*membermask}};
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

/** @brief Adapt u32 less-than comparison to the common opcode dispatch signature. */
auto prepare_setp_lt_u32(LaneResourceResolver& resolver, const arith::context&,
                         const exec_ir::Instruction& operation,
                         std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = resolver.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_operation(view->get(), std::get<exec_ir::Setp>(operation),
                           *successor);
}

/** @brief Adapt the u32 load record to the common opcode dispatch signature. */
auto prepare_load_u32(LaneResourceResolver& resolver, const arith::context&,
                      const exec_ir::Instruction& operation,
                      std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto& load = std::get<exec_ir::Ld>(operation);
  return std::visit(
      [&](const auto& form) -> std::expected<PreparedEffect, LaneFaultCause> {
        using Form = std::remove_cvref_t<decltype(form)>;
        if constexpr (std::same_as<Form, exec_ir::Ld::GenericScalar>) {
          return prepare_load(resolver, form, exec_ir::AddressSpace::generic,
                              *successor);
        } else if constexpr (std::same_as<Form, exec_ir::Ld::ExplicitScalar>) {
          return prepare_load(resolver, form, form.state_space, *successor);
        } else {
          return std::unexpected(LaneFaultCause{common::RawValueError{
              common::RawWidth::b32, common::RawWidth::b64}});
        }
      },
      load.variant);
}

/** @brief Adapt the u32 store record to the common opcode dispatch signature. */
auto prepare_store_u32(LaneResourceResolver& resolver, const arith::context&,
                       const exec_ir::Instruction& operation,
                       std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto& store = std::get<exec_ir::St>(operation);
  return std::visit(
      [&](const auto& form) -> std::expected<PreparedEffect, LaneFaultCause> {
        using Form = std::remove_cvref_t<decltype(form)>;
        if constexpr (std::same_as<Form, exec_ir::St::GenericScalar>) {
          return prepare_store(resolver, form, exec_ir::AddressSpace::generic,
                               *successor);
        } else if constexpr (std::same_as<Form, exec_ir::St::ExplicitScalar>) {
          return prepare_store(resolver, form, form.state_space, *successor);
        } else {
          return std::unexpected(LaneFaultCause{common::RawValueError{
              common::RawWidth::b32, common::RawWidth::b64}});
        }
      },
      store.variant);
}

/** @brief Adapt warp rendezvous preparation to the common dispatch signature. */
auto prepare_bar_warp_sync(LaneResourceResolver& resolver,
                           const arith::context&,
                           const exec_ir::Instruction& operation,
                           std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(resolver, std::get<exec_ir::Bar>(operation),
                           *successor);
}

/** @brief Adapt direct branches to the common opcode dispatch signature. */
auto prepare_branch(LaneResourceResolver&, const arith::context&,
                    const exec_ir::Instruction& operation,
                    std::optional<common::ProgramCounter>)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(std::get<exec_ir::Bra>(operation));
}

/** @brief Adapt bare exits to the common opcode dispatch signature. */
auto prepare_exit(LaneResourceResolver&, const arith::context&,
                  const exec_ir::Instruction& operation,
                  std::optional<common::ProgramCounter>)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(std::get<exec_ir::Exit>(operation));
}

/** @brief Return the standard unsupported result for declaration-only forms. */
auto unsupported_instruction() -> std::unexpected<StepErrorCode> {
  return std::unexpected(StepErrorCode::unsupported_instruction);
}

/** @brief Check the supported memory ordering, scope, and cache controls. */
template <typename Form>
  requires requires(const Form& form) {
    form.semantics;
    form.scope;
    form.mmio;
    form.cache;
  }
[[nodiscard]] constexpr auto supports_memory_controls(const Form& form)
    -> bool {
  return (form.semantics == exec_ir::MemoryConsistency::omitted ||
          form.semantics == exec_ir::MemoryConsistency::weak) &&
         form.scope == exec_ir::MemoryScope::none && !form.mmio &&
         form.cache == exec_ir::CacheOperator::unspecified;
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

/** @brief Select only the scalar unsigned less-than predicate comparison. */
auto select_setp(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  const auto& setp = std::get<exec_ir::Setp>(operation);
  if (!std::holds_alternative<exec_ir::Setp::LtU32>(setp.variant) ||
      std::get<exec_ir::Setp::LtU32>(setp.variant).comparison !=
          exec_ir::ComparisonOperator::lt) {
    return unsupported_instruction();
  }
  return SelectedPreparer{prepare_setp_lt_u32, PrepareKind::scalar};
}

/** @brief Select only validated scalar-load address spaces and u32 handling. */
auto select_load(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  const auto& load = std::get<exec_ir::Ld>(operation);
  return std::visit(
      [](const auto& form) -> std::expected<SelectedPreparer, StepErrorCode> {
        using Form = std::remove_cvref_t<decltype(form)>;
        if constexpr (!std::same_as<Form, exec_ir::Ld::GenericScalar> &&
                      !std::same_as<Form, exec_ir::Ld::ExplicitScalar>) {
          return unsupported_instruction();
        } else {
          if (form.type != exec_ir::DataType::u32 ||
              !supports_memory_controls(form)) {
            return unsupported_instruction();
          }
          if constexpr (std::same_as<Form, exec_ir::Ld::GenericScalar>) {
            if (form.semantics != exec_ir::MemoryConsistency::omitted) {
              return unsupported_instruction();
            }
          } else if (form.state_space != exec_ir::AddressSpace::global &&
                     form.state_space != exec_ir::AddressSpace::param) {
            return unsupported_instruction();
          }
          return SelectedPreparer{prepare_load_u32, PrepareKind::scalar};
        }
      },
      load.variant);
}

/** @brief Select only validated scalar-store address spaces and u32 handling. */
auto select_store(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  const auto& store = std::get<exec_ir::St>(operation);
  return std::visit(
      [](const auto& form) -> std::expected<SelectedPreparer, StepErrorCode> {
        using Form = std::remove_cvref_t<decltype(form)>;
        if constexpr (!std::same_as<Form, exec_ir::St::GenericScalar> &&
                      !std::same_as<Form, exec_ir::St::ExplicitScalar>) {
          return unsupported_instruction();
        } else {
          if (form.type != exec_ir::DataType::u32 ||
              !supports_memory_controls(form)) {
            return unsupported_instruction();
          }
          if constexpr (std::same_as<Form, exec_ir::St::GenericScalar>) {
            if (form.semantics != exec_ir::MemoryConsistency::omitted) {
              return unsupported_instruction();
            }
          } else if (form.state_space != exec_ir::AddressSpace::global) {
            return unsupported_instruction();
          }
          return SelectedPreparer{prepare_store_u32, PrepareKind::scalar};
        }
      },
      store.variant);
}

/** @brief Select the collective preparation and commit path for warp sync. */
auto select_bar(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!std::holds_alternative<exec_ir::Bar::WarpSync>(
          std::get<exec_ir::Bar>(operation).variant)) {
    return unsupported_instruction();
  }
  return SelectedPreparer{prepare_bar_warp_sync, PrepareKind::warp_sync};
}

/** @brief Select the implemented direct branch form, if present. */
auto select_branch(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!std::holds_alternative<exec_ir::Bra::Direct>(
          std::get<exec_ir::Bra>(operation).variant)) {
    return unsupported_instruction();
  }
  return SelectedPreparer{prepare_branch, PrepareKind::scalar};
}

/** @brief Select the implemented bare exit form, if present. */
auto select_exit(const exec_ir::Instruction& operation)
    -> std::expected<SelectedPreparer, StepErrorCode> {
  if (!std::holds_alternative<exec_ir::Exit::Bare>(
          std::get<exec_ir::Exit>(operation).variant)) {
    return unsupported_instruction();
  }
  return SelectedPreparer{prepare_exit, PrepareKind::scalar};
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
      return PreparedEffect{.write = std::nullopt,
                            .memory_write = std::nullopt,
                            .control = *successor};
    }
  }
  return preparer.handler(resolver, arithmetic, instruction, successor);
}

}  // namespace ptxsim::inst_execute_engine::detail
