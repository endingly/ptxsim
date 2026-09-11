#include <ptxsim/exec_ir/exec_ir.hpp>

#include <iterator>
#include <limits>
#include <unordered_set>
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

/** @brief Return whether @p value is a nonzero power-of-two byte alignment. */
[[nodiscard]] constexpr auto valid_alignment(std::size_t value) noexcept
    -> bool {
  return value != 0U && (value & (value - 1U)) == 0U;
}

/**
 * @brief Validate a source-ordered entry-parameter layout and its exact size.
 *
 * The function checks every addition before performing it, so accepted layouts
 * can always be consumed without a wrapping byte offset.
 */
[[nodiscard]] auto valid_entry_parameter_layout(
    const FunctionLayout& layout) noexcept -> bool {
  if (layout.entry_parameters.empty()) {
    return layout.entry_parameter_size == 0U;
  }

  std::size_t end = 0U;
  for (const auto& parameter : layout.entry_parameters) {
    if (!valid_alignment(parameter.alignment) || parameter.size == 0U) {
      return false;
    }
    const auto remainder = end % parameter.alignment;
    const auto padding = remainder == 0U ? 0U : parameter.alignment - remainder;
    if (padding > std::numeric_limits<std::size_t>::max() - end) {
      return false;
    }
    const auto offset = end + padding;
    if (parameter.offset != offset ||
        parameter.size > std::numeric_limits<std::size_t>::max() - offset) {
      return false;
    }
    end = offset + parameter.size;
  }
  return end == layout.entry_parameter_size;
}

[[nodiscard]] auto error(
    ProgramErrorCode code,
    std::optional<common::FunctionId> function = std::nullopt,
    std::optional<common::ProgramCounter> pc = std::nullopt,
    std::optional<common::RawWidth> actual = std::nullopt,
    std::optional<common::SymbolId> symbol = std::nullopt)
    -> std::unexpected<ProgramError> {
  return std::unexpected(ProgramError{code, function, pc, actual, symbol});
}

/** @brief Validate one frontend-independent static storage declaration. */
[[nodiscard]] auto valid_storage_declaration(
    const StorageDeclaration& declaration, std::size_t function_count) noexcept
    -> bool {
  switch (declaration.space) {
    case StorageSpace::global:
    case StorageSpace::constant:
    case StorageSpace::shared:
    case StorageSpace::local:
      break;
    default:
      return false;
  }
  switch (declaration.initialization) {
    case StorageInitialization::uninitialized:
    case StorageInitialization::zero:
    case StorageInitialization::explicit_bytes:
    case StorageInitialization::external:
      break;
    default:
      return false;
  }
  if (!valid_alignment(declaration.alignment)) {
    return false;
  }
  if (declaration.owner_function &&
      declaration.owner_function->value() >= function_count) {
    return false;
  }
  if (declaration.space == StorageSpace::local && !declaration.owner_function) {
    return false;
  }
  if (declaration.dynamic_shared &&
      (declaration.space != StorageSpace::shared ||
       declaration.initialization != StorageInitialization::external ||
       declaration.extent != 0U)) {
    return false;
  }
  if (declaration.initialization == StorageInitialization::external &&
      (declaration.space == StorageSpace::shared ||
       declaration.space == StorageSpace::local) &&
      !declaration.dynamic_shared) {
    return false;
  }
  if (declaration.initialization == StorageInitialization::external &&
      declaration.name.empty()) {
    return false;
  }
  if ((declaration.space == StorageSpace::shared ||
       declaration.space == StorageSpace::local) &&
      declaration.initialization != StorageInitialization::uninitialized &&
      declaration.initialization != StorageInitialization::external) {
    return false;
  }
  if ((declaration.space == StorageSpace::global ||
       declaration.space == StorageSpace::constant) &&
      declaration.initialization == StorageInitialization::uninitialized) {
    return false;
  }
  if (declaration.extent == 0U &&
      declaration.initialization != StorageInitialization::external) {
    return false;
  }
  const bool unsized_external =
      declaration.extent == 0U && !declaration.dynamic_shared &&
      declaration.initialization == StorageInitialization::external;
  if ((unsized_external && declaration.external_extent_multiple == 0U) ||
      (!unsized_external && declaration.external_extent_multiple != 1U)) {
    return false;
  }
  if (declaration.initialization == StorageInitialization::explicit_bytes) {
    return declaration.initializer_bytes.size() == declaration.extent;
  }
  return declaration.initializer_bytes.empty();
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
      functions_(std::move(definition.functions)),
      storage_declarations_(std::move(definition.storage_declarations)) {}

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
                     std::nullopt, width);
      }
    }
    if (!valid_entry_parameter_layout(layout)) {
      return error(ProgramErrorCode::invalid_entry_parameter_layout, layout.id);
    }
    expected_begin = layout.begin + layout.instruction_count;
  }
  if (expected_begin != definition.instructions.size()) {
    return error(ProgramErrorCode::invalid_layout);
  }

  std::unordered_set<std::uint32_t> storage_symbols;
  std::unordered_set<std::string> external_names;
  for (const auto& declaration : definition.storage_declarations) {
    if (!storage_symbols.insert(declaration.symbol.value()).second) {
      return error(ProgramErrorCode::duplicate_storage_symbol, std::nullopt,
                   std::nullopt, std::nullopt, declaration.symbol);
    }
    if (declaration.initialization == StorageInitialization::external &&
        !declaration.dynamic_shared &&
        !external_names.insert(declaration.name).second) {
      return error(ProgramErrorCode::invalid_storage_declaration,
                   declaration.owner_function, std::nullopt, std::nullopt,
                   declaration.symbol);
    }
    if (!valid_storage_declaration(declaration, definition.functions.size())) {
      return error(ProgramErrorCode::invalid_storage_declaration,
                   declaration.owner_function, std::nullopt, std::nullopt,
                   declaration.symbol);
    }
  }

  return ExecutableProgram{std::move(definition)};
}

auto ExecutableProgram::function_layout(common::FunctionId function) const
    -> std::expected<std::reference_wrapper<const FunctionLayout>,
                     ProgramError> {
  const auto* layout = layout_for(functions_, function);
  if (layout == nullptr) {
    return error(ProgramErrorCode::function_not_found, function);
  }
  return std::cref(*layout);
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

auto ExecutableProgram::storage_declarations() const noexcept
    -> const std::vector<StorageDeclaration>& {
  return storage_declarations_;
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
