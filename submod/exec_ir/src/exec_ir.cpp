#include <ptxsim/exec_ir/exec_ir.hpp>

#include <limits>
#include <utility>

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
    case AddressSpace::generic:
    case AddressSpace::global:
      return true;
  }
  return false;
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

[[nodiscard]] auto validate_instruction(const Instruction& instruction,
                                        const FunctionLayout& layout,
                                        common::ProgramCounter pc)
    -> std::expected<void, ProgramError> {
  if (instruction.predicate) {
    if (const auto result =
            validate_slot(layout, layout.id, pc, instruction.predicate->source,
                          common::RawWidth::pred);
        !result) {
      return std::unexpected(result.error());
    }
  }

  return std::visit(
      [&layout, pc]<detail::OperationAlternative T>(
          const T& operation) -> std::expected<void, ProgramError> {
        if constexpr (std::same_as<T, Move>) {
          if (operation.type != DataType::b32) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          if (const auto result =
                  validate_slot(layout, layout.id, pc, operation.destination,
                                common::RawWidth::b32);
              !result) {
            return std::unexpected(result.error());
          }
          return validate_slot(layout, layout.id, pc, operation.source,
                               common::RawWidth::b32);
        } else if constexpr (std::same_as<T, Add>) {
          if (operation.type != DataType::u32) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          if (const auto result =
                  validate_slot(layout, layout.id, pc, operation.destination,
                                common::RawWidth::b32);
              !result) {
            return std::unexpected(result.error());
          }
          if (const auto result =
                  validate_b32_operand(layout, layout.id, pc, operation.lhs);
              !result) {
            return std::unexpected(result.error());
          }
          return validate_b32_operand(layout, layout.id, pc, operation.rhs);
        } else if constexpr (std::same_as<T, Load>) {
          if (operation.type != DataType::u32 ||
              !valid_address_space(operation.space)) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          if (const auto result =
                  validate_slot(layout, layout.id, pc, operation.destination,
                                common::RawWidth::b32);
              !result) {
            return std::unexpected(result.error());
          }
          return validate_slot(layout, layout.id, pc, operation.address,
                               common::RawWidth::b64);
        } else if constexpr (std::same_as<T, Store>) {
          if (operation.type != DataType::u32 ||
              !valid_address_space(operation.space)) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          if (const auto result =
                  validate_slot(layout, layout.id, pc, operation.address,
                                common::RawWidth::b64);
              !result) {
            return std::unexpected(result.error());
          }
          return validate_slot(layout, layout.id, pc, operation.source,
                               common::RawWidth::b32);
        } else if constexpr (std::same_as<T, Branch>) {
          if (operation.target.value() >= layout.instruction_count) {
            return error(ProgramErrorCode::branch_target_out_of_range,
                         layout.id, pc);
          }
          return {};
        } else {
          static_assert(std::same_as<T, Exit>);
          return {};
        }
      },
      instruction.operation);
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

void append_register(std::string& output, common::RegisterSlot slot) {
  output += "reg:";
  output += std::to_string(slot.value());
}

void append_operand(std::string& output, const B32Operand& operand) {
  if (const auto* slot = std::get_if<common::RegisterSlot>(&operand)) {
    append_register(output, *slot);
    return;
  }
  output += common::to_string(std::get<common::RawValue>(operand));
}

void append_instruction(std::string& output, const Instruction& instruction) {
  if (instruction.predicate) {
    output += instruction.predicate->negated ? "@!" : "@";
    append_register(output, instruction.predicate->source);
    output.push_back(' ');
  }

  std::visit(
      [&output]<detail::OperationAlternative T>(const T& operation) {
        if constexpr (std::same_as<T, Move>) {
          output += "mov.b32 ";
          append_register(output, operation.destination);
          output += ", ";
          append_register(output, operation.source);
        } else if constexpr (std::same_as<T, Add>) {
          output += "add.u32 ";
          append_register(output, operation.destination);
          output += ", ";
          append_operand(output, operation.lhs);
          output += ", ";
          append_operand(output, operation.rhs);
        } else if constexpr (std::same_as<T, Load>) {
          switch (operation.space) {
            case AddressSpace::generic:
              output += "ld.u32 ";
              break;
            case AddressSpace::global:
              output += "ld.global.u32 ";
              break;
            default:
              output += "ld.invalid.u32 ";
              break;
          }
          append_register(output, operation.destination);
          output += ", [";
          append_register(output, operation.address);
          output += "]";
        } else if constexpr (std::same_as<T, Store>) {
          switch (operation.space) {
            case AddressSpace::generic:
              output += "st.u32 ";
              break;
            case AddressSpace::global:
              output += "st.global.u32 ";
              break;
            default:
              output += "st.invalid.u32 ";
              break;
          }
          output += "[";
          append_register(output, operation.address);
          output += "], ";
          append_register(output, operation.source);
        } else if constexpr (std::same_as<T, Branch>) {
          output += "bra pc:";
          output += std::to_string(operation.target.value());
        } else {
          static_assert(std::same_as<T, Exit>);
          output += "exit";
        }
      },
      instruction.operation);
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
      output += "@";
      output += std::to_string(flat_offset);
      output += "  [func:";
      output += std::to_string(layout.id.value());
      output += " pc:";
      output += std::to_string(local_pc);
      output += "]  ";
      append_instruction(output, program.instructions_[flat_offset]);
    }
  }
  return output;
}

}  // namespace ptxsim::exec_ir
