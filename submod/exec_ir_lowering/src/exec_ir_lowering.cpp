#include <ptxsim/exec_ir_lowering/exec_ir_lowering.hpp>

#include "exec_ir_lowering.gen.hpp"
#include "lowering_detail.hpp"

#include <cstddef>
#include <limits>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

namespace ptxsim::exec_ir_lowering {
namespace {

using detail::EntryParameterBinding;
using detail::EntryParameterTable;
using detail::LabelTable;
using detail::RegisterLayout;
using ptx_frontend::base::ScalarType;
using ptx_frontend::binding::ScopeId;
using ptx_frontend::binding::Symbol;
using ptx_frontend::binding::SymbolKind;
using ptx_frontend::binding::SymbolTable;

[[nodiscard]] auto error(
    LoweringErrorCode code,
    std::optional<std::uint32_t> function = std::nullopt,
    std::optional<std::uint32_t> instruction = std::nullopt,
    std::optional<std::uint32_t> symbol = std::nullopt,
    std::optional<exec_ir::ProgramError> program_error = std::nullopt)
    -> std::unexpected<LoweringError> {
  return std::unexpected(
      LoweringError{code, function, instruction, symbol, program_error});
}

[[nodiscard]] auto descendant_of(const SymbolTable& symbols, ScopeId scope,
                                 ScopeId function_scope) -> bool {
  const auto& scopes = symbols.scopes();
  for (std::size_t depth = 0; depth <= scopes.size(); ++depth) {
    if (scope.value == function_scope.value)
      return true;
    if (scope.value >= scopes.size())
      return false;
    const auto parent = scopes[scope.value].parent;
    if (!parent)
      return false;
    scope = *parent;
  }
  return false;
}

/** @brief Return whether @p value is a nonzero power-of-two byte alignment. */
[[nodiscard]] constexpr auto valid_alignment(std::size_t value) noexcept
    -> bool {
  return value != 0U && (value & (value - 1U)) == 0U;
}

/** @brief Owns the layout records and symbol bindings for one entry function. */
struct EntryParameterLayoutResult {
  /** @brief Source-ordered records copied into the executable function layout. */
  std::vector<exec_ir::EntryParameterLayout> layouts;
  /** @brief Lookup table used while lowering parameter address operands. */
  EntryParameterTable bindings;
  /** @brief Exact end of the last slot, with no trailing alignment padding. */
  std::size_t size = 0U;
};

[[nodiscard]] auto register_layout(const SymbolTable& symbols,
                                   const Symbol& function_symbol,
                                   std::uint32_t function, bool empty_body)
    -> std::expected<RegisterLayout, LoweringError> {
  if (!function_symbol.owned_scope) {
    if (empty_body)
      return RegisterLayout{};
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 std::nullopt, function_symbol.id.value);
  }
  if (function_symbol.owned_scope->value >= symbols.scopes().size()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function,
                 std::nullopt, function_symbol.id.value);
  }

  RegisterLayout result;
  for (const Symbol& symbol : symbols.symbols()) {
    if (!descendant_of(symbols, symbol.scope, *function_symbol.owned_scope) ||
        symbol.kind != SymbolKind::Variable || !symbol.state_space ||
        *symbol.state_space !=
            ptx_frontend::syntax_ast::AstStateSpace::Register) {
      continue;
    }
    if (symbol.vector_width && *symbol.vector_width != 1U) {
      return error(LoweringErrorCode::unsupported_type, function, std::nullopt,
                   symbol.id.value);
    }
    if (!symbol.type) {
      return error(LoweringErrorCode::malformed_resolved_ir, function,
                   std::nullopt, symbol.id.value);
    }
    const auto width = detail::raw_width_for(*symbol.type);
    const auto count = symbol.parameterized_count.value_or(1U);
    constexpr std::uint64_t max_slots =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
        1U;
    const auto slot_count = static_cast<std::uint64_t>(result.widths.size());
    if (!width || count == 0U || slot_count > max_slots ||
        static_cast<std::uint64_t>(count) > max_slots - slot_count) {
      return error(LoweringErrorCode::malformed_resolved_ir, function,
                   std::nullopt, symbol.id.value);
    }
    for (std::uint32_t member = 0; member < count; ++member) {
      const auto index = symbol.parameterized_count
                             ? std::optional<std::uint32_t>{member}
                             : std::nullopt;
      const auto slot = common::RegisterSlot{
          static_cast<std::uint32_t>(result.widths.size())};
      if (!result.slots.emplace(std::pair{symbol.id.value, index}, slot)
               .second) {
        return error(LoweringErrorCode::malformed_resolved_ir, function,
                     std::nullopt, symbol.id.value);
      }
      result.widths.push_back(*width);
    }
  }
  return result;
}

/**
 * @brief Derive validated ABI slots from owned source-ordered entry metadata.
 *
 * This refuses to infer slots by searching symbols: every direct entry input
 * must be represented once in @p function.entry_parameters.
 */
[[nodiscard]] auto entry_parameter_layout(
    const ptx_frontend::resolved_ir::ResolvedFunction& function,
    const SymbolTable& symbols, const Symbol& function_symbol,
    std::uint32_t function_index)
    -> std::expected<EntryParameterLayoutResult, LoweringError> {
  if (function.is_entry != function_symbol.function_is_entry) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                 std::nullopt, function_symbol.id.value);
  }
  if (!function.is_entry) {
    if (!function.entry_parameters.empty()) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, function_symbol.id.value);
    }
    return EntryParameterLayoutResult{};
  }
  if (!function_symbol.owned_scope ||
      function_symbol.owned_scope->value >= symbols.scopes().size()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                 std::nullopt, function_symbol.id.value);
  }

  EntryParameterLayoutResult result;
  result.layouts.reserve(function.entry_parameters.size());
  result.bindings.reserve(function.entry_parameters.size());
  for (const auto& parameter : function.entry_parameters) {
    if (parameter.symbol_id.value >= symbols.symbols().size()) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto& symbol = symbols.symbols()[parameter.symbol_id.value];
    const auto scalar_type = detail::scalar_type_for(parameter.type);
    if (!scalar_type || *scalar_type == ScalarType::Pred) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto element_size = ptx_frontend::base::scalar_size_of(*scalar_type);
    if (element_size == 0U || !parameter.alignment ||
        *parameter.alignment > std::numeric_limits<std::size_t>::max() ||
        parameter.is_array != parameter.array_extent.has_value() ||
        (parameter.array_extent && *parameter.array_extent == 0U) ||
        symbol.id != parameter.symbol_id ||
        symbol.scope != *function_symbol.owned_scope ||
        symbol.kind != SymbolKind::InputParameter || symbol.vector_width ||
        symbol.parameterized_count ||
        symbol.state_space !=
            ptx_frontend::syntax_ast::AstStateSpace::Parameter ||
        symbol.type != std::optional<std::string>{parameter.type} ||
        symbol.address_alignment != parameter.alignment) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto alignment = static_cast<std::size_t>(*parameter.alignment);
    if (!valid_alignment(alignment)) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto extent = parameter.array_extent.value_or(1U);
    if (extent > std::numeric_limits<std::size_t>::max() / element_size) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto size = static_cast<std::size_t>(element_size) *
                      static_cast<std::size_t>(extent);
    const auto remainder = result.size % alignment;
    const auto padding = remainder == 0U ? 0U : alignment - remainder;
    if (padding > std::numeric_limits<std::size_t>::max() - result.size ||
        size >
            std::numeric_limits<std::size_t>::max() - (result.size + padding)) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    const auto layout = exec_ir::EntryParameterLayout{
        .offset = result.size + padding,
        .size = size,
        .alignment = alignment,
    };
    if (!result.bindings
             .emplace(parameter.symbol_id.value,
                      EntryParameterBinding{layout, *scalar_type})
             .second) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, parameter.symbol_id.value);
    }
    result.layouts.push_back(layout);
    result.size = layout.offset + layout.size;
  }

  for (const auto& symbol : symbols.symbols()) {
    if (symbol.scope == *function_symbol.owned_scope &&
        symbol.kind == SymbolKind::InputParameter &&
        !result.bindings.contains(symbol.id.value)) {
      return error(LoweringErrorCode::invalid_entry_parameter_layout,
                   function_index, std::nullopt, symbol.id.value);
    }
  }
  return result;
}

[[nodiscard]] auto labels_for(
    const ptx_frontend::resolved_ir::ResolvedFunction& function,
    const SymbolTable& symbols, std::uint32_t function_index)
    -> std::expected<LabelTable, LoweringError> {
  LabelTable labels;
  if (function.body.size() > std::numeric_limits<std::uint32_t>::max()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index);
  }
  if (function.body.empty() && function.label_positions.empty()) {
    return labels;
  }
  const auto& function_symbol = symbols.symbols()[function.symbol_id.value];
  if (!function_symbol.owned_scope ||
      function_symbol.owned_scope->value >= symbols.scopes().size()) {
    return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                 std::nullopt, function.symbol_id.value);
  }
  for (const auto& label : function.label_positions) {
    if (label.symbol_id.value >= symbols.symbols().size() ||
        symbols.symbols()[label.symbol_id.value].kind != SymbolKind::Label ||
        !descendant_of(symbols, symbols.symbols()[label.symbol_id.value].scope,
                       *function_symbol.owned_scope) ||
        label.instruction_offset > function.body.size() ||
        label.instruction_offset > std::numeric_limits<std::uint32_t>::max() ||
        !labels
             .emplace(label.symbol_id.value,
                      common::ProgramCounter{
                          static_cast<std::uint32_t>(label.instruction_offset)})
             .second) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                   std::nullopt, label.symbol_id.value);
    }
  }
  return labels;
}

}  // namespace

auto lower(const ptx_frontend::resolved_ir::ResolvedModule& module)
    -> std::expected<exec_ir::ExecutableProgram, LoweringError> {
  if (module.functions.size() > std::numeric_limits<std::uint32_t>::max()) {
    return error(LoweringErrorCode::malformed_resolved_ir);
  }

  exec_ir::ProgramDefinition definition;
  definition.functions.reserve(module.functions.size());
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    const auto function_index = static_cast<std::uint32_t>(index);
    const auto& function = module.functions[index];
    if (function.symbol_id.value >= module.symbols.symbols().size()) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                   std::nullopt, function.symbol_id.value);
    }
    const auto& function_symbol =
        module.symbols.symbols()[function.symbol_id.value];
    if (function_symbol.kind != SymbolKind::Function) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index,
                   std::nullopt, function.symbol_id.value);
    }
    auto registers = register_layout(module.symbols, function_symbol,
                                     function_index, function.body.empty());
    auto entry_parameters = entry_parameter_layout(
        function, module.symbols, function_symbol, function_index);
    const auto labels = labels_for(function, module.symbols, function_index);
    if (!registers)
      return std::unexpected(registers.error());
    if (!entry_parameters)
      return std::unexpected(entry_parameters.error());
    if (!labels)
      return std::unexpected(labels.error());
    if (function.body.size() > std::numeric_limits<std::uint32_t>::max()) {
      return error(LoweringErrorCode::malformed_resolved_ir, function_index);
    }

    const auto begin = definition.instructions.size();
    for (std::size_t instruction_index = 0;
         instruction_index < function.body.size(); ++instruction_index) {
      const detail::BindingContext context{
          *registers,
          *labels,
          entry_parameters->bindings,
          function.is_entry,
          static_cast<std::uint32_t>(function.body.size()),
          function_index,
          static_cast<std::uint32_t>(instruction_index),
      };
      auto lowered = generated::lower_instruction(
          function.body[instruction_index], context);
      if (!lowered)
        return std::unexpected(lowered.error());
      definition.instructions.push_back(std::move(*lowered));
    }
    definition.functions.push_back(
        {common::FunctionId{function_index}, begin,
         static_cast<std::uint32_t>(function.body.size()),
         std::move(registers->widths), entry_parameters->size,
         std::move(entry_parameters->layouts)});
  }

  auto program = exec_ir::ExecutableProgram::create(std::move(definition));
  if (!program) {
    const auto function = program.error().function
                              ? std::optional{program.error().function->value()}
                              : std::nullopt;
    const auto instruction = program.error().pc
                                 ? std::optional{program.error().pc->value()}
                                 : std::nullopt;
    return error(LoweringErrorCode::program_validation_failed, function,
                 instruction, std::nullopt, program.error());
  }
  return std::move(*program);
}

}  // namespace ptxsim::exec_ir_lowering
