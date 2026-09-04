#include <ptxsim/exec_ir/exec_ir.hpp>

#include <iterator>
#include <limits>
#include <utility>

#include <fmt/format.h>

namespace ptxsim::exec_ir {
namespace {

[[nodiscard]] constexpr auto valid_width(common::RawWidth width) noexcept
    -> bool {
  switch (width) {
    case common::RawWidth::pred:
    case common::RawWidth::b8:
    case common::RawWidth::b16:
    case common::RawWidth::b32:
    case common::RawWidth::b64:
    case common::RawWidth::b128:
      return true;
  }
  return false;
}

/** @brief Reject cast-in enum values before they reach executable dispatch. */
[[nodiscard]] constexpr auto valid_address_space(AddressSpace space) noexcept
    -> bool {
  switch (space) {
    case AddressSpace::global:
      return true;
    case AddressSpace::generic:
      return false;
  }
  return false;
}

/** @brief Reject invalid memory controls before the engine observes them. */
[[nodiscard]] constexpr auto valid_memory_controls(MemoryConsistency semantics,
                                                   MemoryScope scope, bool mmio,
                                                   CacheOperator cache) noexcept
    -> bool {
  return (semantics == MemoryConsistency::omitted ||
          semantics == MemoryConsistency::weak) &&
         scope == MemoryScope::none && !mmio &&
         cache == CacheOperator::unspecified;
}

[[nodiscard]] auto error(
    ProgramErrorCode code,
    std::optional<common::FunctionId> function = std::nullopt,
    std::optional<common::ProgramCounter> pc = std::nullopt,
    std::optional<common::RegisterSlot> slot = std::nullopt,
    std::optional<common::RawWidth> expected = std::nullopt,
    std::optional<common::RawWidth> actual = std::nullopt)
    -> std::unexpected<ProgramError> {
  return std::unexpected(
      ProgramError{code, function, pc, slot, expected, actual});
}

[[nodiscard]] auto validate_slot(const FunctionLayout& layout,
                                 common::FunctionId function,
                                 common::ProgramCounter pc,
                                 common::RegisterSlot slot,
                                 common::RawWidth expected)
    -> std::expected<void, ProgramError> {
  const auto index = static_cast<std::size_t>(slot.value());
  if (index >= layout.register_widths.size()) {
    return error(ProgramErrorCode::operand_slot_out_of_range, function, pc,
                 slot);
  }
  const auto actual = layout.register_widths[index];
  if (actual != expected) {
    return error(ProgramErrorCode::operand_width_mismatch, function, pc, slot,
                 expected, actual);
  }
  return {};
}

[[nodiscard]] auto validate_b32_operand(const FunctionLayout& layout,
                                        common::FunctionId function,
                                        common::ProgramCounter pc,
                                        const B32Operand& operand)
    -> std::expected<void, ProgramError> {
  if (const auto* slot = std::get_if<common::RegisterSlot>(&operand)) {
    return validate_slot(layout, function, pc, *slot, common::RawWidth::b32);
  }
  const auto& immediate = std::get<common::RawValue>(operand);
  if (immediate.width() != common::RawWidth::b32) {
    return error(ProgramErrorCode::immediate_width_mismatch, function, pc,
                 std::nullopt, common::RawWidth::b32, immediate.width());
  }
  return {};
}

/** @brief Report whether validation implements an instruction's selected form. */
[[nodiscard]] auto supported_instruction_shape(const Instruction& instruction)
    -> bool {
  return std::visit(
      []<InstructionAlternative T>(const T& operation) {
        if constexpr (std::same_as<T, Mov>) {
          if (!std::holds_alternative<Mov::Scalar>(operation.variant))
            return false;
          const auto& form = std::get<Mov::Scalar>(operation.variant);
          const auto* operands =
              std::get_if<Mov::Scalar::ScalarOperands>(&form.operands);
          return operands != nullptr &&
                 std::holds_alternative<common::RegisterSlot>(operands->src);
        } else if constexpr (std::same_as<T, Add>) {
          return std::holds_alternative<Add::IntegerNoSat>(operation.variant);
        } else if constexpr (std::same_as<T, Ld>) {
          return std::holds_alternative<Ld::GenericScalar>(operation.variant) ||
                 std::holds_alternative<Ld::ExplicitScalar>(operation.variant);
        } else if constexpr (std::same_as<T, St>) {
          return std::holds_alternative<St::GenericScalar>(operation.variant) ||
                 std::holds_alternative<St::ExplicitScalar>(operation.variant);
        } else if constexpr (std::same_as<T, Bar>) {
          return std::holds_alternative<Bar::WarpSync>(operation.variant);
        } else if constexpr (std::same_as<T, Bra>) {
          return std::holds_alternative<Bra::Direct>(operation.variant);
        } else if constexpr (std::same_as<T, Exit>) {
          return std::holds_alternative<Exit::Bare>(operation.variant);
        } else {
          return false;
        }
      },
      instruction);
}

[[nodiscard]] auto validate_instruction(const Instruction& instruction,
                                        const FunctionLayout& layout,
                                        common::ProgramCounter pc)
    -> std::expected<void, ProgramError> {
  if (!supported_instruction_shape(instruction))
    return error(ProgramErrorCode::unsupported_instruction, layout.id, pc);
  if (const auto& predicate = execution_predicate(instruction); predicate) {
    if (const auto result = validate_slot(
            layout, layout.id, pc, predicate->source, common::RawWidth::pred);
        !result) {
      return std::unexpected(result.error());
    }
  }

  return std::visit(
      [&instruction, &layout, pc]<InstructionAlternative T>(
          const T& operation) -> std::expected<void, ProgramError> {
        if constexpr (std::same_as<T, Mov>) {
          if (!std::holds_alternative<Mov::Scalar>(operation.variant))
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          const auto& form = std::get<Mov::Scalar>(operation.variant);
          const auto& operands =
              std::get<Mov::Scalar::ScalarOperands>(form.operands);
          if (form.type != DataType::b32) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          if (const auto result = validate_slot(
                  layout, layout.id, pc, operands.dst, common::RawWidth::b32);
              !result) {
            return std::unexpected(result.error());
          }
          const auto* source = std::get_if<common::RegisterSlot>(&operands.src);
          if (source == nullptr)
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          return validate_slot(layout, layout.id, pc, *source,
                               common::RawWidth::b32);
        } else if constexpr (std::same_as<T, Add>) {
          if (!std::holds_alternative<Add::IntegerNoSat>(operation.variant))
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          const auto& form = std::get<Add::IntegerNoSat>(operation.variant);
          if (form.type != DataType::u32) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          if (const auto result = validate_slot(layout, layout.id, pc, form.dst,
                                                common::RawWidth::b32);
              !result) {
            return std::unexpected(result.error());
          }
          if (const auto result =
                  validate_b32_operand(layout, layout.id, pc, form.src1);
              !result) {
            return std::unexpected(result.error());
          }
          return validate_b32_operand(layout, layout.id, pc, form.src2);
        } else if constexpr (std::same_as<T, Ld>) {
          return std::visit(
              [&](const auto& form) -> std::expected<void, ProgramError> {
                using Form = std::remove_cvref_t<decltype(form)>;
                if constexpr (!std::same_as<Form, Ld::GenericScalar> &&
                              !std::same_as<Form, Ld::ExplicitScalar>) {
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                } else {
                  if (form.type != DataType::u32)
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  if (!valid_memory_controls(form.semantics, form.scope,
                                             form.mmio, form.cache))
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  if constexpr (std::same_as<Form, Ld::GenericScalar>) {
                    if (form.semantics != MemoryConsistency::omitted)
                      return error(ProgramErrorCode::unsupported_instruction,
                                   layout.id, pc);
                  } else if (!valid_address_space(form.state_space)) {
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  }
                  if (const auto result =
                          validate_slot(layout, layout.id, pc, form.dst,
                                        common::RawWidth::b32);
                      !result)
                    return std::unexpected(result.error());
                  const auto* address =
                      std::get_if<common::RegisterSlot>(&form.address.base);
                  if (address == nullptr || form.address.offset)
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  return validate_slot(layout, layout.id, pc, *address,
                                       common::RawWidth::b64);
                }
              },
              operation.variant);
        } else if constexpr (std::same_as<T, St>) {
          return std::visit(
              [&](const auto& form) -> std::expected<void, ProgramError> {
                using Form = std::remove_cvref_t<decltype(form)>;
                if constexpr (!std::same_as<Form, St::GenericScalar> &&
                              !std::same_as<Form, St::ExplicitScalar>) {
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                } else {
                  if (form.type != DataType::u32)
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  if (!valid_memory_controls(form.semantics, form.scope,
                                             form.mmio, form.cache))
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  if constexpr (std::same_as<Form, St::GenericScalar>) {
                    if (form.semantics != MemoryConsistency::omitted)
                      return error(ProgramErrorCode::unsupported_instruction,
                                   layout.id, pc);
                  } else if (!valid_address_space(form.state_space)) {
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  }
                  const auto* address =
                      std::get_if<common::RegisterSlot>(&form.address.base);
                  if (address == nullptr || form.address.offset)
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                  if (const auto result =
                          validate_slot(layout, layout.id, pc, *address,
                                        common::RawWidth::b64);
                      !result)
                    return std::unexpected(result.error());
                  return validate_slot(layout, layout.id, pc, form.src,
                                       common::RawWidth::b32);
                }
              },
              operation.variant);
        } else if constexpr (std::same_as<T, Bar>) {
          if (!std::holds_alternative<Bar::WarpSync>(operation.variant))
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          if (execution_predicate(instruction)) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          return validate_b32_operand(
              layout, layout.id, pc,
              std::get<Bar::WarpSync>(operation.variant).membermask);
        } else if constexpr (std::same_as<T, Bra>) {
          if (!std::holds_alternative<Bra::Direct>(operation.variant))
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          const auto& form = std::get<Bra::Direct>(operation.variant);
          if (form.target.value() >= layout.instruction_count) {
            return error(ProgramErrorCode::branch_target_out_of_range,
                         layout.id, pc);
          }
          return {};
        } else if constexpr (std::same_as<T, Exit>) {
          if (!std::holds_alternative<Exit::Bare>(operation.variant))
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          return {};
        } else {
          return error(ProgramErrorCode::unsupported_instruction, layout.id,
                       pc);
        }
      },
      instruction);
}

[[nodiscard]] auto layout_for(const std::vector<FunctionLayout>& functions,
                              common::FunctionId function)
    -> const FunctionLayout* {
  const auto index = static_cast<std::size_t>(function.value());
  if (index >= functions.size()) {
    return nullptr;
  }
  return &functions[index];
}

[[nodiscard]] auto validate_location(
    const std::vector<FunctionLayout>& functions, common::CodeLocation location)
    -> std::expected<const FunctionLayout*, ProgramError> {
  const auto* layout = layout_for(functions, location.function);
  if (layout == nullptr) {
    return error(ProgramErrorCode::function_not_found, location.function,
                 location.pc);
  }
  if (location.pc.value() >= layout->instruction_count) {
    return error(ProgramErrorCode::pc_out_of_range, location.function,
                 location.pc);
  }
  return layout;
}

}  // namespace

ExecutableProgram::ExecutableProgram(ProgramDefinition definition) noexcept
    : instructions_(std::move(definition.instructions)),
      functions_(std::move(definition.functions)) {}

auto ExecutableProgram::create(ProgramDefinition definition)
    -> std::expected<ExecutableProgram, ProgramError> {
  if (definition.functions.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return error(ProgramErrorCode::function_count_not_representable);
  }

  std::size_t expected_begin = 0;
  for (std::size_t index = 0; index < definition.functions.size(); ++index) {
    const auto& layout = definition.functions[index];
    if (layout.id.value() != index) {
      return error(ProgramErrorCode::function_id_not_dense, layout.id);
    }
    if (layout.begin != expected_begin) {
      return error(ProgramErrorCode::invalid_layout, layout.id);
    }
    if (layout.begin > definition.instructions.size() ||
        static_cast<std::size_t>(layout.instruction_count) >
            definition.instructions.size() - layout.begin) {
      return error(ProgramErrorCode::invalid_layout_range, layout.id);
    }
    for (const auto width : layout.register_widths) {
      if (!valid_width(width)) {
        return error(ProgramErrorCode::invalid_register_width, layout.id,
                     std::nullopt, std::nullopt, std::nullopt, width);
      }
    }
    expected_begin = layout.begin + layout.instruction_count;
  }
  if (expected_begin != definition.instructions.size()) {
    return error(ProgramErrorCode::invalid_layout);
  }

  for (const auto& layout : definition.functions) {
    for (std::uint32_t local_pc = 0; local_pc < layout.instruction_count;
         ++local_pc) {
      const auto pc = common::ProgramCounter{local_pc};
      const auto flat_offset =
          layout.begin + static_cast<std::size_t>(local_pc);
      if (const auto result = validate_instruction(
              definition.instructions[flat_offset], layout, pc);
          !result) {
        return std::unexpected(result.error());
      }
      if (local_pc == layout.instruction_count - 1U &&
          may_fallthrough(definition.instructions[flat_offset])) {
        return error(ProgramErrorCode::no_fallthrough, layout.id, pc);
      }
    }
  }

  return ExecutableProgram{std::move(definition)};
}

auto ExecutableProgram::fetch(common::CodeLocation location) const
    -> std::expected<std::reference_wrapper<const Instruction>, ProgramError> {
  const auto layout = validate_location(functions_, location);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  return std::cref(instructions_[(*layout)->begin + location.pc.value()]);
}

auto ExecutableProgram::flat_offset(common::CodeLocation location) const
    -> std::expected<std::size_t, ProgramError> {
  const auto layout = validate_location(functions_, location);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  return (*layout)->begin + location.pc.value();
}

auto ExecutableProgram::fallthrough(common::CodeLocation location) const
    -> std::expected<common::CodeLocation, ProgramError> {
  const auto layout = validate_location(functions_, location);
  if (!layout) {
    return std::unexpected(layout.error());
  }
  if (location.pc.value() + 1U >= (*layout)->instruction_count) {
    return error(ProgramErrorCode::no_fallthrough, location.function,
                 location.pc);
  }
  return common::CodeLocation{location.function,
                              common::ProgramCounter{location.pc.value() + 1U}};
}

auto to_string(const ExecutableProgram& program) -> std::string {
  std::string output;
  for (const auto& layout : program.functions_) {
    for (std::uint32_t local_pc = 0; local_pc < layout.instruction_count;
         ++local_pc) {
      if (!output.empty()) {
        output.push_back('\n');
      }
      const auto flat_offset =
          layout.begin + static_cast<std::size_t>(local_pc);
      fmt::format_to(std::back_inserter(output), "gpc{}  [func:{} pc:{}]  {}",
                     flat_offset, layout.id.value(), local_pc,
                     to_string(program.instructions_[flat_offset]));
    }
  }
  return output;
}

}  // namespace ptxsim::exec_ir
