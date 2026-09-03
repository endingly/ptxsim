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

[[nodiscard]] auto validate_instruction(const Instruction& instruction,
                                        const FunctionLayout& layout,
                                        common::ProgramCounter pc)
    -> std::expected<void, ProgramError> {
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
          return validate_slot(layout, layout.id, pc, operands.src,
                               common::RawWidth::b32);
        } else if constexpr (std::same_as<T, Add>) {
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
                if (form.type != DataType::u32)
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                if (!valid_memory_controls(form.semantics, form.scope,
                                           form.mmio, form.cache))
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                if constexpr (std::same_as<std::remove_cvref_t<decltype(form)>,
                                           Ld::GenericScalar>) {
                  if (form.semantics != MemoryConsistency::omitted)
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                } else if (!valid_address_space(form.state_space)) {
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                }
                if (const auto result = validate_slot(
                        layout, layout.id, pc, form.dst, common::RawWidth::b32);
                    !result)
                  return std::unexpected(result.error());
                return validate_slot(layout, layout.id, pc, form.address,
                                     common::RawWidth::b64);
              },
              operation.variant);
        } else if constexpr (std::same_as<T, St>) {
          return std::visit(
              [&](const auto& form) -> std::expected<void, ProgramError> {
                if (form.type != DataType::u32)
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                if (!valid_memory_controls(form.semantics, form.scope,
                                           form.mmio, form.cache))
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                if constexpr (std::same_as<std::remove_cvref_t<decltype(form)>,
                                           St::GenericScalar>) {
                  if (form.semantics != MemoryConsistency::omitted)
                    return error(ProgramErrorCode::unsupported_instruction,
                                 layout.id, pc);
                } else if (!valid_address_space(form.state_space)) {
                  return error(ProgramErrorCode::unsupported_instruction,
                               layout.id, pc);
                }
                if (const auto result =
                        validate_slot(layout, layout.id, pc, form.address,
                                      common::RawWidth::b64);
                    !result)
                  return std::unexpected(result.error());
                return validate_slot(layout, layout.id, pc, form.src,
                                     common::RawWidth::b32);
              },
              operation.variant);
        } else if constexpr (std::same_as<T, Bar>) {
          if (execution_predicate(instruction)) {
            return error(ProgramErrorCode::unsupported_instruction, layout.id,
                         pc);
          }
          return validate_b32_operand(
              layout, layout.id, pc,
              std::get<Bar::WarpSync>(operation.variant).membermask);
        } else if constexpr (std::same_as<T, Bra>) {
          const auto& form = std::get<Bra::Direct>(operation.variant);
          if (form.target.value() >= layout.instruction_count) {
            return error(ProgramErrorCode::branch_target_out_of_range,
                         layout.id, pc);
          }
          return {};
        } else {
          static_assert(std::same_as<T, Exit>);
          return {};
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

/** @brief Append the data-type selector stored by an execution form. */
void append_data_type(std::string& output, DataType type) {
  switch (type) {
    case DataType::b32:
      output += "b32";
      return;
    case DataType::u32:
      output += "u32";
      return;
  }
  output += "invalid";
}

/** @brief Append non-default memory controls retained in an execution record. */
void append_memory_controls(std::string& output, MemoryConsistency semantics,
                            MemoryScope scope, bool mmio, CacheOperator cache) {
  if (semantics == MemoryConsistency::weak)
    output += ".weak";
  if (scope != MemoryScope::none)
    output += ".invalid_scope";
  if (mmio)
    output += ".mmio";
  if (cache != CacheOperator::unspecified)
    output += ".invalid_cache";
}

void append_instruction(std::string& output, const Instruction& instruction) {
  if (const auto& predicate = execution_predicate(instruction); predicate) {
    output += predicate->negated ? "@!" : "@";
    append_register(output, predicate->source);
    output.push_back(' ');
  }

  std::visit(
      [&output]<InstructionAlternative T>(const T& operation) {
        if constexpr (std::same_as<T, Mov>) {
          const auto& form = std::get<Mov::Scalar>(operation.variant);
          const auto& operands =
              std::get<Mov::Scalar::ScalarOperands>(form.operands);
          output += "mov.";
          append_data_type(output, form.type);
          output += " ";
          append_register(output, operands.dst);
          output += ", ";
          append_register(output, operands.src);
        } else if constexpr (std::same_as<T, Add>) {
          const auto& form = std::get<Add::IntegerNoSat>(operation.variant);
          output += "add.";
          append_data_type(output, form.type);
          output += " ";
          append_register(output, form.dst);
          output += ", ";
          append_operand(output, form.src1);
          output += ", ";
          append_operand(output, form.src2);
        } else if constexpr (std::same_as<T, Ld>) {
          std::visit(
              [&](const auto& form) {
                using Form = std::remove_cvref_t<decltype(form)>;
                output += "ld";
                if constexpr (!std::same_as<Form, Ld::GenericScalar>) {
                  output += form.state_space == AddressSpace::global
                                ? ".global"
                                : ".invalid";
                }
                append_memory_controls(output, form.semantics, form.scope,
                                       form.mmio, form.cache);
                output += ".";
                append_data_type(output, form.type);
                output += " ";
                append_register(output, form.dst);
                output += ", [";
                append_register(output, form.address);
                output += "]";
              },
              operation.variant);
        } else if constexpr (std::same_as<T, St>) {
          std::visit(
              [&](const auto& form) {
                using Form = std::remove_cvref_t<decltype(form)>;
                output += "st";
                if constexpr (!std::same_as<Form, St::GenericScalar>) {
                  output += form.state_space == AddressSpace::global
                                ? ".global"
                                : ".invalid";
                }
                append_memory_controls(output, form.semantics, form.scope,
                                       form.mmio, form.cache);
                output += ".";
                append_data_type(output, form.type);
                output += " ";
                output += "[";
                append_register(output, form.address);
                output += "], ";
                append_register(output, form.src);
              },
              operation.variant);
        } else if constexpr (std::same_as<T, Bar>) {
          output += "bar.warp.sync ";
          append_operand(output,
                         std::get<Bar::WarpSync>(operation.variant).membermask);
        } else if constexpr (std::same_as<T, Bra>) {
          const auto& form = std::get<Bra::Direct>(operation.variant);
          output += form.uni ? "bra.uni pc:" : "bra pc:";
          output += std::to_string(form.target.value());
        } else {
          static_assert(std::same_as<T, Exit>);
          output += "exit";
        }
      },
      instruction);
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
